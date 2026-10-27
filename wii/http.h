#ifndef HTTP_H
#define HTTP_H

#include "http_request.h"

/* Also returns 0 when an https host fails its certificate check. */
http_post_fn http_post;

/* A line of text that says why the last http_post returned 0. */
const char *http_failure(void);

#endif
