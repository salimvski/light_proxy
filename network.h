#ifndef NETWORK_H
#define NETWORK_H

typedef enum {
    NET_MAX_IP_LENGTH = 46,
    NET_MAX_PORT = 65535,
    NET_DEFAULT_PORT = 80,
    NET_BUFFER_SIZE = 4096,
} net_limits_t;

int setup_server_socket(int port);
int connect_to_host(const char* address, int port);

#endif /* NETWORK_H */