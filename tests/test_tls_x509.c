#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <bearssl.h>
#include "generated_x509_fixture.h"

static unsigned check(const char *host, uint32_t days, int use_root)
{
    br_x509_trust_anchor root = {
        { (unsigned char *)test_root_dn, sizeof test_root_dn },
        BR_X509_TA_CA,
        { BR_KEYTYPE_RSA, { .rsa = {
            (unsigned char *)test_root_n, sizeof test_root_n,
            (unsigned char *)test_root_e, sizeof test_root_e
        } } }
    };
    br_x509_minimal_context x;
    br_x509_minimal_init(&x, &br_sha256_vtable, use_root ? &root : NULL,
                         use_root ? 1u : 0u);
    br_x509_minimal_set_hash(&x, br_sha256_ID, &br_sha256_vtable);
    br_x509_minimal_set_rsa(&x, &br_rsa_i62_pkcs1_vrfy);
    br_x509_minimal_set_time(&x, days, 12u * 3600u);
    x.vtable->start_chain(&x.vtable, host);
    x.vtable->start_cert(&x.vtable, sizeof test_leaf_der);
    for (size_t off = 0; off < sizeof test_leaf_der; ) {
        size_t n = sizeof test_leaf_der - off;
        if (n > 97u) n = 97u;
        x.vtable->append(&x.vtable, test_leaf_der + off, n);
        off += n;
    }
    x.vtable->end_cert(&x.vtable);
    return x.vtable->end_chain(&x.vtable);
}

int main(void)
{
    assert(check("tiny64-test.invalid", 740303u, 1) == 0);
    assert(check("wrong.invalid", 740303u, 1) == BR_ERR_X509_BAD_SERVER_NAME);
    assert(check("tiny64-test.invalid", 743634u, 1) == BR_ERR_X509_EXPIRED);
    assert(check("tiny64-test.invalid", 740303u, 0) == BR_ERR_X509_NOT_TRUSTED);
    puts("PASS: BearSSL CA chain, hostname, expiration and unknown issuer validation");
    return 0;
}
