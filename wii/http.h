#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

/* POSTs body to "http://name[:port]" plus path. Returns the HTTP status, or
   0 when no response arrived. Plain HTTP only. */
long http_post(const char *host, const char *path, const char *content_type, const void *body, size_t body_len);

#endif
