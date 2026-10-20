/*
 * log.c - the Lisa 2 debug log.
 */

#include <stdarg.h>
#include <stdio.h>

#include "lisa/log.h"

u8 g_log_enabled;           /* 0x4bac88 */
u8 g_log_to_stdout;         /* 0x4bac89 */
u8 g_log_to_file;           /* 0x4bac8a */
static FILE *s_log_file;    /* 0x63c688 */

/* 0x45b4f0 */
int log_banner(void)
{
    printf("\nLisa 2 Development System, %s\n", "Compilation 0.91.0");
    printf("Copyright (c) UDS, 1995-1996\n\n");
    return 0;
}

/* 0x45b550 */
int log_open(void)
{
    g_log_enabled = 1;
    s_log_file = NULL;
    if (g_log_to_file == 1) {
        s_log_file = fopen("Default.fil", "wb");
        if (s_log_file == NULL) {
            return 0;
        }
    }
    return 1;
}

/* 0x45b5c0
 * The original formats into a 200 byte stack buffer with vsprintf; this
 * one truncates instead of overflowing. It flushes stdin after printing,
 * which does nothing useful; stdout is flushed here. */
int log_printf(const char *fmt, ...)
{
    char buf[200];
    va_list ap;

    if (!g_log_enabled) {
        return 0;
    }
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (g_log_to_stdout == 1) {
        printf("%s", buf);
        fflush(stdout);
    }
    if (g_log_to_file == 1 && s_log_file == NULL) {
        s_log_file = fopen("Default.fil", "wb");
        if (s_log_file == NULL) {
            return 0;
        }
    }
    if (g_log_to_file == 1) {
        fprintf(s_log_file, "%s", buf);
        fflush(s_log_file);
    }
    return 1;
}

/* 0x45b590 */
void log_close(void)
{
    g_log_enabled = 0;
    if (s_log_file != NULL) {
        fclose(s_log_file);
        s_log_file = NULL;
    }
}
