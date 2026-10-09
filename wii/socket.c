#include "socket.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <network.h>

enum { NAME_CAP = 64, RETRY_DELAY_US = 100000, NETWORK_TRIES = 150, SEND_PIECE = 4096 };

/* A console with no usable network, such as a Wii mini or one whose router
   it cannot join, must not hang here, so the number of tries is bounded. */
static bool network_ready(void) {
    static bool ready;
    for (int tries = 0; !ready && tries < NETWORK_TRIES; tries++) {
        s32 result = net_init();
        if (result == 0) {
            ready = true;
        } else if (result != -EAGAIN) {
            break;
        } else {
            usleep(RETRY_DELAY_US);
        }
    }
    return ready;
}

/* The last answer is kept because every request of an upload goes to the
   same host and each lookup is a round trip. */
static bool resolve(const char *name, struct in_addr *address) {
    static char known_name[NAME_CAP];
    static struct in_addr known_address;
    if (known_name[0] == '\0' || strcmp(name, known_name) != 0) {
        struct hostent *entry = NULL;
        if (!inet_aton(name, &known_address)) {
            entry = net_gethostbyname(name);
            if (!entry || entry->h_length != sizeof known_address || !entry->h_addr_list[0]) {
                return false;
            }
            memcpy(&known_address, entry->h_addr_list[0], sizeof known_address);
        }
        snprintf(known_name, sizeof known_name, "%s", name);
    }
    *address = known_address;
    return true;
}

static bool wait_for(s32 socket, u32 events) {
    struct pollsd poll = {.socket = socket, .events = events};
    return net_poll(&poll, 1, SOCKET_WAIT_MS) > 0;
}

s32 socket_open(const char *name, unsigned port) {
    struct sockaddr_in address = {0};
    address.sin_len = sizeof address;
    address.sin_family = AF_INET;
    address.sin_port = htons((u16)port);
    if (!network_ready() || !resolve(name, &address.sin_addr)) {
        return -1;
    }
    s32 socket = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (socket >= 0 && net_connect(socket, (struct sockaddr *)&address, sizeof address) < 0) {
        net_close(socket);
        return -1;
    }
    return socket;
}

s32 socket_send(s32 socket, const void *data, s32 len) {
    /* net_send returns only after IOS has taken every byte. The wait shows
       that IOS can take some, so each call offers a small piece. */
    if (len > SEND_PIECE) {
        len = SEND_PIECE;
    }
    return wait_for(socket, POLLOUT) ? net_send(socket, data, len, 0) : -ETIMEDOUT;
}

s32 socket_recv(s32 socket, void *data, s32 len) {
    return wait_for(socket, POLLIN) ? net_recv(socket, data, len, 0) : -ETIMEDOUT;
}

void socket_close(s32 socket) {
    net_close(socket);
}
