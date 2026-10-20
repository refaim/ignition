/*
 * test_settings.c - checks settings_defaults against the bytes the
 * original 0x40c810 writes, as the store interpreter ran it.
 */

#include <stdio.h>
#include <string.h>

#include "settings.h"

static const u8 s_expected[SETTINGS_SIZE] = {
#include "settings_expected.inc"
};

int main(void)
{
    int i, bad = 0;

    memset(g_settings, 0, sizeof(g_settings));
    settings_defaults();
    for (i = 0; i < SETTINGS_SIZE; i++) {
        if (g_settings[i] != s_expected[i]) {
            fprintf(stderr, "offset 0x%03x: 0x%02x, expected 0x%02x\n",
                    i, g_settings[i], s_expected[i]);
            bad = 1;
        }
    }
    puts(bad ? "FAIL" : "ok");
    return bad;
}
