#include "utils.h"
#include <unistd.h>
#include <errno.h>


ssize_t write_all(int fd, const void *buffer, size_t count) 
{
    const char *buffer_ptr = (const char *)buffer;
    size_t data_sent = 0;

    while (data_sent < count) {
        ssize_t sent = write(fd, buffer_ptr + data_sent, 
                            count - data_sent);

        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        data_sent += sent;
    }

    return data_sent;
};