#include "upload.h"

#include <string.h>

enum { ANSWER_CAP = 64 };

static void flush(upload *u) {
    if (u->pending_count > 0 && u->status == UPLOAD_HTTP_OK) {
        size_t json_len = events_batch_json(&u->config, u->pending, u->pending_count, u->json, sizeof u->json);
        char answer[ANSWER_CAP] = "";
        u->status = u->post(u->host, "/batch/", "application/json", u->json, json_len, answer, sizeof answer);
        if (u->status == UPLOAD_HTTP_OK && !strstr(answer, UPLOAD_OK_MARK)) {
            u->status = UPLOAD_NOT_CAPTURE;
        }
        if (u->status == UPLOAD_HTTP_OK) {
            u->sent += u->pending_count;
        }
    }
    u->pending_count = 0;
}

/* The fields are set one by one because the struct is too large to build
   as a temporary on the Wii's stack. */
void upload_begin(upload *u, const events_config *config, const char *host, upload_post_fn *post) {
    u->sent = 0;
    u->status = UPLOAD_HTTP_OK;
    u->config = *config;
    u->host = host;
    u->post = post;
    u->pending_count = 0;
}

void upload_add(upload *u, const playlog_session *session) {
    u->pending[u->pending_count++] = *session;
    if (u->pending_count == UPLOAD_SESSIONS_PER_REQUEST) {
        flush(u);
    }
}

bool upload_end(upload *u) {
    flush(u);
    return u->status == UPLOAD_HTTP_OK;
}
