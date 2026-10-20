/*
 * file.h - Lisa 2 file helpers.
 *
 * Calls return 1 on success or one of the FILE_ERR codes. file_load
 * returns the buffer and leaves its error code in g_file_error.
 */

#ifndef IGN_LISA_FILE_H
#define IGN_LISA_FILE_H

#include <stdio.h>

#include "types.h"

enum {
    FILE_ERR_OPEN = 0x7d0,
    FILE_ERR_READ = 0x7da,
    FILE_ERR_WRITE = 0x7e4,
    FILE_ERR_MISSING = 0x7ee,      /* file_load */
    FILE_ERR_NOT_FOUND = 0x7ef,    /* file_exists */
    FILE_ERR_EMPTY = 0x7f8,
    FILE_ERR_NO_MEMORY = 0x802,
    FILE_ERR_DELETE = 0x80c
};

extern int g_file_error;    /* 0x4bab34 */

/* Reads `count` bytes at `offset` into `buf`. */
int file_read_at(const char *name, void *buf, u32 count, s32 offset);

/* The whole file, in a block of the DEFAULT memory pool. NULL on error. */
void *file_load(const char *name);

int file_write(const char *name, const void *buf, u32 count);

/* Size of an open stream; the position is kept. */
long file_stream_size(FILE *fp);

/* Size of a file. 0 when it cannot be opened. */
long file_size(const char *name);

/* Deletes a file: 1, or FILE_ERR_DELETE. */
int file_delete(const char *name);

/* 1, or FILE_ERR_NOT_FOUND. */
int file_exists(const char *name);

#endif /* IGN_LISA_FILE_H */
