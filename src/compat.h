/*
 * compat.h - the few POSIX calls Windows names differently.
 */

#ifndef IGN_COMPAT_H
#define IGN_COMPAT_H

#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
/* No permission bits on Windows. */
#define mkdir(path, mode) _mkdir(path)
#endif

#endif /* IGN_COMPAT_H */
