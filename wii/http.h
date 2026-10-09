#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

/* POSTs body to host plus path. host is "http://name[:port]" or
   "https://name[:port]". Returns the HTTP status, or 0 when no response
   arrived or an https host failed its certificate check. Writes the start
   of the response body to answer as a string. answer can be NULL when
   answer_cap is 0. */
long http_post(const char *host, const char *path, const char *content_type, const void *body, size_t body_len,
               char *answer, size_t answer_cap);

#endif
