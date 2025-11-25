#include "utils.h"
#include "network.h"
#include <unistd.h>
#include <stdlib.h>
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

ssize_t forward_all(int from_fd, int to_fd) {

    char buffer[NET_BUFFER_SIZE];
    ssize_t total = 0;

    while (1) {
        ssize_t n = recv(from_fd, buffer, sizeof(buffer), 0);
        if (n < 0) {
            if (errno == ECONNRESET) {
                printf("Upstream connection reset (client disconnected)\n");
            } else {
                perror("recv failed forward");
            }
            return -1;
        }
        if (n == 0) {
            // Connection closed by peer
            break;
        }

        ssize_t sent = write_all(to_fd, buffer, n);
        if (sent <= 0) {
            perror("write failed");
            return -1;
        }

    }

    return total;
};


int parse_port(const char *str) {
    char *endptr;
    long port = strtol(str, &endptr, 10);
    
    // Check for conversion errors
    if (endptr == str || *endptr != '\0') {
        fprintf(stderr, "Error: Port must be a number\n");
        return -1;
    }
    
    // Check port range
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Error: Port must be 1-65535\n");
        return -1;
    }
    
    return (int)port;
};
