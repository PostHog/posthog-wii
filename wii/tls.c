#include "tls.h"

#include <stdio.h>
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

enum { DIGEST_SIZE = 32, EXTRA_ROOTS_CAP = 16384, FAILURE_CAP = 64 };

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
static const char *failure = "";

/* mbedTLS documents its error codes as negative hexadecimal numbers. */
static void fail(const char *what, int error) {
    static char text[FAILURE_CAP];
    snprintf(text, sizeof text, "%s (error -0x%04X).", what, (unsigned)-error);
    failure = text;
}

static void fail_handshake(int error) {
    /* The flags hold a result only after the certificate check has run. */
    uint32_t flags = error == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED ? mbedtls_ssl_get_verify_result(&session) : 0;
    if (flags & (MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE)) {
        failure = "The host's certificate is not valid on the console's date.\nCheck the date in the Wii settings.";
    } else {
        fail("The secure connection failed", error);
    }
}

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

/* mbedTLS reads and replaces its seed file through the next two functions,
   because of wii/tls_config.h. */

static const char *seed_problem;

static int read_seed(unsigned char *seed, size_t len) {
    if (sdcard_read(SDCARD_SEED_FILE, seed, len) != len) {
        seed_problem = "The SD card has no random seed for a secure connection.\n"
                       "Run \"make sd\" and copy the folder to the card again.";
        return -1;
    }
    return (int)len;
}

static int write_seed(unsigned char *seed, size_t len) {
    if (!sdcard_write(SDCARD_SEED_FILE, seed, len)) {
        seed_problem = "Could not save a new random seed to the SD card.";
        return -1;
    }
    return (int)len;
}

static int send_bytes(void *ctx, const unsigned char *data, size_t len) {
    s32 sent = socket_send(*(s32 *)ctx, data, (s32)len);
    return sent < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : (int)sent;
}

static int receive_bytes(void *ctx, unsigned char *data, size_t len) {
    s32 got = socket_recv(*(s32 *)ctx, data, (s32)len);
    return got < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : (int)got;
}

/* Seeds the random number generator from the seed file and the readings,
   and loads the trusted roots, once. */
static bool prepare(void) {
    static bool prepared;
    if (prepared) {
        return true;
    }
    mbedtls_platform_set_time(utc_now);
    mbedtls_platform_set_nv_seed(read_seed, write_seed);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&random_bits);
    mbedtls_x509_crt_init(&roots);
    mbedtls_ssl_config_init(&config);
    seed_problem = NULL;
    int result = mbedtls_ctr_drbg_seed(&random_bits, mbedtls_entropy_func, &entropy, NULL, 0);
    if (result == 0) {
        result = mbedtls_x509_crt_parse(&roots, (const unsigned char *)BUILT_IN_ROOTS, sizeof BUILT_IN_ROOTS);
    }
    if (result == 0) {
        result = mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                             MBEDTLS_SSL_PRESET_DEFAULT);
    }
    if (result != 0) {
        if (seed_problem) {
            failure = seed_problem;
        } else {
            fail("Could not set up the secure connection", result);
        }
        return false;
    }

    static char extra_roots[EXTRA_ROOTS_CAP];
    size_t extra_len = sdcard_read(SDCARD_ROOTS_FILE, extra_roots, sizeof extra_roots - 1);
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
    int result = mbedtls_ssl_setup(&session, &config);
    if (result == 0) {
        result = mbedtls_ssl_set_hostname(&session, host);
    }
    if (result == 0) {
        mbedtls_ssl_set_bio(&session, &session_socket, send_bytes, receive_bytes, NULL);
        result = mbedtls_ssl_handshake(&session);
    }
    if (result == 0) {
        return true;
    }
    fail_handshake(result);
    mbedtls_ssl_free(&session);
    return false;
}

const char *tls_failure(void) {
    return failure;
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
