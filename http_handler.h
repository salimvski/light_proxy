#ifndef HTTP_HANDLER_H
#define HTTP_HANDLER_H


#define CONNECTION_CLOSE_HEADER "\r\nConnection: close"
#define CLOSE_HEADER_LEN (sizeof(CONNECTION_CLOSE_HEADER) - 1)

typedef enum {
    HTTP_MAX_URL_LENGTH = 256,
    HTTP_MAX_METHOD_LENGTH = 16,
    HTTP_MAX_VERSION_LENGTH = 16,
    HTTP_MAX_HOST_LENGTH = 253,
} http_limits_t;

typedef struct {
    char *field;
    char *value;
} Header;

typedef struct {
    char method[HTTP_MAX_METHOD_LENGTH];
    char url[HTTP_MAX_URL_LENGTH];
    char version[HTTP_MAX_VERSION_LENGTH];
    char *host;
} HttpRequest;

int parse_http_request(const char *request, HttpRequest *req);

int inject_connection_close(char *request_buffer, ssize_t *current_length);

int resolve_host(const char *host, char *ip_str, size_t ip_len);

ssize_t read_http_request(int client_fd, char* buffer, size_t buffer_size);

ssize_t forward_all(int from_fd, int to_fd);

#endif /* HTTP_HANDLER_H */