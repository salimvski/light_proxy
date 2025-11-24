#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include "utils.h"
#include "network.h"
#include "http_handler.h"

int g_server_socket = 0;

volatile sig_atomic_t g_shutdown_requested = 0;

void handle_signal(int sig) {
    const char msg[] = "Shutting down...\n";
    
    write(STDERR_FILENO, msg, sizeof(msg) - 1);
    
    g_shutdown_requested = 1;
}

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

int setup_upstream_socket(char* address, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket upstream failed to create");
        return -1;
    }

    int opt = 1;

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        close(sock);
        return -1;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(address);

    int is_connected = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    if (is_connected != 0) {
        perror("Connect failed with upstream");
        close(sock);
        return -1;
    }

    return sock;
};

int setup_server_socket(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket failed");
        return -1;
    }

    int opt = 1;

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        close(sock);
        return -1;
    }

    struct sockaddr_in addr = {0};

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        close(sock);
        return -1;
    }

    if (listen(sock, 10) < 0) {
        perror("Listen failed");
        close(sock);
        return -1;
    }

    return sock;
};

int main(int argc, char* argv[]) {
    signal(SIGINT, handle_signal);

    int server_port;

    if (argc < 2) {
        printf("Usage: %s <server_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    server_port = atoi(argv[1]);

    // install signal handler (no SA_RESTART, so accept can be interrupted)
    struct sigaction sa = {0};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    g_server_socket = setup_server_socket(server_port);
    if (g_server_socket < 0) {
        perror("Fatal: Cannot create server socket");
        exit(EXIT_FAILURE);
    }

    int client_fd = -1;
    int up_stream_socket = -1;

    printf("Server listening on port %d...\n", server_port);

    while (!g_shutdown_requested) {
        up_stream_socket = -1;
        client_fd = -1;
        struct sockaddr_in client_addr = {0};
        socklen_t addr_len = sizeof(client_addr);
        client_fd = accept(g_server_socket, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (g_shutdown_requested) break;
            if (errno == EINTR) continue;  // interrupted by signal
            perror("accept error");
            continue;
        }

        char request_buffer[NET_BUFFER_SIZE] = {0};
        size_t max_recv = sizeof(request_buffer) - 1;
        
        int bytes_received = recv(client_fd, request_buffer, max_recv, 0);
        if (bytes_received == 0) {
            printf("Client closed connection\n");
            goto client_cleanup;
        }
        if (bytes_received < 0) {
            perror("recv failed client");
            goto client_cleanup;
        }

        char host[HTTP_MAX_HOST_LENGTH];
        int upstream_client_port = NET_DEFAULT_PORT;

        HttpRequest req = {0};

        if (parse_http_request(request_buffer, &req) != 0) {
            fprintf(stderr, "Error: Failed to parse HTTP request\n");
            goto client_cleanup;
        }

        char ip[NET_MAX_IP_LENGTH];
        if (resolve_host(req.host, ip, sizeof(ip)) != 0) {
            fprintf(stderr, "Error: Failed to resolve host %s\n", req.host);
            goto client_cleanup;
        }
        printf("Resolved IP: %s\n", ip);

        up_stream_socket = setup_upstream_socket(ip, upstream_client_port);
        if (up_stream_socket < 0) {
            perror("setup_upstream_socket failed");
            goto client_cleanup;
        }

        ssize_t result = 
        write_all(up_stream_socket, request_buffer, sizeof(request_buffer) - 1);
        if (result < 0) {
            perror("write failed");
            goto client_cleanup;
        }

        forward_all(up_stream_socket, client_fd);

    client_cleanup:
        if (up_stream_socket >= 0)
            close(up_stream_socket);
        if (client_fd >= 0)
            close(client_fd);
        if (req.host) free(req.host);
    }

    if (g_server_socket >= 0) close(g_server_socket);
    return 0; 


};