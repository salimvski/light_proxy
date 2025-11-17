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

#include "http_handler.h"

int server_socket = -1;

#define BUFFER_SIZE 4096

void handle_sigint(int sig) {
    printf("\nCaught SIGINT %d, shutting down server...\n", sig);
    if (server_socket >= 0) {
        close(server_socket);
    }
    exit(EXIT_SUCCESS);
};

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
            perror("recv failed");
            return -1;
        }
        if (n == 0) {
            // Connection closed by peer
            break;
        }

        ssize_t sent = 0;
        while (sent < n) {
            ssize_t s = write(to_fd, buffer + sent, n - sent);
            if (s <= 0) {
                perror("write failed");
                return -1;
            }
            sent += s;
        }

        total += n;
    }

    return total;
};

int setup_upstream_socket(char* address, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket upstream failed to create");
        exit(EXIT_FAILURE);
    }

    int opt = 1;

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("address %s and port %d\n", address, port);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(address);

    int is_connected = connect(sock, (struct sockaddr*)&addr, sizeof(addr));

    if (is_connected == 0) {
        printf("successfull connect to server at port %d\n", port);
    } else {
        perror("connect failed with upstream\n");
        close(sock);
        exit(EXIT_FAILURE);
    }

    return sock;
};

int setup_server_socket(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    if (listen(sock, 10) < 0) {
        perror("Listen failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    return sock;
};

int main(int argc, char* argv[]) {
    int server_port;

    if (argc < 2) {
        printf("Usage: %s <server_port>", argv[0]);
        exit(EXIT_FAILURE);
    }

    server_port = atoi(argv[1]);

    signal(SIGINT, handle_sigint);

    int server_socket = setup_server_socket(server_port);

    printf("Server listening on port %d...\n", server_port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd =
            accept(server_socket, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        char request_buffer[BUFFER_SIZE];
        int bytes_received = recv(client_fd, request_buffer, sizeof(request_buffer) - 1, 0);

        request_buffer[bytes_received] = '\0';

        char host[256];
        int upstream_client_port = 80;

        HttpRequest req;

        if (parse_http_request(request_buffer, &req) != 0) {
            printf("Failed to parse HTTP request\n");
            return 1;
        }

        // printf("Method: %s\nURL: %s\nVersion: %s\nHost: %s\n",
        //        req.method, req.url, req.version, req.host);

        char ip[32];
        if (resolve_host(req.host, ip, sizeof(ip)) == 0) {
            printf("Resolved IP: %s\n", ip);
        } else {
            printf("Failed to resolve host\n");
        }

        free(req.host);

        int up_stream_socket = -1;
        up_stream_socket = setup_upstream_socket(ip, upstream_client_port);

        ssize_t sent = write(up_stream_socket, request_buffer, strlen(request_buffer));
        if (sent < 0) {
            perror("write failed");
            exit(EXIT_FAILURE);
        }

        forward_all(up_stream_socket, client_fd);

        close(client_fd);
        close(up_stream_socket);
    }

    close(server_socket);
    return 0;
};