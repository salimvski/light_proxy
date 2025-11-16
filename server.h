#ifndef SERVER_H
#define SERVER_H

#include <sys/types.h>
#include <stddef.h>
#include <unistd.h>


void parse_host(const char *request, char *host, int *port);

void parse_host(const char *request, char *host, int *port);

ssize_t forward_all(int from_fd, int to_fd)

ssize_t read_n_bytes(int sock, void* buf, size_t n);

void handle_sigint(int sig);

void run_server(int port);

#endif