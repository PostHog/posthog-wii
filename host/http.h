#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

/* POSTs body to host plus path. Returns the HTTP status, or 0 when no
   response arrived. Writes the start of the response body to answer as a
   string. */
long http_post(const char *host, const char *path, const char *content_type, const void *body, size_t body_len,
               char *answer, size_t answer_cap);

#endif
