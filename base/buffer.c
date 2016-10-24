#include "buffer.h"
#include "string.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>


// Read data to buffer
// Return: if haven't read eof, bytes read_n; 
//         else, 0 - read_n;
int buffer_recv(buffer_t* buffer, int fd)
{
    assert(buffer->end < buffer->limit);
    int read_n = 0;
    while (buffer->end < buffer->limit) {
        int margin = buffer->limit - buffer->end;
        int len = recv(fd, buffer->end, margin, 0);
        if (len == 0) // EOF
            return -read_n;
        if (len == -1) {
            if (errno == EAGAIN)
                break;
            perror("recv");
            return ERR_INTERNAL_ERROR;
        }
        read_n += len;
        buffer->end += len;
    };  // We may have not read all data
    return read_n;
}

int buffer_send(buffer_t* buffer, int fd)
{
    int sent = 0;
    while (buffer_size(buffer) > 0) {
        int len = send(fd, buffer->begin, buffer_size(buffer), 0);
        if (len == -1) {
            if (errno == EAGAIN)
                break;
            else if (errno == EPIPE) {
                // TODO(wgtdkp): the connection is broken
            }
            perror("send");
            return ERR_INTERNAL_ERROR;
        }
        sent += len;
        buffer->begin += len;
    };
    if (buffer_size(buffer) == 0)
        buffer_clear(buffer);
    return sent;
}

int buffer_append_string(buffer_t* buffer, const string_t* str)
{
    int margin = buffer->limit - buffer->end;
    assert(margin > 0);
    int appended = min(margin, str->len);
    memcpy(buffer->end, str->data, appended);
    buffer->end += appended;
    return appended;
}

int buffer_print(buffer_t* buffer, const char* format, ...)
{
    va_list args;
    va_start (args, format);
    int margin = buffer->limit - buffer->end;
    assert(margin > 0);
    int len = vsnprintf(buffer->end, margin, format, args);
    buffer->end += len;
    va_end (args);
    return len;
}

void print_buffer(buffer_t* buffer)
{
    for(char* p = buffer->begin; p != buffer->end; ++p) {
        printf("%c", *p);
        fflush(stdout);
    }
    printf("\n");
}
