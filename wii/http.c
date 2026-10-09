#include "http.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "socket.h"
#include "tls.h"

/* RESPONSE_CAP holds the status line, every response header and the start of
   the body. A proxy between the console and PostHog adds headers. */
enum { SCHEME_CAP = 6, NAME_CAP = 64, HEADER_CAP = 320, RESPONSE_CAP = 4096, HTTP_PORT = 80, HTTPS_PORT = 443 };

_Static_assert(SCHEME_CAP == 5 + 1 && NAME_CAP == 63 + 1, "the widths in http_post's sscanf fill these buffers");

typedef struct {
    s32 socket;
    bool secure;
} connection;

static const char *failure = "";

static bool write_all(const connection *link, const char *data, size_t len) {
    for (size_t sent = 0; sent < len;) {
        int piece = (int)(len - sent);
        int n = link->secure ? tls_send(data + sent, piece) : socket_send(link->socket, data + sent, piece);
        if (n <= 0) {
            return false;
        }
        sent += (size_t)n;
    }
    return true;
}

/* Reads until the host closes the connection or out is full. Every request
   asks the host to close after its response. */
static void read_response(const connection *link, char out[RESPONSE_CAP]) {
    size_t len = 0;
    while (len < RESPONSE_CAP - 1) {
        int room = (int)(RESPONSE_CAP - 1 - len);
        int n = link->secure ? tls_recv(out + len, room) : socket_recv(link->socket, out + len, room);
        if (n <= 0) {
            break;
        }
        len += (size_t)n;
    }
    out[len] = '\0';
}

long http_post(const char *host, const char *path, const char *content_type, const void *body, size_t body_len,
               char *answer, size_t answer_cap) {
    if (answer_cap > 0) {
        answer[0] = '\0';
    }
    failure = "The host in the settings file must start with http:// or\nhttps://.";
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

    char header[HEADER_CAP];
    int header_len = snprintf(header, sizeof header,
                              "POST %s HTTP/1.1\r\n"
                              "Host: %s:%u\r\n"
                              "Content-Type: %s\r\n"
                              "Content-Length: %u\r\n"
                              "Connection: close\r\n\r\n",
                              path, name, port, content_type, (unsigned)body_len);
    if (header_len >= (int)sizeof header) {
        return 0;
    }

    link.socket = socket_open(name, port);
    if (link.socket < 0) {
        failure = socket_failure();
        return 0;
    }
    long status = 0;
    failure = "The host did not answer.";
    if (link.secure && !tls_open(link.socket, name)) {
        failure = tls_failure();
    } else {
        char response[RESPONSE_CAP] = "";
        if (write_all(&link, header, (size_t)header_len) && write_all(&link, body, body_len)) {
            read_response(&link, response);
        }
        sscanf(response, "HTTP/%*s %ld", &status);
        const char *response_body = strstr(response, "\r\n\r\n");
        if (response_body && answer_cap > 0) {
            snprintf(answer, answer_cap, "%s", response_body + 4);
        }
        if (link.secure) {
            tls_close();
        }
    }
    socket_close(link.socket);
    return status;
}

const char *http_failure(void) {
    return failure;
}
