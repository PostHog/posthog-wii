#include "http.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <network.h>

#include "tls.h"

enum {
    SCHEME_CAP = 6,
    NAME_CAP = 64,
    HEADER_CAP = 320,
    RESPONSE_CAP = 64,
    RETRY_DELAY_US = 100000,
    NETWORK_TRIES = 150,
    HTTP_PORT = 80,
    HTTPS_PORT = 443,
};

typedef struct {
    s32 socket;
    bool secure;
} connection;

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

static bool write_all(const connection *link, const char *data, size_t len) {
    for (size_t sent = 0; sent < len;) {
        int n = link->secure ? tls_send(data + sent, (int)(len - sent))
                             : net_send(link->socket, data + sent, (s32)(len - sent), 0);
        if (n <= 0) {
            return false;
        }
        sent += (size_t)n;
    }
    return true;
}

static int read_some(const connection *link, char *data, int len) {
    return link->secure ? tls_recv(data, len) : net_recv(link->socket, data, len, 0);
}

long http_post(const char *host, const char *path, const char *content_type, const void *body, size_t body_len) {
    char scheme[SCHEME_CAP];
    char name[NAME_CAP];
    unsigned port = 0;
    if (sscanf(host, "%5[a-z]://%63[^:/]:%u", scheme, name, &port) < 2) {
        return 0;
    }
    connection link = {.secure = strcmp(scheme, "https") == 0};
    if (!link.secure && strcmp(scheme, "http") != 0) {
        return 0;
    }
    if (port == 0) {
        port = link.secure ? HTTPS_PORT : HTTP_PORT;
    }

    struct sockaddr_in address = {0};
    address.sin_len = sizeof address;
    address.sin_family = AF_INET;
    address.sin_port = htons((u16)port);
    if (!network_ready() || !resolve(name, &address.sin_addr)) {
        return 0;
    }

    link.socket = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (link.socket < 0) {
        return 0;
    }

    char header[HEADER_CAP];
    int header_len = snprintf(header, sizeof header,
                              "POST %s HTTP/1.1\r\n"
                              "Host: %s:%u\r\n"
                              "Content-Type: %s\r\n"
                              "Content-Length: %u\r\n"
                              "Connection: close\r\n\r\n",
                              path, name, port, content_type, (unsigned)body_len);

    long status = 0;
    char response[RESPONSE_CAP] = {0};
    if (header_len < (int)sizeof header && net_connect(link.socket, (struct sockaddr *)&address, sizeof address) >= 0 &&
        (!link.secure || tls_open(link.socket, name))) {
        if (write_all(&link, header, (size_t)header_len) && write_all(&link, body, body_len) &&
            read_some(&link, response, sizeof response - 1) > 0) {
            sscanf(response, "HTTP/%*s %ld", &status);
        }
        if (link.secure) {
            tls_close();
        }
    }
    net_close(link.socket);
    return status;
}
