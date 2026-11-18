#ifndef STRING_H
#define STRING_H

void *memmove(void *dest, const void *src, size_t n);
void *memchr(const void *ptr, int value, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strncmp(const char *a, const char *b, size_t n);

#endif