#include "server.h"

#define MAX_CONNECTION  (10000)

int epoll_fd; //fd refering to epoll 
julia_epoll_event_t events[MAX_EVENT_NUM];  
pool_t connection_pool;  //连接池
pool_t request_pool;	//请求池
pool_t accept_pool;	//接受池

static int heap_size = 0;
static connection_t* connections[MAX_CONNECTION + 1] = {NULL};

connection_t* open_connection(int fd) {
    // Disable Nagle algorithm
    int on = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));

    connection_t* c = pool_alloc(&connection_pool);
    c->active_time = time(NULL);
    c->fd = fd;
    c->side = C_SIDE_FRONT;
    c->r = pool_alloc(&request_pool);
    request_init(c->r, c);

    if (connection_register(c) == -1) {
        close_connection(c);
        return NULL;
    }

    set_nonblocking(c->fd);
    c->event.events = EVENTS_IN;
    c->event.data.ptr = c;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, c->fd, &c->event) == -1) {
        close_connection(c);
        return NULL;
    }
    return c;
}

connection_t* uwsgi_open_connection(request_t* r, location_t* loc) {
    assert(loc->pass);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    ERR_ON(fd == -1, "socket");

    struct sockaddr_in addr;
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(loc->port);
    if (inet_pton(AF_INET, loc->host.data, &addr.sin_addr) <= 0)
        return NULL;
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
        return NULL;
    
    connection_t* c = pool_alloc(&connection_pool);
    c->active_time = time(NULL);
    c->fd = fd;
    c->side = C_SIDE_BACK;
    c->r = r;

    if (connection_register(c) == -1) {
        close_connection(c);
        return NULL;
    }

    set_nonblocking(c->fd);
    c->event.events = EVENTS_IN;
    c->event.data.ptr = c;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, c->fd, &c->event) == -1) {
        close_connection(c);
        return NULL;
    }
    return c;
}

void close_connection(connection_t* c) {
    connection_unregister(c);
    // The events automatically removed
    close(c->fd);
    if (c->side == C_SIDE_FRONT) {
        if (c->r->uc) {
            close_connection(c->r->uc);
        }
        pool_free(&request_pool, c->r);
    } else {
        c->r->uc = NULL;
    }
    pool_free(&connection_pool, c);
}
//设置监听者
int add_listener(int* listen_fd) {
    julia_epoll_event_t ev; //epoll_event事件
    set_nonblocking(*listen_fd);    //设置非阻塞的fd
    ev.events = EVENTS_IN;	//可以进行读操作	
    ev.data.ptr = listen_fd;	//设置监听的文件描述符的指针
    return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, *listen_fd, &ev);
}
//设置非阻塞的文件:(1.得到当前文件访问权限:flag,2.设置当前文件)
int set_nonblocking(int fd) {
    int flag = fcntl(fd, F_GETFL, 0); //函数fcntl操作fd,这里F_GETFL指得到文件访问模式和文件状态标志.
    ABORT_ON(flag == -1, "fcntl: F_GETFL"); //错误处理
    flag |= O_NONBLOCK;  //添加上不可阻塞
    ABORT_ON(fcntl(fd, F_SETFL, flag) == -1, "fcntl: FSETFL");//设置当前不可阻塞`
    return 0;
}

#define P(i)    (i / 2)
#define L(i)    (i * 2)
#define R(i)    (L(i) + 1)

static void heap_shift_up(int idx) {
    int k = idx;
    connection_t* c = connections[k];
    while (P(k) > 0) {
        connection_t* pc = connections[P(k)];
        if (c->active_time >= pc->active_time)
            break;
        connections[k] = pc;
        connections[k]->heap_idx = k;
        k = P(k);
    }
    connections[k] = c;
    connections[k]->heap_idx = k;
}

static void heap_shift_down(int idx) {
    int k = idx;
    connection_t* c = connections[k];
    while (true) {
        int kid = L(k);
        if (R(k) <= heap_size &&
            connections[R(k)]->active_time < connections[L(k)]->active_time) {
            kid = R(k);
        }
        if (kid > heap_size ||
            c->active_time < connections[kid]->active_time) {
            break;
        }
        connections[k] = connections[kid]; 
        connections[k]->heap_idx = k;
        k = kid;
    }
    connections[k] = c;
    connections[k]->heap_idx = k;    
}

void connection_activate(connection_t* c) {
    c->active_time = time(NULL);
    heap_shift_down(c->heap_idx);
    if (c->side == C_SIDE_FRONT && c->r->uc)
        connection_activate(c->r->uc);
}

void connection_expire(connection_t* c) {
    c->active_time = time(NULL) - server_cfg.timeout - 1;
    heap_shift_up(c->heap_idx);
    if (c->side == C_SIDE_FRONT && c->r->uc)
        connection_expire(c->r->uc);
    else if (c->side == C_SIDE_BACK)
        c->r->uc = NULL;
}

bool connection_is_expired(connection_t* c) {
  return c->active_time + server_cfg.timeout < time(NULL);
}

// Return: 0, success; -1, fail;
int connection_register(connection_t* c) {
    if (heap_size + 1 > MAX_CONNECTION)
      return -1;
    connections[++heap_size] = c;
    heap_shift_up(heap_size);
    return 0;
}

void connection_unregister(connection_t* c) {
    assert(heap_size > 0);
    connections[c->heap_idx] = connections[heap_size];
    connections[c->heap_idx]->heap_idx = c->heap_idx;
    --heap_size;
    if (heap_size > 0) {
        heap_shift_down(c->heap_idx);
    }
}

void connection_sweep(void) {
    while (heap_size > 0) {
        connection_t* c = connections[1];
        if (time(NULL) >= c->active_time + server_cfg.timeout) {
            close_connection(c);
        } else {
            break;
        }
    }
}
