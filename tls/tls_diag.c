/* Phase 10.1: BearSSL integration smoke test, not an HTTPS implementation.
 * No TLS handshake is permitted here: Tiny64 still needs trusted roots,
 * certificate-validity time, and a cryptographically secure entropy source.
 */
#include <stddef.h>
#include <stdint.h>
#include <bearssl.h>
#include "../console/console.h"
#include "../memory/heap.h"

static void print_size(size_t value)
{
    char digits[24];
    unsigned n = 0;
    do {
        digits[n++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value && n < sizeof digits);
    while (n)
        console_putc(digits[--n]);
}

static void print_stat(const char *label, size_t value)
{
    console_print(label);
    print_size(value);
    console_print(" bytes\n");
}

void tls_diagnostic(void)
{
    /* Keep the contexts and record buffer on the heap, not the kernel stack. */
    br_ssl_client_context *client = NULL;
    br_x509_minimal_context *x509 = NULL;
    unsigned char *records = NULL;
    size_t before = heap_free_bytes();
    size_t during;

    console_print("TLS 1.2: BearSSL client integration check\n");
    print_stat("TLS client context: ", sizeof(br_ssl_client_context));
    print_stat("X.509 validator context: ", sizeof(br_x509_minimal_context));
    print_stat("Half-duplex record buffer: ", BR_SSL_BUFSIZE_MONO);
    print_stat("Heap available before: ", before);

    client = kmalloc(sizeof *client);
    x509 = kmalloc(sizeof *x509);
    records = kmalloc(BR_SSL_BUFSIZE_MONO);
    if (!client || !x509 || !records) {
        console_print("TLS: insufficient heap for client contexts\n");
        goto cleanup;
    }

    /* This initialises BearSSL's client, crypto algorithms, and X.509 engine.
     * A NULL trust-anchor list is ONLY suitable for this no-network test.
     */
    br_ssl_client_init_full(client, x509, NULL, 0);
    br_ssl_engine_set_versions(&client->eng, BR_TLS12, BR_TLS12);
    br_ssl_engine_set_buffer(&client->eng, records, BR_SSL_BUFSIZE_MONO, 0);

    /* No legacy TLS suites will be negotiated in the eventual client. */
    static const uint16_t suites[] = {
        BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
        BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256
    };
    br_ssl_engine_set_suites(&client->eng, suites,
                             sizeof suites / sizeof suites[0]);

    during = heap_free_bytes();
    print_stat("Heap used by TLS test: ", before - during);
    console_print("TLS: engine initialised (TLS 1.2, ECDHE + AES-GCM)\n");
    console_print("TLS: HTTPS DISABLED (no trust roots, clock or secure RNG)\n");

cleanup:
    kfree(records);
    kfree(x509);
    kfree(client);
    print_stat("Heap available after: ", heap_free_bytes());
}
