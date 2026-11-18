# Phase 10.1: selected BearSSL client + X.509 dependencies.
# Do not compile all 294 BearSSL sources (server/keygen/alternative profiles).
# TLS is used by tlsdiag and the nonblocking tlstest HTTPS GET probe.
# Browser HTTPS is not yet enabled.
# Keep the existing account-system crypto primitives in account/account.mk.
TLS_SOURCES := \
    vendor/bearssl/src/aead/ccm.c \
    vendor/bearssl/src/codec/ccopy.c \
    vendor/bearssl/src/codec/dec32le.c \
    vendor/bearssl/src/codec/dec64be.c \
    vendor/bearssl/src/codec/enc32le.c \
    vendor/bearssl/src/codec/enc64be.c \
    vendor/bearssl/src/ec/ec_all_m31.c \
    vendor/bearssl/src/ec/ec_c25519_m64.c \
    vendor/bearssl/src/ec/ec_p256_m64.c \
    vendor/bearssl/src/ec/ec_prime_i31.c \
    vendor/bearssl/src/ec/ec_secp256r1.c \
    vendor/bearssl/src/ec/ec_secp384r1.c \
    vendor/bearssl/src/ec/ec_secp521r1.c \
    vendor/bearssl/src/ec/ecdsa_atr.c \
    vendor/bearssl/src/ec/ecdsa_i31_bits.c \
    vendor/bearssl/src/ec/ecdsa_i31_vrfy_asn1.c \
    vendor/bearssl/src/ec/ecdsa_i31_vrfy_raw.c \
    vendor/bearssl/src/hash/ghash_ctmul64.c \
    vendor/bearssl/src/hash/md5.c \
    vendor/bearssl/src/hash/multihash.c \
    vendor/bearssl/src/hash/sha1.c \
    vendor/bearssl/src/hash/sha2big.c \
    vendor/bearssl/src/int/i31_add.c \
    vendor/bearssl/src/int/i31_bitlen.c \
    vendor/bearssl/src/int/i31_decmod.c \
    vendor/bearssl/src/int/i31_decode.c \
    vendor/bearssl/src/int/i31_encode.c \
    vendor/bearssl/src/int/i31_fmont.c \
    vendor/bearssl/src/int/i31_iszero.c \
    vendor/bearssl/src/int/i31_modpow.c \
    vendor/bearssl/src/int/i31_montmul.c \
    vendor/bearssl/src/int/i31_muladd.c \
    vendor/bearssl/src/int/i31_ninv31.c \
    vendor/bearssl/src/int/i31_rshift.c \
    vendor/bearssl/src/int/i31_sub.c \
    vendor/bearssl/src/int/i31_tmont.c \
    vendor/bearssl/src/int/i32_div32.c \
    vendor/bearssl/src/int/i62_modpow2.c \
    vendor/bearssl/src/mac/hmac_ct.c \
    vendor/bearssl/src/rand/hmac_drbg.c \
    vendor/bearssl/src/rand/sysrng.c \
    vendor/bearssl/src/rsa/rsa_default_pkcs1_vrfy.c \
    vendor/bearssl/src/rsa/rsa_default_pub.c \
    vendor/bearssl/src/rsa/rsa_i62_pkcs1_vrfy.c \
    vendor/bearssl/src/rsa/rsa_i62_pub.c \
    vendor/bearssl/src/rsa/rsa_pkcs1_sig_unpad.c \
    vendor/bearssl/src/ssl/prf.c \
    vendor/bearssl/src/ssl/prf_md5sha1.c \
    vendor/bearssl/src/ssl/prf_sha256.c \
    vendor/bearssl/src/ssl/prf_sha384.c \
    vendor/bearssl/src/ssl/ssl_client.c \
    vendor/bearssl/src/ssl/ssl_client_default_rsapub.c \
    vendor/bearssl/src/ssl/ssl_client_full.c \
    vendor/bearssl/src/ssl/ssl_engine.c \
    vendor/bearssl/src/ssl/ssl_engine_default_aescbc.c \
    vendor/bearssl/src/ssl/ssl_engine_default_aesccm.c \
    vendor/bearssl/src/ssl/ssl_engine_default_aesgcm.c \
    vendor/bearssl/src/ssl/ssl_engine_default_chapol.c \
    vendor/bearssl/src/ssl/ssl_engine_default_descbc.c \
    vendor/bearssl/src/ssl/ssl_engine_default_ecdsa.c \
    vendor/bearssl/src/ssl/ssl_engine_default_rsavrfy.c \
    vendor/bearssl/src/ssl/ssl_hs_client.c \
    vendor/bearssl/src/ssl/ssl_rec_cbc.c \
    vendor/bearssl/src/ssl/ssl_rec_ccm.c \
    vendor/bearssl/src/ssl/ssl_rec_chapol.c \
    vendor/bearssl/src/ssl/ssl_rec_gcm.c \
    vendor/bearssl/src/symcipher/aes_ct64.c \
    vendor/bearssl/src/symcipher/aes_ct64_cbcdec.c \
    vendor/bearssl/src/symcipher/aes_ct64_cbcenc.c \
    vendor/bearssl/src/symcipher/aes_ct64_ctr.c \
    vendor/bearssl/src/symcipher/aes_ct64_ctrcbc.c \
    vendor/bearssl/src/symcipher/aes_ct64_dec.c \
    vendor/bearssl/src/symcipher/aes_ct64_enc.c \
    vendor/bearssl/src/symcipher/chacha20_ct.c \
    vendor/bearssl/src/symcipher/des_ct.c \
    vendor/bearssl/src/symcipher/des_ct_cbcdec.c \
    vendor/bearssl/src/symcipher/des_ct_cbcenc.c \
    vendor/bearssl/src/symcipher/des_support.c \
    vendor/bearssl/src/symcipher/poly1305_ctmul.c \
    vendor/bearssl/src/symcipher/poly1305_ctmulq.c \
    vendor/bearssl/src/x509/x509_minimal.c

C_OBJECTS += tls/tls_diag.o tls/tls_probe.o tls/tls_clock.o tls/trust_anchors.o tls/https_preview.o $(TLS_SOURCES:.c=.o)

# Host OS entropy, clock and SIMD are not available in Tiny64.
# In particular, never accidentally link Linux /dev/urandom or getentropy.
TLS_CFLAGS = $(CFLAGS) \
    -DBR_USE_GETENTROPY=0 -DBR_USE_URANDOM=0 \
    -DBR_USE_UNIX_TIME=0 -DBR_USE_WIN32_TIME=0 \
    -DBR_USE_WIN32_RAND=0 -DBR_RDRAND=0 \
    -DBR_AES_X86NI=0 -DBR_SSE2=0

$(BUILD_DIR)/vendor/bearssl/src/%.o: vendor/bearssl/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(TLS_CFLAGS) -MMD -MP -c $< -o $@
