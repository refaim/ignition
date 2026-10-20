/*
 * vfs.h - the game's paths on the host file system.
 *
 * The game opens files by DOS path relative to its folder, in any case
 * and with backslashes ("baltazar\data\menu.col", "LEVELS\CANADA\..").
 * vfs_fopen resolves such a path under the data root, matching each
 * component without regard to case.
 */

#ifndef IGN_VFS_H
#define IGN_VFS_H

#include <stddef.h>
#include <stdio.h>

/* The folder that holds the game files (the disc root). */
void vfs_set_root(const char *root);
const char *vfs_root(void);

/* Writes the host path of `name` to `out`. A missing last component is
 * kept as given, so files can be created. Returns 0 when a folder on the
 * way does not exist. */
int vfs_resolve(const char *name, char *out, size_t out_size);

/* fopen on the resolved path. */
FILE *vfs_fopen(const char *name, const char *mode);

#endif /* IGN_VFS_H */
