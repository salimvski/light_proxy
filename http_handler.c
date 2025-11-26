#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "http_handler.h"
#include "network.h"



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
            if (req->host == NULL) {
                perror("strdup failed: Out of memory");
                free(req_copy);
                return -2;
            }
            break;
        }
    }

    free(req_copy);
    return 0;
}


int resolve_host(const char *host, char *ip_str, size_t ip_len) {
    if (!host || !ip_str) return -1;

    struct in_addr addr = {0};
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


ssize_t read_http_request(int client_fd, char* buffer, size_t buffer_size) {
    ssize_t bytes = recv(client_fd, buffer, buffer_size - 1, 0);
    if (bytes == 0) return 0;          // Client closed
    if (bytes < 0) {
        perror("recv failed client");
        return -1;
    }
    buffer[bytes] = '\0';
    return bytes;
}


/**
 * @brief Injects the "Connection: close" header into an HTTP request.
 * * This prevents the upstream server from using Keep-Alive, forcing a socket closure 
 * after the response, which is vital for single-threaded proxies.
 * * @param request_buffer Pointer to the buffer holding the client request.
 * @param current_length Pointer to the current length of the request data.
 * @return 0 on success, -1 on malformed request, -2 if buffer is too small.
 */
int inject_connection_close(char *request_buffer, ssize_t *current_length) {
    
    // 1. Locate the End-of-Headers Terminator (\r\n\r\n)
    char *end_of_headers = strstr(request_buffer, "\r\n\r\n");

    if (end_of_headers == NULL) {
        fprintf(stderr, "Error: Malformed request, header terminator not found.\n");
        return -1; // Malformed request
    }
    
    size_t original_len = (size_t)(*current_length);
    size_t new_len = original_len + CLOSE_HEADER_LEN;

    // 2. Safety Check: Ensure the new length fits in the buffer
    if (new_len >= NET_BUFFER_SIZE) {
        fprintf(stderr, "Error: Buffer overflow risk during header injection.\n");
        return -2; // Buffer too small
    }

    // 3. Shift the Terminator (\r\n\r\n) and any following data to the right
    //    This creates space for the new header.
    //    The amount of data to shift is the length from the terminator to the end of the request.
    memmove(end_of_headers + CLOSE_HEADER_LEN, 
            end_of_headers, 
            original_len - (end_of_headers - request_buffer));
            
    // 4. Inject the new "Connection: close" header into the newly created space
    memcpy(end_of_headers, CONNECTION_CLOSE_HEADER, CLOSE_HEADER_LEN);

    // 5. Update the total size of the request
    *current_length = (ssize_t)new_len;

    return 0; // Success
}

