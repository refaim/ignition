/*
 * settings.c - the game settings.
 */

#include "settings.h"

/* 0x40c810
 * The stores are in the original's order; some overlap. Offsets are
 * from 0x4bf580. */
void settings_defaults(void)
{
    static const u8 keys[] = {
        0xcb, 0xcd, 0xc8, 0xd0, 0x35, 0x34, 0x36, 0x28,
        0x2e, 0x30, 0x21, 0x2f, 0x2b
    };
    int i;

    settings_set(0x000, SETTINGS_VERSION);
    settings_set(0x004, 0);
    settings_set(0x008, 0);
    settings_set(0x00c, 0);
    settings_set(0x010, 1);
    settings_set(0x014, 1);
    settings_set(0x018, 1);
    settings_set(0x01c, 0);
    settings_set(0x024, 3);
    settings_set(0x020, 2);
    settings_set(0x034, 8);
    settings_set(0x038, 8);
    settings_set(0x03c, 0);
    settings_set(0x040, 0);
    settings_set(0x044, 0);
    settings_set(0x028, 4);
    settings_set(0x02c, 5);
    settings_set(0x030, 7);
    for (i = 0; i < 9; i++) {
        settings_set(0x048 + 4 * i, 1);
    }
    settings_set8(0x06f, 0);
    settings_set8(0x06c, 'P');
    settings_set8(0x0e4, 'P');
    settings_set8(0x06d, 'L');
    settings_set8(0x0e7, 0);
    settings_set(0x0f3, 0);
    settings_set(0x0f7, 0);
    settings_set(0x0fb, 1);
    settings_set8(0x06e, '1');
    settings_set8(0x0e5, 'L');
    settings_set(0x107, 0);
    settings_set(0x10b, 0);
    settings_set(0x15b, 0);
    settings_set(0x10f, 0);
    settings_set(0x113, 1);
    settings_set(0x117, 1);
    settings_set(0x11b, 2);
    settings_set(0x11f, 2);
    settings_set(0x123, 0);
    settings_set(0x127, 0);
    settings_set(0x12b, 0);
    settings_set(0x12f, 0);
    settings_set8(0x0e6, '2');
    settings_set(0x0ff, 10);
    settings_set(0x103, 3);
    for (i = 0; i < 8; i++) {
        settings_set(0x133 + 4 * i, i);
    }
    settings_set(0x157, 0);
    settings_set(0x15f, 0);
    settings_set(0x153, 1);
    for (i = 0; i < 7; i++) {
        settings_set(0x163 + 4 * i, 1);
    }
    for (i = 0; i < (int)sizeof(keys); i++) {
        settings_set8(0x17f + i, keys[i]);
    }
    settings_set(0x18f, 0);
    settings_set(0x193, 0);
    settings_set8(0x18c, 0x2a);
    settings_set8(0x19d, 0);
    settings_set8(0x19c, '1');
    settings_set8(0x1aa, '1');
    settings_set8(0x18d, 0x2c);
    settings_set8(0x1ac, 0);
    settings_set(0x57f, 0);
    settings_set(0x583, 0);
    settings_set(0x587, 0);
    settings_set(0x58b, 0);
    settings_set(0x58f, 0);
    settings_set(0x593, 0);
    settings_set8(0x18e, 0x1f);
    settings_set8(0x19b, 'G');
    settings_set8(0x1ab, '2');
}
