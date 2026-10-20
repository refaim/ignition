/*
 * mem.c - Lisa 2 memory pools.
 *
 * Layout, as the original allocates it:
 *   pool  0x140 bytes: name[64], then 64 directory pointers
 *   dir   0x100 bytes: 64 page pointers
 *   page  0x80 bytes:  16 blocks of { ptr, size }
 * A block with size 0 is free. Directories and pages are never freed
 * before the pool is destroyed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lisa/log.h"
#include "lisa/mem.h"

struct mem_block {
    void *ptr;
    u32 size;
};

struct mem_page {
    struct mem_block blocks[MEM_PAGE_BLOCKS];
};

struct mem_dir {
    struct mem_page *pages[MEM_DIR_PAGES];
};

struct mem_pool {
    char name[64];
    struct mem_dir *dirs[MEM_POOL_DIRS];
};

static struct mem_pool *s_pools[MEM_MAX_POOLS];    /* 0x63c6a0 */

/* 0x45ad50 */
int mem_init(void)
{
    memset(s_pools, 0, sizeof(s_pools));
    mem_pool_create("DEFAULT");
    return 1;
}

/* 0x45ad80 */
int mem_pool_create(const char *name)
{
    struct mem_pool *p;
    int i, k;

    for (i = 0; i < MEM_MAX_POOLS && s_pools[i] != NULL; i++) {
    }
    if (i == MEM_MAX_POOLS) {
        return -1;
    }
    p = malloc(sizeof(*p));
    if (p == NULL) {
        return -1;
    }
    s_pools[i] = p;
    k = 0;
    if (name != NULL) {
        for (; name[k] != '\0' && k < 63; k++) {
            p->name[k] = name[k];
        }
    }
    p->name[k] = '\0';
    memset(p->dirs, 0, sizeof(p->dirs));
    return i;
}

static void *page_take(struct mem_page *page, int slot, u32 size)
{
    void *ptr = malloc(size);

    if (ptr == NULL) {
        return NULL;
    }
    page->blocks[slot].ptr = ptr;
    page->blocks[slot].size = size;
    return ptr;
}

/* 0x45ae10
 * Takes the first free block of the first page that has one. Without
 * one, adds a page to the first directory with room, then a directory.
 * The original reads one entry past each array when it is full; the
 * result is the same. */
void *mem_alloc(int pool, u32 size)
{
    struct mem_pool *p = s_pools[pool];
    int d, g, b;

    for (d = 0; d < MEM_POOL_DIRS && p->dirs[d] != NULL; d++) {
        struct mem_dir *dir = p->dirs[d];

        for (g = 0; g < MEM_DIR_PAGES && dir->pages[g] != NULL; g++) {
            struct mem_page *page = dir->pages[g];

            for (b = 0; b < MEM_PAGE_BLOCKS && page->blocks[b].size != 0; b++) {
            }
            if (b != MEM_PAGE_BLOCKS) {
                return page_take(page, b, size);
            }
        }
        if (g != MEM_DIR_PAGES) {
            /* A new page: the sizes of blocks 1-15 are cleared, block 0
             * is taken. */
            struct mem_page *page = malloc(sizeof(*page));

            if (page == NULL) {
                return NULL;
            }
            dir->pages[g] = page;
            for (b = 1; b < MEM_PAGE_BLOCKS; b++) {
                page->blocks[b].size = 0;
            }
            page->blocks[0].size = 0;
            return page_take(page, 0, size);
        }
    }
    if (d != MEM_POOL_DIRS) {
        struct mem_dir *dir = malloc(sizeof(*dir));
        struct mem_page *page;

        if (dir == NULL) {
            return NULL;
        }
        p->dirs[d] = dir;
        memset(dir, 0, sizeof(*dir));
        page = malloc(sizeof(*page));
        if (page == NULL) {
            return NULL;
        }
        dir->pages[0] = page;
        for (b = 0; b < MEM_PAGE_BLOCKS; b++) {
            page->blocks[b].size = 0;
        }
        return page_take(page, 0, size);
    }
    return NULL;
}

/* 0x45afb0 */
void *mem_calloc(int pool, u32 size)
{
    void *ptr = mem_alloc(pool, size);

    if (ptr != NULL) {
        memset(ptr, 0, size);
    }
    return ptr;
}

/* 0x45b000
 * The original matches the pointer in free blocks too, so freeing a
 * pointer that malloc handed out again could clear a stale entry and
 * leave the live one to be freed twice by mem_pool_destroy. Here only
 * live blocks match. */
int mem_free(int pool, void *block)
{
    struct mem_pool *p = s_pools[pool];
    int d, g, b;

    for (d = 0; d < MEM_POOL_DIRS; d++) {
        struct mem_dir *dir = p->dirs[d];

        if (dir == NULL) {
            continue;
        }
        for (g = 0; g < MEM_DIR_PAGES; g++) {
            struct mem_page *page = dir->pages[g];

            if (page == NULL) {
                continue;
            }
            for (b = 0; b < MEM_PAGE_BLOCKS; b++) {
                if (page->blocks[b].ptr == block && page->blocks[b].size != 0) {
                    free(block);
                    page->blocks[b].size = 0;
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* 0x45b080 */
int mem_pool_destroy(int pool)
{
    struct mem_pool *p = s_pools[pool];
    int d, g, b;

    for (d = 0; d < MEM_POOL_DIRS; d++) {
        struct mem_dir *dir = p->dirs[d];

        if (dir == NULL) {
            continue;
        }
        for (g = 0; g < MEM_DIR_PAGES; g++) {
            struct mem_page *page = dir->pages[g];

            if (page == NULL) {
                continue;
            }
            for (b = 0; b < MEM_PAGE_BLOCKS; b++) {
                if (page->blocks[b].size != 0) {
                    free(page->blocks[b].ptr);
                }
            }
            free(page);
        }
        free(dir);
    }
    free(p);
    s_pools[pool] = NULL;
    return 1;
}

/* 0x446560 */
void mem_out_of_memory(void)
{
    log_printf("Out of memory, please refer to the manual!\n");
    exit(1);
}
