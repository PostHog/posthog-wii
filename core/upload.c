#include "upload.h"
#include "events.h"
#include "http_request.h"
#include "playlog.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

enum { ANSWER_CAP = 64 };

static void flush(upload *job) {
    if (job->pending_count > 0 && job->status == UPLOAD_HTTP_OK) {
        size_t json_len =
            events_batch_json(&job->config, job->pending, job->pending_count, job->json, sizeof job->json);
        char answer[ANSWER_CAP] = "";
        const http_request request = {
            .host = job->host,
            .path = "/batch/",
            .content_type = "application/json",
            .body = job->json,
            .body_len = json_len,
        };
        job->status = job->post(&request, answer, sizeof answer);
        if (job->status == UPLOAD_HTTP_OK && !strstr(answer, UPLOAD_OK_MARK)) {
            job->status = UPLOAD_NOT_CAPTURE;
        }
        if (job->status == UPLOAD_HTTP_OK) {
            job->sent += job->pending_count;
        }
    }
    job->pending_count = 0;
}

/* This function sets the fields one by one, because the struct is too large
   to build as a temporary on the Wii's stack. */
void upload_begin(upload *job, const events_config *config, const char *host, http_post_fn *post) {
    job->sent = 0;
    job->status = UPLOAD_HTTP_OK;
    job->config = *config;
    job->host = host;
    job->post = post;
    job->pending_count = 0;
}

void upload_add(upload *job, const playlog_session *session) {
    job->pending[job->pending_count++] = *session;
    if (job->pending_count == UPLOAD_SESSIONS_PER_REQUEST) {
        flush(job);
    }
}

bool upload_end(upload *job) {
    flush(job);
    return job->status == UPLOAD_HTTP_OK;
}
