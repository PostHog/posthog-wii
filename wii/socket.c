#include "socket.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/unistd.h>

#include <network.h>
#include <ogc/lwp_watchdog.h>

/* NETWORK_WAIT_MS is longer than libogc's own wait for an address. */
enum {
    NAME_CAP = 64,
    FAILURE_CAP = 64,
    RETRY_DELAY_US = 100000,
    CONNECT_DELAY_US = 20000,
    NETWORK_WAIT_MS = 30000,
    SEND_PIECE = 4096,
};

static char failure[FAILURE_CAP];

static void fail(const char *what, s32 error) {
    (void)snprintf(failure, sizeof failure, "%s (error %d).", what, (int)error);
}

static bool past(u64 start, u32 limit_ms) {
    return diff_msec(start, gettime()) > limit_ms;
}

/* net_init never returns on a console with no network device, so the start
   runs in the background and the app stops waiting for it at the limit. The
   start stays in the background after that, because net_deinit waits for it
   to finish with no limit. */
static s32 start_network(u64 start) {
    s32 result = net_init_async(NULL, NULL);
    if (result < 0) {
        return result;
    }
    while ((result = net_get_status()) == -EBUSY) {
        if (past(start, NETWORK_WAIT_MS)) {
            return -ETIMEDOUT;
        }
        usleep(RETRY_DELAY_US);
    }
    return result;
}

static bool network_ready(void) {
    static bool ready;
    u64 start = gettime();
    while (!ready) {
        s32 result = start_network(start);
        if (result == 0) {
            ready = true;
        } else if (result != -EAGAIN || past(start, NETWORK_WAIT_MS)) {
            fail("The console's network did not start", result);
            return false;
        } else {
            usleep(RETRY_DELAY_US);
        }
    }
    return true;
}

/* resolve keeps the last answer, because every request of an upload goes to
   the same host and each lookup is a round trip. */
static bool resolve(const char *name, struct in_addr *address) {
    static char known_name[NAME_CAP];
    static struct in_addr known_address;
    if (known_name[0] == '\0' || strcmp(name, known_name) != 0) {
        struct hostent *entry = NULL;
        if (!inet_aton(name, &known_address)) {
            entry = net_gethostbyname(name);
            if (!entry || entry->h_length != sizeof known_address || !entry->h_addr_list[0]) {
                /* libogc leaves the lookup's error in errno. */
                fail("Could not find the host's address", errno);
                return false;
            }
            memcpy(&known_address, entry->h_addr_list[0], sizeof known_address);
        }
        /* The next request looks up a name too long to keep again. */
        (void)snprintf(known_name, sizeof known_name, "%s", name);
    }
    *address = known_address;
    return true;
}

static bool wait_for(s32 socket, u32 events) {
    struct pollsd poll = {.socket = socket, .events = events};
    return net_poll(&poll, 1, SOCKET_WAIT_MS) > 0;
}

/* A connect that waits inside IOS has no limit, so the socket does not wait
   and this function retries the connect until it succeeds or the limit passes. */
static s32 connect_within_limit(s32 socket, struct sockaddr_in *address) {
    u32 no_wait = 1;
    u32 wait = 0;
    s32 result = net_ioctl(socket, FIONBIO, &no_wait);
    for (u64 start = gettime(); result >= 0;) {
        result = net_connect(socket, (struct sockaddr *)address, sizeof *address);
        if (result >= 0 || result == -EISCONN) {
            /* socket_send relies on a socket that waits. */
            return net_ioctl(socket, FIONBIO, &wait);
        }
        if (result == -EINPROGRESS || result == -EALREADY) {
            if (past(start, SOCKET_WAIT_MS)) {
                return -ETIMEDOUT;
            }
            usleep(CONNECT_DELAY_US);
            result = 0;
        }
    }
    return result;
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
    s32 result = socket < 0 ? socket : connect_within_limit(socket, &address);
    if (result < 0) {
        fail("Could not connect to the host", result);
        if (socket >= 0) {
            net_close(socket);
        }
        return -1;
    }
    return socket;
}

const char *socket_failure(void) {
    return failure;
}

s32 socket_send(s32 socket, const void *data, s32 len) {
    /* net_send returns only after IOS has taken every byte. A ready socket
       shows only that IOS can take some bytes, so each call offers at most
       SEND_PIECE of them. */
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
