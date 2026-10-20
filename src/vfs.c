/*
 * vfs.c - the game's paths on the host file system.
 */

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "vfs.h"

static char s_root[1024] = ".";

void vfs_set_root(const char *root)
{
    snprintf(s_root, sizeof(s_root), "%s", root);
}

const char *vfs_root(void)
{
    return s_root;
}

/* Appends `part` to `path` with the case found on disk. */
static int append_component(char *path, size_t size, const char *part, int last)
{
    DIR *dir = opendir(path);
    struct dirent *e;
    size_t n = strlen(path);
    const char *found = part;

    if (dir == NULL) {
        return 0;
    }
    while ((e = readdir(dir)) != NULL) {
        if (strcasecmp(e->d_name, part) == 0) {
            found = e->d_name;
            break;
        }
    }
    if (e == NULL && !last) {
        closedir(dir);
        return 0;
    }
    snprintf(path + n, size - n, "/%s", found);
    closedir(dir);
    return 1;
}

int vfs_resolve(const char *name, char *out, size_t out_size)
{
    char tmp[1024], *part, *save;

    snprintf(tmp, sizeof(tmp), "%s", name);
    for (part = tmp; *part != '\0'; part++) {
        if (*part == '\\') {
            *part = '/';
        }
    }
    snprintf(out, out_size, "%s", s_root);
    for (part = strtok_r(tmp, "/", &save); part != NULL; ) {
        char *next = strtok_r(NULL, "/", &save);

        if (strcmp(part, ".") != 0
            && !append_component(out, out_size, part, next == NULL)) {
            return 0;
        }
        part = next;
    }
    return 1;
}

FILE *vfs_fopen(const char *name, const char *mode)
{
    char path[1024];

    if (!vfs_resolve(name, path, sizeof(path))) {
        return NULL;
    }
    return fopen(path, mode);
}
