/*
 * file.c - Lisa 2 file helpers.
 *
 * Paths are the game's DOS paths; vfs_fopen finds them under the data
 * root.
 */

#include <stdio.h>

#include "lisa/file.h"
#include "lisa/mem.h"
#include "vfs.h"

int g_file_error;           /* 0x4bab34 */

/* 0x457420
 * The original leaves the file open when the read comes up short. */
int file_read_at(const char *name, void *buf, u32 count, s32 offset)
{
    FILE *fp = vfs_fopen(name, "rb");

    if (fp == NULL) {
        return FILE_ERR_OPEN;
    }
    if (fseek(fp, (long)offset, SEEK_SET) != 0 || fread(buf, 1, count, fp) != count) {
        fclose(fp);
        return FILE_ERR_READ;
    }
    fclose(fp);
    return 1;
}

/* 0x4574a0
 * On a short read the original leaks the block and the open file. */
void *file_load(const char *name)
{
    long size;
    void *buf;
    FILE *fp;

    if (file_exists(name) != 1) {
        g_file_error = FILE_ERR_MISSING;
        return NULL;
    }
    size = file_size(name);
    if (size == 0) {
        g_file_error = FILE_ERR_EMPTY;
        return NULL;
    }
    buf = mem_alloc(0, (u32)size);
    if (buf == NULL) {
        g_file_error = FILE_ERR_NO_MEMORY;
        return NULL;
    }
    fp = vfs_fopen(name, "rb");
    if (fp == NULL) {
        g_file_error = FILE_ERR_OPEN;
        return NULL;
    }
    if (fread(buf, 1, (size_t)size, fp) != (size_t)size) {
        fclose(fp);
        mem_free(0, buf);
        g_file_error = FILE_ERR_READ;
        return NULL;
    }
    fclose(fp);
    return buf;
}

/* 0x457590 */
int file_write(const char *name, const void *buf, u32 count)
{
    FILE *fp = vfs_fopen(name, "wb");

    if (fp == NULL) {
        return FILE_ERR_OPEN;
    }
    if (fwrite(buf, 1, count, fp) != count) {
        fclose(fp);
        return FILE_ERR_WRITE;
    }
    fclose(fp);
    return 1;
}

/* 0x4575f0 */
long file_stream_size(FILE *fp)
{
    long at = ftell(fp);
    long size;

    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, at, SEEK_SET);
    return size;
}

/* 0x457630
 * Opens in text mode and does not check fopen; callers test file_exists
 * first. On Windows the size of a text stream is still its byte size. */
long file_size(const char *name)
{
    FILE *fp = vfs_fopen(name, "rb");
    long size;

    if (fp == NULL) {
        return 0;
    }
    size = file_stream_size(fp);
    fclose(fp);
    return size;
}

/* 0x4576b0 */
int file_exists(const char *name)
{
    FILE *fp = vfs_fopen(name, "r");

    if (fp == NULL) {
        return FILE_ERR_NOT_FOUND;
    }
    fclose(fp);
    return 1;
}

/* 0x457660 */
int file_delete(const char *name)
{
    char path[1024];

    if (!vfs_resolve(name, path, sizeof(path)) || remove(path) != 0) {
        return FILE_ERR_DELETE;
    }
    return 1;
}
