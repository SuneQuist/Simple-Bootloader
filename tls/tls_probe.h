#ifndef TINY64_TLS_PROBE_H
#define TINY64_TLS_PROBE_H
#include <stddef.h>
#include <stdint.h>
/* Terminal diagnostic: verified TLS 1.2 + encrypted GET / */
int tls_probe_start(const char *host, uint16_t port);
void tls_probe_poll(void);  /* Polls the diagnostic or browser HTTPS request. */
int tls_probe_active(void); /* Diagnostic mode only. */
void tls_probe_cancel(void);

/* Single-flight, nonblocking, certificate-verified HTTPS browser client.
 * The caller owns `out` until success, failure or cancellation.
 * poll: 0=busy, 1=HTTP response complete, -1=error. */
int tls_https_start(const char *host, uint16_t port, const char *path,
                    char *out, size_t capacity);
int tls_https_poll(void);
void tls_https_cancel(void);
int tls_https_active(void);
size_t tls_https_length(void);
int tls_https_status(void);
const char *tls_https_location(void);
const char *tls_https_error(void);
#endif
