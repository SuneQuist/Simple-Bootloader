CPPFLAGS += -Iaccount/vendor/bearssl

C_OBJECTS += account/account.o account/salt_x86.o account/mem.o \
             account/vendor/bearssl/sha2small.o \
             account/vendor/bearssl/hmac.o \
             account/vendor/bearssl/dec32be.o \
             account/vendor/bearssl/enc32be.o
