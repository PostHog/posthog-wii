#ifndef TLS_H
#define TLS_H

#include <stdbool.h>

#include <gctypes.h>

/* One TLS 1.2 connection at a time, over a libogc socket. */

/* The certificate check compares dates in UTC, and the console's clock
   holds local time. minutes is console local time minus UTC. It is 0 until
   this call. */
void tls_set_utc_offset(int minutes);

/* Starts TLS on a connected socket. Returns true only when the server's
   certificate names host and chains to a trusted root. The trusted roots
   are wii/roots.pem and the owner's roots file on the SD card. The first
   call takes about a second, which it spends on noise_read. */
bool tls_open(s32 socket, const char *host);

/* Both return the number of bytes moved, 0 when the server closed the
   connection, or a negative number on failure. */
int tls_send(const void *data, int len);
int tls_recv(void *data, int len);

void tls_close(void);

#endif
