/* Changes to mbedTLS's default configuration. The Makefile passes this file
   to mbedTLS as MBEDTLS_USER_CONFIG_FILE. */

/* libogc has no BSD sockets, no POSIX clock and no entropy device, and its
   time() returns local time. tls.c supplies the socket calls, both clocks
   and the entropy source. */
#undef MBEDTLS_NET_C
#undef MBEDTLS_TIMING_C
#define MBEDTLS_PLATFORM_MS_TIME_ALT
#define MBEDTLS_PLATFORM_TIME_ALT
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT

/* mbedTLS itself reads and writes no file. */
#undef MBEDTLS_FS_IO
#undef MBEDTLS_PSA_ITS_FILE_C
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C

/* The app is a TLS 1.2 client over TCP. */
#undef MBEDTLS_SSL_PROTO_TLS1_3
#undef MBEDTLS_SSL_SRV_C
#undef MBEDTLS_SSL_PROTO_DTLS
#undef MBEDTLS_SSL_DTLS_ANTI_REPLAY
#undef MBEDTLS_SSL_DTLS_HELLO_VERIFY
#undef MBEDTLS_SSL_DTLS_CLIENT_PORT_REUSE
#undef MBEDTLS_SSL_RENEGOTIATION
#undef MBEDTLS_SELF_TEST
