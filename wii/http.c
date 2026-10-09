#include "http.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <network.h>

#include "send.h"

/* Plain HTTP over libogc sockets. PostHog Cloud needs TLS, which this
   sender does not have. */

enum { NAME_CAP = 64, HEADER_CAP = 320, RESPONSE_CAP = 64, RETRY_DELAY_US = 100000 };

static bool network_ready(void) {
    static bool ready;
    while (!ready) {
        s32 result = net_init();
        if (result == -EAGAIN) {
            usleep(RETRY_DELAY_US);
            continue;
        }
        if (result < 0) {
            return false;
        }
        ready = true;
    }
    return true;
}

static bool resolve(const char *name, struct in_addr *address) {
    if (inet_aton(name, address)) {
        return true;
    }
    struct hostent *entry = net_gethostbyname(name);
    if (!entry || entry->h_length != sizeof *address || !entry->h_addr_list[0]) {
        return false;
    }
    memcpy(address, entry->h_addr_list[0], sizeof *address);
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

long send_batch(const char *host, const char *json, size_t json_len) {
    return http_post(host, "/batch/", "application/json", json, json_len);
}
