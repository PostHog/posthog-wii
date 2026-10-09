#include "send.h"

#include <stdio.h>
#include <string.h>

#include <curl/curl.h>

enum { URL_CAP = 512, TIMEOUT_SECONDS = 30 };

static size_t discard_response(char *data, size_t size, size_t count, void *ctx) {
    (void)data;
    (void)ctx;
    return size * count;
}

long send_batch(const char *host, const char *json, size_t json_len) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        return 0;
    }

    size_t host_len = strlen(host);
    while (host_len > 0 && host[host_len - 1] == '/') {
        host_len--;
    }
    char url[URL_CAP];
    snprintf(url, sizeof url, "%.*s/batch/", (int)host_len, host);

    struct curl_slist *headers = curl_slist_append(NULL, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)json_len);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)TIMEOUT_SECONDS);

    long status = 0;
    if (curl_easy_perform(curl) == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return status;
}
