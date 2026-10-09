#ifndef SEND_H
#define SEND_H

#include <stddef.h>

/* POSTs a /batch request body to a PostHog host such as
   "https://us.i.posthog.com". Returns the HTTP status, or 0 when no
   response arrived. */
long send_batch(const char *host, const char *json, size_t json_len);

#endif
