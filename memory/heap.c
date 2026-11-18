/**
kmalloc() searches for the first free block large enough to satisfy the request. If the block is much larger than necessary, it splits it.
kfree() marks a block as available and merges adjacent free blocks.
krealloc() preserves existing data when moving an allocation, and kcalloc() initializes newly allocated memory to zero.
This is intentionally a simple allocator. It doesn't support concurrent allocations from multiple CPUs, and the fixed 128 KiB arena is only a starting point.
 */
#include "heap.h"
#include <stdint.h>

/*
 * Temporary fixed heap.
 *
 * Tiny64 identity-maps the first 4 MiB.
 * The heap is stored in the kernel's .bss section.
 *
 * The linker ensures the kernel ends below 0x380000,
 * leaving space before the stack at 0x3F0000.
 */
#define HEAP_BYTES (1024u * 1024u)

typedef struct block {
    size_t size;
    struct block *next;
    unsigned free;
    uint64_t reserved;
} block_t;

/* Reserve 1 MiB of memory, with suitable alignment. */
static union {
    long double align;
    unsigned char bytes[HEAP_BYTES];
} arena;

static block_t *first;

static size_t allocation_failures = 0;
static size_t largest_failed_request = 0;

size_t heap_failure_count(void)
{
    return allocation_failures;
}

size_t heap_largest_failed_request(void)
{
    return largest_failed_request;
}

/* Round requested sizes up to a multiple of 16. */
static size_t align_size(size_t n)
{
    if (n > SIZE_MAX - 15u)
        return 0;

    return (n + 15u) & ~(size_t)15u;
}

void heap_init(void)
{
    first = (block_t *)arena.bytes;

    first->size = HEAP_BYTES - sizeof(block_t);
    first->next = 0;
    first->free = 1;

    allocation_failures = 0;
    largest_failed_request = 0;
}

void *kmalloc(size_t size)
{
    if (!first || !size)
        return 0;

    size = align_size(size);

    if (!size)
        return 0;

    for (block_t *b = first; b; b = b->next) {
        if (!b->free || b->size < size)
            continue;

        /* Split a sufficiently large free block. */
        if (b->size >= size + sizeof(block_t) + 16u) {
            block_t *tail =
                (block_t *)((unsigned char *)(b + 1) + size);

            tail->size = b->size - size - sizeof(block_t);
            tail->next = b->next;
            tail->free = 1;

            b->next = tail;
            b->size = size;
        }

        b->free = 0;

        return b + 1;
    }

    allocation_failures++;

    if (size > largest_failed_request)
        largest_failed_request = size;

    return 0;
}

void kfree(void *ptr)
{
    if (!ptr || !first)
        return;

    /* Find the allocation matching this pointer. */
    for (block_t *b = first; b; b = b->next) {
        if ((void *)(b + 1) != ptr)
            continue;

        if (b->free)
            return;

        b->free = 1;

        /* Merge adjacent free blocks. */
        for (block_t *p = first; p && p->next;) {
            if (p->free && p->next->free) {
                p->size += sizeof(block_t) + p->next->size;
                p->next = p->next->next;
            } else {
                p = p->next;
            }
        }

        return;
    }
}

void *kcalloc(size_t count, size_t size)
{
    if (size && count > SIZE_MAX / size)
        return 0;

    size_t bytes = count * size;
    unsigned char *p = kmalloc(bytes);

    if (p) {
        for (size_t i = 0; i < bytes; ++i)
            p[i] = 0;
    }

    return p;
}

void *krealloc(void *ptr, size_t size)
{
    if (!ptr)
        return kmalloc(size);

    if (!size) {
        kfree(ptr);
        return 0;
    }

    for (block_t *b = first; b; b = b->next) {
        if ((void *)(b + 1) != ptr || b->free)
            continue;

        if (size <= b->size)
            return ptr;

        void *newptr = kmalloc(size);

        if (!newptr)
            return 0;

        unsigned char *d = newptr;
        unsigned char *s = ptr;

        for (size_t i = 0; i < b->size; ++i)
            d[i] = s[i];

        kfree(ptr);

        return newptr;
    }

    return 0;
}

int heap_test(void)
{
    unsigned char *a = kmalloc(128);
    unsigned char *b = kmalloc(1024);

    if (!a || !b)
        return 0;

    for (unsigned i = 0; i < 128; ++i)
        a[i] = (unsigned char)i;

    kfree(b);

    unsigned char *c = kmalloc(512);

    /* Verify that freed memory was reused. */
    if (c != b)
        return 0;

    unsigned char *d = krealloc(a, 256);

    if (!d)
        return 0;

    /* Verify that realloc preserved the data. */
    for (unsigned i = 0; i < 128; ++i) {
        if (d[i] != (unsigned char)i)
            return 0;
    }

    kfree(c);
    kfree(d);

    return 1;
}

/*
 * Return the total amount of free heap memory.
 *
 * Does not include allocation headers.
 */
size_t heap_free_bytes(void)
{
    size_t total = 0;

    for (block_t *b = first; b != NULL; b = b->next) {
        if (b->free)
            total += b->size;
    }

    return total;
}

/*
 * Return the largest contiguous free allocation.
 *
 * This helps distinguish heap fragmentation
 * from insufficient total memory.
 */
size_t heap_largest_free_block(void)
{
    size_t largest = 0;

    for (block_t *b = first; b != NULL; b = b->next) {
        if (b->free && b->size > largest)
            largest = b->size;
    }

    return largest;
}
