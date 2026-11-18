# Shared BearSSL crypto primitives used by the Tiny64 account system.
# The full upstream BearSSL tree keeps headers in inc/ and sources in src/.
CPPFLAGS += -Ivendor/bearssl/inc -Ivendor/bearssl/src

C_OBJECTS += account/account.o account/salt_x86.o account/mem.o \
             vendor/bearssl/src/hash/sha2small.o \
             vendor/bearssl/src/mac/hmac.o \
             vendor/bearssl/src/codec/dec32be.o \
             vendor/bearssl/src/codec/enc32be.o
