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
#include "http_handler.h"

int server_socket = 0;

volatile sig_atomic_t shutdown_requested = 0;

#define BUFFER_SIZE 4096

void handle_signal(int sig) {
    const char msg[] = "Shutting down...\n";
    
    write(STDERR_FILENO, msg, sizeof(msg) - 1);
    
    shutdown_requested = 1;
}

void parse_host(const char* request, char* host, int* port) {
    *port = 80;

    const char* host_line = strstr(request, "Host:");
    if (!host_line)
        return;

    host_line += 5;

    while (*host_line == ' ')
        host_line++;

    int i = 0;
    while (*host_line && *host_line != ':' && *host_line != '\r' &&
           *host_line != '\n' && i < 255) {
        host[i++] = *host_line++;
    }
    host[i] = '\0';

    if (*host_line == ':') {
        host_line++;
        *port = atoi(host_line);
    }
};

void parse_and_rewrite_request(char* buffer, char* host, int* port) {
    parse_host(buffer, host, port);

    char method[16], url[1024], version[16];
    sscanf(buffer, "%15s %1023s %15s", method, url, version);

    char* p = strstr(url, "://");
    if (p)
        p += 3;

    p = strchr(p, '/');
    if (!p)
        p = "/";

    char new_request_line[1024];
    snprintf(new_request_line, sizeof(new_request_line), "%s %s %s\r\n", method,
             p, version);

    char* first_line_end = strstr(buffer, "\r\n");
    if (first_line_end) {
        char rest[BUFFER_SIZE];
        strcpy(rest, first_line_end + 2);

        snprintf(buffer, BUFFER_SIZE, "%s%s", new_request_line, rest);
    }
};

ssize_t forward_all(int from_fd, int to_fd) {

    char buffer[4096];
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

    server_socket = setup_server_socket(server_port);
    if (server_socket < 0) {
        perror("Fatal: Cannot create server socket");
        exit(EXIT_FAILURE);
    }

    int client_fd = -1;
    int up_stream_socket = -1;

    printf("Server listening on port %d...\n", server_port);

    while (!shutdown_requested) {
        up_stream_socket = -1;
        client_fd = -1;
        struct sockaddr_in client_addr = {0};
        socklen_t addr_len = sizeof(client_addr);
        client_fd = accept(server_socket, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (shutdown_requested) break;
            if (errno == EINTR) continue;  // interrupted by signal
            perror("accept error");
            continue;
        }

        char request_buffer[BUFFER_SIZE] = {0};
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

        char host[256];
        int upstream_client_port = 80;

        HttpRequest req = {0};

        if (parse_http_request(request_buffer, &req) != 0) {
            fprintf(stderr, "Error: Failed to parse HTTP request\n");
            goto client_cleanup;
        }

        char ip[32];
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

    if (server_socket >= 0) close(server_socket);
    return 0; 


};