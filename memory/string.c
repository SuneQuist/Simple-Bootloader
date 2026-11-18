#include <stddef.h>
#include "string.h"

void *memmove(void *dest, const void *src, size_t n)
{
    unsigned char *d = dest;
    const unsigned char *s = src;

    if (d == s || n == 0)
        return dest;

    if (d < s || d >= s + n) {
        for (size_t i = 0; i < n; i++)
            d[i] = s[i];
    } else {
        while (n != 0) {
            n--;
            d[n] = s[n];
        }
    }

    return dest;
}

void *memchr(const void *ptr, int value, size_t n)
{
    const unsigned char *p = ptr;

    for (size_t i = 0; i < n; i++) {
        if (p[i] == (unsigned char)value)
            return (void *)(p + i);
    }

    return NULL;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a;
    const unsigned char *y = b;

    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i])
            return (int)x[i] - (int)y[i];
    }

    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;

    while (s[n] != '\0')
        n++;

    return n;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char x = (unsigned char)a[i];
        unsigned char y = (unsigned char)b[i];

        if (x != y)
            return (int)x - (int)y;

        if (x == '\0')
            return 0;
    }

    return 0;
}
