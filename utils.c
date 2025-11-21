#include "utils.h"
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <stdio.h>


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


ssize_t read_all(int fd, void *buffer, size_t count) 
{
    char *buffer_ptr = (char *)buffer;
    size_t data_received = 0;

    while (data_received < count) {
        ssize_t sent = recv(fd, buffer_ptr + data_received, 
                            count - data_received, 0);

        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        if (sent == 0) break;  // Return what we've read so far

        data_received += sent;
        printf("Data received %d", data_received);
    }

    return data_received;
};