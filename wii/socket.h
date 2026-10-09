#ifndef SOCKET_H
#define SOCKET_H

#include <gctypes.h>

/* A wait of more than this for the other side fails a send or a receive. */
enum { SOCKET_WAIT_MS = 15000 };

/* Connects to a host name or an address. Returns a negative number when the
   network is down, the name does not resolve or the host does not accept. */
s32 socket_open(const char *name, unsigned port);

/* Both return the number of bytes moved, 0 when the other side closed the
   connection, or a negative number on failure. A send can move fewer bytes
   than len. */
s32 socket_send(s32 socket, const void *data, s32 len);
s32 socket_recv(s32 socket, void *data, s32 len);

void socket_close(s32 socket);

#endif
