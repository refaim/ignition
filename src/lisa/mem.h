/*
 * mem.h - Lisa 2 memory pools.
 *
 * A pool tracks the blocks allocated in it so they can be freed together.
 * Up to 256 pools exist at once; pool 0 is "DEFAULT", made by mem_init.
 * Each pool holds up to 64 directories of 64 pages of 16 blocks.
 */

#ifndef IGN_LISA_MEM_H
#define IGN_LISA_MEM_H

#include "types.h"

enum {
    MEM_MAX_POOLS = 256,
    MEM_POOL_DIRS = 64,
    MEM_DIR_PAGES = 64,
    MEM_PAGE_BLOCKS = 16
};

/* Clears the pool table and makes the DEFAULT pool. Returns 1. */
int mem_init(void);

/* Makes a pool; `name` is kept to 63 characters. Returns its index, or -1
 * when the table is full or out of memory. */
int mem_pool_create(const char *name);

/* malloc in a pool. NULL when the pool is full or out of memory. */
void *mem_alloc(int pool, u32 size);

/* mem_alloc, then zero filled. */
void *mem_calloc(int pool, u32 size);

/* Frees one block of a pool. Returns 0 when the pool does not hold it. */
int mem_free(int pool, void *block);

/* Whether pool `pool` exists. */
int mem_pool_exists(int pool);

/* Frees every block of a pool and the pool. Returns 1. */
int mem_pool_destroy(int pool);

/* Logs "Out of memory" and exits. */
void mem_out_of_memory(void);

#endif /* IGN_LISA_MEM_H */
