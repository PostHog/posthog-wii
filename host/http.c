#include "http.h"
#include "http_request.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <curl/curl.h>

enum { URL_CAP = 512, HEADER_CAP = 128, TIMEOUT_SECONDS = 30 };

typedef struct {
    char *text;
    size_t cap;
    size_t len;
} answer_buffer;

static size_t keep_start(const char *data, size_t size, size_t count, void *ctx) {
    answer_buffer *answer = ctx;
    size_t total = size * count;
    for (size_t i = 0; i < total && answer->len + 1 < answer->cap; i++) {
        answer->text[answer->len++] = data[i];
    }
    return total;
}

long http_post(const http_request *request, char *answer, size_t answer_cap) {
    /* One handle for the whole run lets libcurl reuse the connection. */
    static CURL *curl;
    if (!curl) {
        curl = curl_easy_init();
    }
    if (!curl) {
        return 0;
    }

    size_t host_len = strlen(request->host);
    while (host_len > 0 && request->host[host_len - 1] == '/') {
        host_len--;
    }
    char url[URL_CAP];
    (void)snprintf(url, sizeof url, "%.*s%s", (int)host_len, request->host, request->path);
    char header[HEADER_CAP];
    (void)snprintf(header, sizeof header, "Content-Type: %s", request->content_type);

    answer_buffer kept = {.text = answer, .cap = answer_cap};
    struct curl_slist *headers = curl_slist_append(NULL, header);
    long status = 0;
    if (headers && curl_easy_setopt(curl, CURLOPT_URL, url) == CURLE_OK &&
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers) == CURLE_OK &&
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request->body) == CURLE_OK &&
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)request->body_len) == CURLE_OK &&
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, keep_start) == CURLE_OK &&
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &kept) == CURLE_OK &&
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)TIMEOUT_SECONDS) == CURLE_OK &&
        curl_easy_perform(curl) == CURLE_OK && curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status) != CURLE_OK) {
        status = 0;
    }
    if (answer_cap > 0) {
        answer[kept.len] = '\0';
    }

    curl_slist_free_all(headers);
    return status;
}
