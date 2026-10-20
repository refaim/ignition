/*
 * log.h - the Lisa 2 debug log.
 *
 * Messages go to stdout, to "Default.fil" in the working directory, or
 * both, when the log is enabled. Ignition enables the file and disables
 * stdout during start up (0x417ffd).
 */

#ifndef IGN_LISA_LOG_H
#define IGN_LISA_LOG_H

#include "types.h"

extern u8 g_log_enabled;     /* 0x4bac88 */
extern u8 g_log_to_stdout;   /* 0x4bac89 */
extern u8 g_log_to_file;     /* 0x4bac8a */

/* Prints the "Lisa 2 Development System" banner to stdout. */
int log_banner(void);

/* Enables the log and opens Default.fil when the file is wanted.
 * Returns 0 when the file cannot be created. */
int log_open(void);

/* printf to the enabled outputs. Returns 0 when the log is disabled or
 * the file cannot be created, 1 otherwise. */
int log_printf(const char *fmt, ...);

void log_close(void);

#endif /* IGN_LISA_LOG_H */
