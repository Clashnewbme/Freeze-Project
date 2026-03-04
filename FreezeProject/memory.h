#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <stddef.h>

extern uint32_t total_memory;
extern uint32_t used_memory;
extern uint32_t free_memory;

void memory_init(uint32_t mem_size);

void* kmalloc(size_t size);
void* kmalloc_aligned(size_t size, uint32_t alignment);
void  kfree(void* ptr);

uint32_t memory_get_used(void);
uint32_t memory_get_free(void);

#endif
