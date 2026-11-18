#ifndef TINY64_TLS_PROBE_TRANSPORT_H
#define TINY64_TLS_PROBE_TRANSPORT_H
#include <stddef.h>
#include <stdint.h>
/* Diagnostic-only, single-connection nonblocking TCP transport. */
int tls_transport_start(const char *host, uint16_t port);
int tls_transport_poll(void);             /* 0 connecting, 1 ready, 2 clean TCP FIN, -1 failed */
int tls_transport_send(const uint8_t *data, size_t len); /* 0 busy, >0 accepted, -1 failed */
size_t tls_transport_read(uint8_t *out, size_t cap);
const char *tls_transport_error(void);
int tls_transport_active(void);
void tls_transport_close(void);
#endif
