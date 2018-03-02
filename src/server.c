#include "server.h"

static int startup(unsigned short port);
static int server_init(void);
static void usage(void);

static void usage(void) {
    printf("Usage:\n"
           "    julia [-s signal]    signal could only be stop\n");
    exit(OK);
}

static void save_pid(int pid) {
    FILE* fp = fopen(INSTALL_DIR "julia.pid", "w");
    if (fp == NULL) {
        ju_error("open pid file: " INSTALL_DIR "julia.pid failed");
        exit(ERROR);
    }
    fprintf(fp, "%d", pid);
    fclose(fp);
}

static int get_pid(void) {
    FILE* fp = fopen(INSTALL_DIR "julia.pid", "a+"); //打开文件
    if (fp == NULL) {  
        ju_error("open pid file: " INSTALL_DIR "julia.pid failed");
        exit(ERROR);
    }
    int pid = 0;
    fscanf(fp, "%d", &pid);   //输入进程id
    return pid;
}

static void send_signal(const char* signal) {
    if (strncasecmp(signal, "stop", 4) == 0) { //大小写匹配
        kill(-get_pid(), SIGINT); //终止进程组内pid中所有的进程(--pid)
    } else {
        usage();
    }
    exit(OK);
}
//发生中断的时候,调用这个相应函数
static void sig_int(int signo) {
    ju_log("julia exited...");
    save_pid(0);
    kill(-getpid(), SIGINT);	//给进程发送一个信号,终止当前信号的信号组内的所有进程
    raise(SIGKILL);	//把信号发送给调用者
}

static int startup(uint16_t port) {
    // If the client closed the c, then it will cause SIGPIPE
    // Here simplely ignore this SIG
    signal(SIGPIPE, SIG_IGN);  //这个信号是什么,怎么产生的,使用管道的时候用到
    signal(SIGINT, sig_int);	//
    
    // Open socket, bind and listen
    int listen_fd = 0;		
    struct sockaddr_in server_addr = {0}; //配置清零的时候,可以这么做
    int addr_len = sizeof(server_addr);	
    listen_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (listen_fd == ERROR) {
        return ERROR;
    }

    int on = 1;
    /*函数原型:
     *int setsockopt(int sockfd, int level, int optname,
                           const void *optval, socklen_t optlen);
     *
     * 头文件:<sys/type.h><sys/socket.h>
     * 设置sockfd的,level是SOL_SOCKET,是socket层的属性(也可以指定tcp协议的属性.)
     * optval和optlen是被用来访问当前可选值.
     */
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    if (server_cfg.workers.size > 1) { //对于工作的一个cpu设置大小大于1的话
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on)); //对应的端口重用
    }

    memset((void*)&server_addr, 0, addr_len); //再次初始化?对一个结构体的初始化使用memset?
    server_addr.sin_family = AF_INET;	    //服务器协议
    server_addr.sin_port = htons(port);	    //服务器端口
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);	//任意的入地址
    if (bind(listen_fd, (struct sockaddr*)&server_addr, addr_len) < 0) { //绑定服务器的地址结构
        return ERROR;
    }

    if (listen(listen_fd, 1024) < 0) {	//最大连接数量
        return ERROR;
    }
    return listen_fd;
}
//变量初始化,
static int server_init(void) {
    // Set limits
    struct rlimit nofile_limit = {65535, 65535}; //设置资源的硬限制和软限制
    setrlimit(RLIMIT_NOFILE, &nofile_limit);	//设置每个进程最多文件最多打开的数目
    
    parse_init();	    //解析器初始化?
    header_map_init();
    mime_map_init();
    
    pool_init(&connection_pool, sizeof(connection_t), 8, 0);	//池子初始化
    pool_init(&request_pool, sizeof(request_t), 8, 0);
    
    epoll_fd = epoll_create1(0); //监视多个文件描述符,观察I/O是否可用
    ABORT_ON(epoll_fd == ERROR, "epoll_create1");   //为什么使用assert
    return OK;
}

static void accept_connection(int listen_fd) {
    while (true) {
        int c_fd = accept(listen_fd, NULL, NULL);
        if (c_fd == ERROR) {
            ERR_ON((errno != EWOULDBLOCK), "accept");
            break;
        }
        open_connection(c_fd);
    }
}

int main(int argc, char* argv[]) {
    if (argc >= 2) {   //健壮的体现
        if (argv[1][0] != '-') {  
            usage();
        }
        switch (argv[1][1]) {
        case 'h': usage(); break;
        case 's': send_signal(argv[2]); break; 
        default: usage(); break;
        }
    }

    get_pid();  //从文件中输入pid

    if (config_load(&server_cfg) != OK) { //配置服务器
        raise(SIGINT);
    }

    if (server_cfg.debug) {
        goto work; //直接跳转到work地方
    }

    if (server_cfg.daemon) {
        daemon(1, 0);	//1表示不改变当前工作目录,0表示把stdin,stdout,stderr输出到/dev/null中
    }
    printf("pid is %d",get_pid());
    if (get_pid() != 0) { //做一个判断,开始的时候写入位零,之后位一个进程号
        ju_error("julia has already been running...");
        exit(ERROR);
    }
    save_pid(getpid());//得到pid存入就好

    int nworker = 0;
    //父进程存在这个函数中，
    while (true) { 
        if (nworker >= server_cfg.workers.size) { //当工作数目大于当前服务器所接受的进程数目的时候
            int stat;
            wait(&stat); //父进程只能等待子进程退出
            /*宏文件，WIFEXITED,WIFSIGNALED两个 #include<sys/wait.h>和<stdlib.h>
	     *WIFEXITED是正常退出（exit,_exit），WIFSIGNALED是异常退出（signal传送），对应类型进行一个判断。
	     *如果是异常类型可以通过宏WTERMSIG可以通过这个命令来得到signal号，
	     *abort 系统调用发送signal为6
	     *除零操作返回signal为8
	     * */
	    if (WIFEXITED(stat))
                raise(SIGINT);
            // Worker unexpectly exited, restart it
            ju_log("julia failed, restarting...");
        }
        int pid = fork(); //得到一个线程
        ABORT_ON(pid < 0, "fork"); //出错以后abort?为什么要abort?终止进程.
        if (pid == 0) //子进程
            break;
        int* worker = vector_at(&server_cfg.workers, nworker++); //nworker++人数,
        *worker = pid;
    }

work:;
    int listen_fd;		//
    if (server_init() != OK ||  //进行服务端初始化
        (listen_fd = startup(server_cfg.port)) < 0) { //启动服务器,在指定的端口
        ju_error("startup server failed");	    //
        exit(ERROR);
    }
    
    ju_log("julia started...");	    //监听到
    ju_log("listening at port: %u", server_cfg.port);	//端口
    assert(add_listener(&listen_fd) != ERROR); //添加监听者

wait:;
    int nfds = epoll_wait(epoll_fd, events, MAX_EVENT_NUM, 30);	//设置监听
    if (nfds == ERROR) { //错误
        ABORT_ON(errno != EINTR, "epoll_wait");
    }
    
    // TODO(wgtdkp): multithreading here: separate fds to several threads
    for (int i = 0; i < nfds; ++i) {
        int fd = *((int*)(events[i].data.ptr));
        if (fd == listen_fd) {
            // We could accept more than one c per request
            accept_connection(listen_fd);
            continue;
        }
        
        int err;
        connection_t* c = events[i].data.ptr;
        if (!connection_is_expired(c) && (events[i].events & EPOLLIN)) {
            err = (c->side == C_SIDE_BACK) ?
                  handle_upstream(c): handle_request(c);
            err == ERROR ? connection_expire(c): connection_activate(c);
        }
        if (!connection_is_expired(c) && (events[i].events & EPOLLOUT)) {
            err = (c->side == C_SIDE_BACK) ?
                  handle_pass(c): handle_response(c);
            err == ERROR ? connection_expire(c): connection_activate(c);
        }
    }
    connection_sweep();
    goto wait;

    close(epoll_fd);
    close(listen_fd);
    return OK;
}
