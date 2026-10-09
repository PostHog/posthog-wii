#include "http.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <network.h>

/* Plain HTTP over libogc sockets. PostHog Cloud needs TLS, which this
   file does not have. */

enum { NAME_CAP = 64, HEADER_CAP = 320, RESPONSE_CAP = 64, RETRY_DELAY_US = 100000, NETWORK_TRIES = 150 };

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

static bool write_all(s32 socket, const char *data, size_t len) {
    for (size_t sent = 0; sent < len;) {
        s32 n = net_send(socket, data + sent, (s32)(len - sent), 0);
        if (n <= 0) {
            return false;
        }
        sent += (size_t)n;
    }
    return true;
}

long http_post(const char *host, const char *path, const char *content_type, const void *body, size_t body_len) {
    char name[NAME_CAP];
    unsigned port = 80;
    if (sscanf(host, "http://%63[^:/]:%u", name, &port) < 1 || !network_ready()) {
        return 0;
    }

    struct sockaddr_in address = {0};
    address.sin_len = sizeof address;
    address.sin_family = AF_INET;
    address.sin_port = htons((u16)port);
    if (!resolve(name, &address.sin_addr)) {
        return 0;
    }

    s32 socket = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (socket < 0) {
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
    if (header_len < (int)sizeof header && net_connect(socket, (struct sockaddr *)&address, sizeof address) >= 0 &&
        write_all(socket, header, (size_t)header_len) && write_all(socket, body, body_len) &&
        net_recv(socket, response, sizeof response - 1, 0) > 0) {
        sscanf(response, "HTTP/%*s %ld", &status);
    }
    net_close(socket);
    return status;
}
