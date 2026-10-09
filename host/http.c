#include "http.h"

#include <stdio.h>
#include <string.h>

#include <curl/curl.h>

enum { URL_CAP = 512, HEADER_CAP = 128, TIMEOUT_SECONDS = 30 };

static size_t discard_response(char *data, size_t size, size_t count, void *ctx) {
    (void)data;
    (void)ctx;
    return size * count;
}

long http_post(const char *host, const char *path, const char *content_type, const void *body, size_t body_len) {
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

    struct curl_slist *headers = curl_slist_append(NULL, header);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)body_len);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)TIMEOUT_SECONDS);

    long status = 0;
    if (curl_easy_perform(curl) == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    }

    curl_slist_free_all(headers);
    return status;
}
