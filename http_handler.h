#define MAX_HEADERS 20
#define MAX_LINE 512

typedef struct {
    char *field;
    char *value;
} Header;

typedef struct {
    char method[16];
    char url[256];
    char version[16];
    char *host;
} HttpRequest;


int parse_http_request(const char *request, HttpRequest *req);

int resolve_host(const char *host, char *ip_str, size_t ip_len);