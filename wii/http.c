#include "http.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http_request.h"
#include "socket.h"
#include "tls.h"

/* RESPONSE_CAP holds the status line, every response header and the start of
   the body. A proxy between the console and PostHog adds headers. */
enum {
    NAME_CAP = 64,
    HEADER_CAP = 320,
    RESPONSE_CAP = 4096,
    HTTP_PORT = 80,
    HTTPS_PORT = 443,
    PORT_MAX = 65535,
    DECIMAL = 10,
};

typedef struct {
    bool secure;
    char name[NAME_CAP];
    unsigned port;
} endpoint;

typedef struct {
    s32 socket;
    bool secure;
} connection;

static const char *failure = "";

/* Returns the rest of text after prefix, or NULL when text does not start with it. */
static const char *after_prefix(const char *text, const char *prefix) {
    size_t len = strlen(prefix);
    return strncmp(text, prefix, len) == 0 ? text + len : NULL;
}

/* Reads "scheme://name[:port][/...]". Returns false for any other shape, a
   name that does not fit or a port outside 1 to 65535. */
static bool parse_host(const char *host, endpoint *out) {
    const char *rest = after_prefix(host, "https://");
    out->secure = rest != NULL;
    if (!out->secure) {
        rest = after_prefix(host, "http://");
        if (!rest) {
            return false;
        }
    }
    size_t name_len = strcspn(rest, ":/");
    if (name_len == 0 || name_len >= NAME_CAP) {
        return false;
    }
    memcpy(out->name, rest, name_len);
    out->name[name_len] = '\0';
    out->port = out->secure ? HTTPS_PORT : HTTP_PORT;
    if (rest[name_len] != ':') {
        return true;
    }
    const char *digits = rest + name_len + 1;
    if (*digits < '0' || *digits > '9') {
        return false;
    }
    char *end = NULL;
    unsigned long port = strtoul(digits, &end, DECIMAL);
    if (port == 0 || port > PORT_MAX || (*end != '\0' && *end != '/')) {
        return false;
    }
    out->port = (unsigned)port;
    return true;
}

/* Returns the code in a status line such as "HTTP/1.1 200 OK", or 0. */
static long status_code(const char *response) {
    const char *version = after_prefix(response, "HTTP/");
    if (!version) {
        return 0;
    }
    const char *code = version + strcspn(version, " \r\n");
    if (*code != ' ') {
        return 0;
    }
    char *end = NULL;
    long status = strtol(code + 1, &end, DECIMAL);
    return end == code + 1 ? 0 : status;
}

static bool write_all(const connection *link, const char *data, size_t len) {
    for (size_t sent = 0; sent < len;) {
        int piece = (int)(len - sent);
        int moved = link->secure ? tls_send(data + sent, piece) : socket_send(link->socket, data + sent, piece);
        if (moved <= 0) {
            return false;
        }
        sent += (size_t)moved;
    }
    return true;
}

/* Reads until the host closes the connection or out is full. Every request
   asks the host to close after its response. */
static void read_response(const connection *link, char out[RESPONSE_CAP]) {
    size_t len = 0;
    while (len < RESPONSE_CAP - 1) {
        int room = (int)(RESPONSE_CAP - 1 - len);
        int moved = link->secure ? tls_recv(out + len, room) : socket_recv(link->socket, out + len, room);
        if (moved <= 0) {
            break;
        }
        len += (size_t)moved;
    }
    out[len] = '\0';
}

long http_post(const http_request *request, char *answer, size_t answer_cap) {
    if (answer_cap > 0) {
        answer[0] = '\0';
    }
    failure = "The host in the settings file must start with http:// or\nhttps://.";
    endpoint target;
    if (!parse_host(request->host, &target)) {
        return 0;
    }
    connection link = {.secure = target.secure};

    char header[HEADER_CAP];
    int header_len =
        snprintf(header, sizeof header,
                 "POST %s HTTP/1.1\r\n"
                 "Host: %s:%u\r\n"
                 "Content-Type: %s\r\n"
                 "Content-Length: %u\r\n"
                 "Connection: close\r\n\r\n",
                 request->path, target.name, target.port, request->content_type, (unsigned)request->body_len);
    if (header_len < 0 || header_len >= (int)sizeof header) {
        return 0;
    }

    link.socket = socket_open(target.name, target.port);
    if (link.socket < 0) {
        failure = socket_failure();
        return 0;
    }
    long status = 0;
    failure = "The host did not answer.";
    if (link.secure && !tls_open(link.socket, target.name)) {
        failure = tls_failure();
    } else {
        char response[RESPONSE_CAP] = "";
        if (write_all(&link, header, (size_t)header_len) && write_all(&link, request->body, request->body_len)) {
            read_response(&link, response);
        }
        status = status_code(response);
        const char *response_body = strstr(response, "\r\n\r\n");
        if (response_body && answer_cap > 0) {
            (void)snprintf(answer, answer_cap, "%s", response_body + 4);
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
