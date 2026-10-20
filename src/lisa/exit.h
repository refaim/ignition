/*
 * exit.h - Lisa 2 exit handlers.
 *
 * Library modules register a function to run at shut down. Handlers run
 * in two passes, priority 0x10000 first, then 0x20000; exit_register
 * always uses 0x10000. Up to 200 handlers; ids come from a stack of 199.
 */

#ifndef IGN_LISA_EXIT_H
#define IGN_LISA_EXIT_H

#include "types.h"

typedef int (*exit_fn)(s32 user);

int exit_init(void);

/* A new handler id, or -1. */
int exit_alloc_id(void);

/* The original passes name, function and user value in globals
 * (0x63c690, 0x63c694, 0x63c698); here they are arguments. Returns -1
 * when the table is full. */
int exit_register(int id, const char *name, exit_fn fn, s32 user);

/* Disables every handler registered with `id`. */
int exit_unregister(int id);

/* Runs and disables every handler. Returns 1. */
int exit_run_all(void);

#endif /* IGN_LISA_EXIT_H */
