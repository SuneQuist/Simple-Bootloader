#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>

void heap_init(void);

void *kmalloc(size_t size);
void kfree(void *ptr);
void *kcalloc(size_t count, size_t size);
void *krealloc(void *ptr, size_t size);

int heap_test(void);
size_t heap_failure_count(void);
size_t heap_largest_failed_request(void);

/*
 * Return the total number of free payload bytes.
 */
size_t heap_free_bytes(void);

/*
 * Return the size of the largest free block.
 */
size_t heap_largest_free_block(void);

#endif
