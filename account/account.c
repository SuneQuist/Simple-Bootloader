#include "account.h"
#include "vendor/bearssl/bearssl_hmac.h"

enum { ACCOUNT_MAX = 8, NAME_CAP = 32, PASSWORD_MAX = 128,
       SALT_SIZE = 16, HASH_SIZE = 32, ITERATIONS = 600000 };

struct account {
    char name[NAME_CAP];
    unsigned char salt[SALT_SIZE];
    unsigned char hash[HASH_SIZE];
};

static struct account accounts[ACCOUNT_MAX];
static unsigned count, session_uid;

/* Supplied by salt_x86.c in the kernel, stubbed by the host tests. */
int account_random_salt(unsigned char salt[16]);

static void wipe(void *ptr, size_t len) {
    volatile unsigned char *p = ptr;
    while (len--)
        *p++ = 0;
}

static size_t bounded_len(const char *s, size_t limit) {
    size_t n = 0;
    while (n < limit && s[n])
        ++n;
    return n;
}

static const struct account *find_account(const char *name, unsigned *slot) {
    for (unsigned i = 0; i < count; ++i) {
        const char *a = name;
        const char *b = accounts[i].name;
        while (*a && *a == *b) {
            ++a;
            ++b;
        }
        if (*a == *b) {
            *slot = i;
            return &accounts[i];
        }
    }
    return 0;
}

static void password_hash(const char *password, size_t len,
                          const unsigned char salt[SALT_SIZE],
                          unsigned char out[HASH_SIZE]) {
    /* RFC 8018 PBKDF2, specialized to a 32-byte output (one block). */
    static const unsigned char block_index[] = { 0, 0, 0, 1 };
    br_hmac_key_context key;
    br_hmac_context ctx;
    unsigned char u[HASH_SIZE];

    br_hmac_key_init(&key, &br_sha256_vtable, password, len);
    br_hmac_init(&ctx, &key, HASH_SIZE);
    br_hmac_update(&ctx, salt, SALT_SIZE);
    br_hmac_update(&ctx, block_index, sizeof block_index);
    br_hmac_out(&ctx, u);
    for (unsigned j = 0; j < HASH_SIZE; ++j)
        out[j] = u[j];

    for (unsigned i = 1; i < ITERATIONS; ++i) {
        br_hmac_init(&ctx, &key, HASH_SIZE);
        br_hmac_update(&ctx, u, HASH_SIZE);
        br_hmac_out(&ctx, u);
        for (unsigned j = 0; j < HASH_SIZE; ++j)
            out[j] ^= u[j];
    }

    wipe(u, sizeof u);
    wipe(&ctx, sizeof ctx);
    wipe(&key, sizeof key);
}

/* Compare all bytes without stopping at the first mismatch. */
static int hash_equal(const unsigned char *a, const unsigned char *b) {
    volatile unsigned diff = 0;
    for (unsigned i = 0; i < HASH_SIZE; ++i)
        diff |= (unsigned)(a[i] ^ b[i]);
    return diff == 0;
}

void account_init(void) {
    /* The raw loader does not clear .bss. Unused slots are not read. */
    count = 0;
    session_uid = ACCOUNT_NONE;
}

unsigned create_account(const char *name, const char *password) {
    if (!name || !password || count == ACCOUNT_MAX)
        return ACCOUNT_NONE;

    size_t n = bounded_len(name, NAME_CAP);
    size_t p = bounded_len(password, PASSWORD_MAX + 1);
    if (!n || n == NAME_CAP || !p || p > PASSWORD_MAX)
        return ACCOUNT_NONE;
    if (n == 4 && name[0] == 'r' && name[1] == 'o' &&
        name[2] == 'o' && name[3] == 't')
        return ACCOUNT_NONE;

    unsigned slot;
    if (find_account(name, &slot))
        return ACCOUNT_NONE;

    struct account *entry = &accounts[count];
    if (!account_random_salt(entry->salt))
        return ACCOUNT_NONE;

    password_hash(password, p, entry->salt, entry->hash);
    for (size_t i = 0; i <= n; ++i)
        entry->name[i] = name[i];

    ++count;
    return count; /* UID 1 for the first account. Doesn't log it in. */
}

void login(const char *name, const char *password) {
    unsigned slot;
    session_uid = ACCOUNT_NONE;
    if (!name || !password || bounded_len(name, NAME_CAP) == NAME_CAP)
        return;
    size_t len = bounded_len(password, PASSWORD_MAX + 1);
    if (!len || len > PASSWORD_MAX)
        return;

    const struct account *entry = find_account(name, &slot);
    if (!entry)
        return;

    unsigned char computed[HASH_SIZE];
    password_hash(password, len, entry->salt, computed);
    if (hash_equal(computed, entry->hash))
        session_uid = slot + 1;
    wipe(computed, sizeof computed);
}

unsigned account_uid(void) {
    return session_uid;
}

void account_logout(void) {
    session_uid = ACCOUNT_NONE;
}

