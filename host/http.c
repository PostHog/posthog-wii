#include "http.h"

#include <stdio.h>
#include <string.h>

#include <curl/curl.h>

enum { URL_CAP = 512, HEADER_CAP = 128, TIMEOUT_SECONDS = 30 };

typedef struct {
    char *text;
    size_t cap;
    size_t len;
} answer_buffer;

static size_t keep_start(char *data, size_t size, size_t count, void *ctx) {
    answer_buffer *answer = ctx;
    size_t total = size * count;
    for (size_t i = 0; i < total && answer->len + 1 < answer->cap; i++) {
        answer->text[answer->len++] = data[i];
    }
    return total;
}

long http_post(const char *host, const char *path, const char *content_type, const void *body, size_t body_len,
               char *answer, size_t answer_cap) {
    /* One handle for the whole run lets libcurl reuse the connection. */
    static CURL *curl;
    if (!curl && !(curl = curl_easy_init())) {
        return 0;
    }

    size_t host_len = strlen(host);
    while (host_len > 0 && host[host_len - 1] == '/') {
        host_len--;
    }
    char url[URL_CAP];
    snprintf(url, sizeof url, "%.*s%s", (int)host_len, host, path);
    char header[HEADER_CAP];
    snprintf(header, sizeof header, "Content-Type: %s", content_type);

    answer_buffer kept = {.text = answer, .cap = answer_cap};
    struct curl_slist *headers = curl_slist_append(NULL, header);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body_len);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, keep_start);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &kept);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)TIMEOUT_SECONDS);

    long status = 0;
    if (curl_easy_perform(curl) == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    }
    if (answer_cap > 0) {
        answer[kept.len] = '\0';
    }

    curl_slist_free_all(headers);
    return status;
}
