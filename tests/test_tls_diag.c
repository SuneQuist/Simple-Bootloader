/* Host-side smoke test for the freestanding BearSSL integration.
 * This is NOT a network handshake or a certificate validation test.
 */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bearssl.h>

static size_t allocated;
static int saw_init, saw_disabled;

void *kmalloc(size_t n)
{
    size_t *p = malloc(n + sizeof *p);
    if (!p) return NULL;
    *p = n;
    allocated += n;
    return p + 1;
}

void kfree(void *p)
{
    if (p) {
        size_t *h = (size_t *)p - 1;
        allocated -= *h;
        free(h);
    }
}

size_t heap_free_bytes(void) { return 1024u * 1024u - allocated; }
void console_print(const char *s)
{
    if (strstr(s, "TLS: engine initialised")) saw_init = 1;
    if (strstr(s, "HTTPS DISABLED")) saw_disabled = 1;
    fputs(s, stdout);
}
void console_putc(char c) { putchar(c); }
void tls_diagnostic(void);

int main(void)
{
    tls_diagnostic();
    assert(saw_init && saw_disabled);
    assert(allocated == 0);

    /* The freestanding build must fail closed without an entropy source. */
    br_ssl_client_context *client = malloc(sizeof *client);
    br_x509_minimal_context *x509 = malloc(sizeof *x509);
    unsigned char *records = malloc(BR_SSL_BUFSIZE_MONO);
    assert(client && x509 && records);
    br_ssl_client_init_full(client, x509, NULL, 0);
    br_ssl_engine_set_buffer(&client->eng, records, BR_SSL_BUFSIZE_MONO, 0);
    assert(br_ssl_client_reset(client, "example.com", 0) == 0);
    assert(br_ssl_engine_last_error(&client->eng) == BR_ERR_NO_RANDOM);
    free(records);
    free(x509);
    free(client);
    puts("PASS: BearSSL initialisation, heap cleanup and fail-closed RNG");
    return 0;
}
