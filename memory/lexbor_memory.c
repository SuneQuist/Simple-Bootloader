
#include <stddef.h>

#include "heap.h"
#include "lexbor_memory.h"

/*
 * Tiny64 allocator implementations used directly
 * by Lexbor.
 */

void *lexbor_malloc(size_t size)
{
    return kmalloc(size);
}

void *lexbor_realloc(void *ptr, size_t size)
{
    return krealloc(ptr, size);
}

void *lexbor_calloc(size_t count, size_t size)
{
    return kcalloc(count, size);
}

void *lexbor_free(void *ptr)
{
    kfree(ptr);
    return NULL;
}

void lexbor_init(void)
{
    /*
     * Nothing to register.
     * Lexbor calls the functions above directly.
     */
}
