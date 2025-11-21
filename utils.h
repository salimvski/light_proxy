#ifndef UTILS_H
#define UTILS_H

#include <sys/types.h>

ssize_t write_all(int fd, const void *buffer, size_t count);
ssize_t read_all(int fd, void *buffer, size_t count);

#endif /* UTILS_H */