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
#include "network.h"
#include "utils.h"

int g_server_socket = 0;

volatile sig_atomic_t g_shutdown_requested = 0;

void handle_signal(int sig) {
    (void)sig;
    const char msg[] = "Shutting down...\n";

    write(STDERR_FILENO, msg, sizeof(msg) - 1);

    g_shutdown_requested = 1;
}

void setup_signal_handling() {
    struct sigaction sa = {0};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
};

int main(int argc, char* argv[]) {
    setup_signal_handling();

    int server_port;

    if (argc < 2) {
        printf("Usage: %s <server_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    server_port = parse_port(argv[1]);
    if (server_port < 0) {
        exit(EXIT_FAILURE);
    }

    g_server_socket = setup_server_socket(server_port);
    if (g_server_socket < 0) {
        perror("Fatal: Cannot create server socket");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", server_port);

    while (!g_shutdown_requested) {
        int client_fd = -1;
        int up_stream_socket = -1;

        struct sockaddr_in client_addr = {0};
        socklen_t addr_len = sizeof(client_addr);
        client_fd = accept(g_server_socket, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (g_shutdown_requested)
                break;
            if (errno == EINTR)
                continue;  // interrupted by signal
            perror("accept error");
            continue;
        }

        char request_buffer[NET_BUFFER_SIZE] = {0};
        size_t max_recv = sizeof(request_buffer) - 1;
        ssize_t bytes_received = read_http_request(client_fd,
                                                   request_buffer, max_recv);

        if (bytes_received == 0) {
            printf("Client closed connection\n");
            goto client_cleanup;
        }
        if (bytes_received < 0) {
            perror("recv failed client");
            goto client_cleanup;
        }

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

        up_stream_socket = connect_to_host(ip, upstream_client_port);
        if (up_stream_socket < 0) {
            perror("setup_upstream_socket failed");
            goto client_cleanup;
        }

        int injection_status = inject_connection_close(request_buffer, &bytes_received);

        if (injection_status == 0) {
            ssize_t result =
                write_all(up_stream_socket, request_buffer, bytes_received);
            if (result < 0) {
                perror("write failed");
                goto client_cleanup;
            }
        } else {
            const char* err_400_response = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
            write(client_fd, err_400_response, strlen(err_400_response));
            goto client_cleanup;
        }

        ssize_t total_response_size = forward_all(up_stream_socket, client_fd);

        if (total_response_size >= 0) {
            printf("Successfully forwarded %zd bytes of response.\n",
                   total_response_size);
            goto client_cleanup;
        } else {
            fprintf(stderr,
                    "Fatal error during response streaming (Code: %zd).\n", total_response_size);

            const char* err_500 = "HTTP/1.1 500 Internal Proxy Error\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
            write(client_fd, err_500, strlen(err_500));

            goto client_cleanup;
        }

    client_cleanup:
        if (up_stream_socket >= 0) {
            close(up_stream_socket);
        }
        if (client_fd >= 0) {
            close(client_fd);
            printf("Connection reseted by peer...\n");
        }
        if (req.host) {
            free(req.host);
        }
    }

    if (g_server_socket >= 0) {
        close(g_server_socket);
    }
    return 0;
};