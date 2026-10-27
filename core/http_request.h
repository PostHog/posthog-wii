#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <stddef.h>

/* One POST. host is "http://name[:port]" or "https://name[:port]". */
typedef struct {
    const char *host;
    const char *path;
    const char *content_type;
    const void *body;
    size_t body_len;
} http_request;

/* Sends the request. Returns the HTTP status, or 0 when no response arrived.
   Writes the start of the response body to answer as a string. answer can be
   NULL when answer_cap is 0. */
typedef long http_post_fn(const http_request *request, char *answer, size_t answer_cap);

#endif
