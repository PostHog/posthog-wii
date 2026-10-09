#include "tls.h"

#include <string.h>
#include <time.h>

#include <ogc/lwp_watchdog.h>

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/platform.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

#include "noise.h"
#include "sdcard.h"
#include "socket.h"

enum { DIGEST_SIZE = 32, EXTRA_ROOTS_CAP = 16384 };

/* PEM text must end in a zero byte for mbedTLS to read it. */
static const char BUILT_IN_ROOTS[] = {
#embed "roots.pem"
    , 0};

/* Each suite has forward secrecy and authenticated encryption. */
static const int SUITES[] = {
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
    MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
    MBEDTLS_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
    MBEDTLS_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
    0,
};

static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context random_bits;
static mbedtls_x509_crt roots;
static mbedtls_ssl_config config;
static mbedtls_ssl_context session;
static s32 session_socket;
static int utc_offset_minutes;

void tls_set_utc_offset(int minutes) {
    utc_offset_minutes = minutes;
}

/* The console's clock holds local time, and certificate dates are in UTC. */
static mbedtls_time_t utc_now(mbedtls_time_t *out) {
    mbedtls_time_t now = time(NULL) - (mbedtls_time_t)utc_offset_minutes * 60;
    if (out) {
        *out = now;
    }
    return now;
}

/* mbedTLS calls the next two functions, because of wii/tls_config.h. */

mbedtls_ms_time_t mbedtls_ms_time(void) {
    return (mbedtls_ms_time_t)ticks_to_millisecs(gettime());
}

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen);

/* mbedTLS counts every byte it gets here as fully random. The readings are
   far from that, so many of them are hashed down to one digest. */
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen) {
    (void)data;
    static uint8_t readings[NOISE_CAP];
    uint8_t digest[DIGEST_SIZE];
    size_t count = noise_read(readings, sizeof readings);
    if (mbedtls_sha256(readings, count, digest, 0) != 0) {
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }
    *olen = len < sizeof digest ? len : sizeof digest;
    memcpy(output, digest, *olen);
    return 0;
}

static int send_bytes(void *ctx, const unsigned char *data, size_t len) {
    s32 sent = socket_send(*(s32 *)ctx, data, (s32)len);
    return sent < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : (int)sent;
}

static int receive_bytes(void *ctx, unsigned char *data, size_t len) {
    s32 got = socket_recv(*(s32 *)ctx, data, (s32)len);
    return got < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : (int)got;
}

/* Seeds the random number generator and loads the trusted roots, once. */
static bool prepare(void) {
    static bool prepared;
    if (prepared) {
        return true;
    }
    mbedtls_platform_set_time(utc_now);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&random_bits);
    mbedtls_x509_crt_init(&roots);
    mbedtls_ssl_config_init(&config);
    if (mbedtls_ctr_drbg_seed(&random_bits, mbedtls_entropy_func, &entropy, NULL, 0) != 0 ||
        mbedtls_x509_crt_parse(&roots, (const unsigned char *)BUILT_IN_ROOTS, sizeof BUILT_IN_ROOTS) != 0 ||
        mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        return false;
    }

    static char extra_roots[EXTRA_ROOTS_CAP];
    size_t extra_len = sdcard_read_roots(extra_roots, sizeof extra_roots - 1);
    if (extra_len > 0) {
        extra_roots[extra_len] = '\0';
        /* A certificate that does not parse is left out. The others still load. */
        mbedtls_x509_crt_parse(&roots, (const unsigned char *)extra_roots, extra_len + 1);
    }

    mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&config, &roots, NULL);
    mbedtls_ssl_conf_rng(&config, mbedtls_ctr_drbg_random, &random_bits);
    mbedtls_ssl_conf_ciphersuites(&config, SUITES);
    prepared = true;
    return true;
}

bool tls_open(s32 socket, const char *host) {
    if (!prepare()) {
        return false;
    }
    session_socket = socket;
    mbedtls_ssl_init(&session);
    if (mbedtls_ssl_setup(&session, &config) == 0 && mbedtls_ssl_set_hostname(&session, host) == 0) {
        mbedtls_ssl_set_bio(&session, &session_socket, send_bytes, receive_bytes, NULL);
        if (mbedtls_ssl_handshake(&session) == 0) {
            return true;
        }
    }
    mbedtls_ssl_free(&session);
    return false;
}

int tls_send(const void *data, int len) {
    return mbedtls_ssl_write(&session, data, (size_t)len);
}

int tls_recv(void *data, int len) {
    int got = mbedtls_ssl_read(&session, data, (size_t)len);
    return got == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ? 0 : got;
}

void tls_close(void) {
    mbedtls_ssl_close_notify(&session);
    mbedtls_ssl_free(&session);
}
