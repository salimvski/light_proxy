#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "http_handler.h"



int parse_http_request(const char *request, HttpRequest *req) {
    if (!request || !req) return -1;

    char *req_copy = strdup(request);
    if (!req_copy) return -1;

    char *line = strtok(req_copy, "\r\n");
    if (!line) { free(req_copy); return -1; }

    // Parse request line: METHOD URL VERSION
    if (sscanf(line, "%15s %255s %15s", req->method, req->url, req->version) != 3) {
        free(req_copy);
        return -1;
    }

    req->host = NULL;
    while ((line = strtok(NULL, "\r\n"))) {
        if (strncasecmp(line, "Host:", 5) == 0) {
            char *h = line + 5;
            while (*h == ' ' || *h == '\t') h++;
            req->host = strdup(h);
            break;
        }
    }

    free(req_copy);
    return 0;
}


int resolve_host(const char *host, char *ip_str, size_t ip_len) {
    if (!host || !ip_str) return -1;

    struct in_addr addr;
    // Try if host is already an IP
    if (inet_pton(AF_INET, host, &addr) == 1) {
        strncpy(ip_str, host, ip_len);
        ip_str[ip_len-1] = '\0';
        return 0;
    }

    // Otherwise use DNS
    struct hostent *he = gethostbyname(host);
    if (!he) return -1;

    memcpy(&addr, he->h_addr_list[0], sizeof(struct in_addr));
    strncpy(ip_str, inet_ntoa(addr), ip_len);
    ip_str[ip_len-1] = '\0';
    return 0;
}

