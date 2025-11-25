#ifndef UTILS_H
#define UTILS_H

#include <sys/types.h>

ssize_t write_all(int fd, const void *buffer, size_t count);
ssize_t read_all(int fd, void *buffer, size_t count);

int parse_port(const char *str);

ssize_t forward_all(int from_fd, int to_fd);

#endif /* UTILS_H */