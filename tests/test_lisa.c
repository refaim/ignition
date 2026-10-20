/*
 * test_lisa.c - checks the Lisa 2 pool and file helpers against the
 * behaviour read from IGN_WIN.EXE.
 */

#include <stdio.h>
#include <string.h>

#include "lisa/file.h"
#include "lisa/mem.h"
#include "vfs.h"

static int s_failed;

#define CHECK(cond) do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
            s_failed = 1; \
        } \
    } while (0)

static void test_pools(void)
{
    void *blocks[MEM_PAGE_BLOCKS * 2 + 1];
    int pool, i;

    CHECK(mem_init() == 1);
    pool = mem_pool_create("TEST");
    CHECK(pool == 1);   /* 0 is DEFAULT */
    for (i = 0; i < (int)(sizeof(blocks) / sizeof(blocks[0])); i++) {
        blocks[i] = mem_calloc(pool, 16);
        CHECK(blocks[i] != NULL);
        CHECK(((unsigned char *)blocks[i])[15] == 0);
    }
    /* A freed block's slot is reused before a new page is made. */
    CHECK(mem_free(pool, blocks[3]) == 1);
    CHECK(mem_free(pool, blocks[3]) == 0);
    blocks[3] = mem_alloc(pool, 8);
    CHECK(blocks[3] != NULL);
    CHECK(mem_free(pool, (void *)&pool) == 0);
    CHECK(mem_pool_destroy(pool) == 1);
    /* The slot is free again. */
    CHECK(mem_pool_create("AGAIN") == pool);
    CHECK(mem_pool_destroy(pool) == 1);
}

static void test_files(const char *dir)
{
    char path[1024], host[1024];
    char buf[4] = {0};
    char *whole;

    snprintf(path, sizeof(path), "%s\\TEST_LISA.BIN", dir);
    CHECK(file_write(path, "IGNITION", 8) == 1);
    CHECK(file_exists(path) == 1);
    CHECK(file_size(path) == 8);
    CHECK(file_read_at(path, buf, 3, 2) == 1);
    CHECK(memcmp(buf, "NIT", 3) == 0);
    CHECK(file_read_at(path, buf, 4, 6) == FILE_ERR_READ);
    whole = file_load(path);
    CHECK(whole != NULL && memcmp(whole, "IGNITION", 8) == 0);
    CHECK(vfs_resolve(path, host, sizeof(host)) == 1);
    remove(host);
    CHECK(file_exists(path) == FILE_ERR_NOT_FOUND);
    CHECK(file_load(path) == NULL && g_file_error == FILE_ERR_MISSING);
    CHECK(file_read_at(path, buf, 1, 0) == FILE_ERR_OPEN);
}

int main(int argc, char **argv)
{
    test_pools();
    vfs_set_root(argc > 1 ? argv[1] : ".");
    test_files(".");
    puts(s_failed ? "FAIL" : "ok");
    return s_failed;
}
