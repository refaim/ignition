/*
 * input.h - Lisa 2 keyboard input, on the platform's key events.
 *
 * The original reads DirectInput's buffered keyboard data once a frame
 * (input_poll) into four tables indexed by scan code (DIK_*):
 *   g_key_down     1 while held
 *   g_key_trigger  1 on press, again on each auto-repeat
 *   g_key_pressed  1 on press until the game clears it (the menus read
 *                  and clear this one, through 0x4c50e0)
 *   g_key_held     1 while held, for the edge test
 * It also keeps the last 32 typed characters, for codes typed in menus.
 */

#ifndef IGN_INPUT_H
#define IGN_INPUT_H

#include "types.h"

extern u8 g_key_down[256];      /* 0x50de60 */
extern u8 g_key_trigger[256];   /* 0x50df68 */
extern u8 g_key_pressed[256];   /* 0x50e068 */
extern u8 g_key_held[256];      /* 0x50e168 */

/* The typed characters: a ring of 32 mirrored 32 bytes on, then a NUL,
 * so the 32 characters before g_typed_at always read as one string. */
extern char g_typed[65];        /* 0x50de18 */
extern char *g_typed_at;        /* 0x50de10 */

/* Called with (down, scan code) for each key event when set. */
extern void (*g_key_hook)(int down, int key);  /* 0x50de14 */

void input_reset(void);

/* `delay` and `period` are the auto-repeat times in ms. */
int input_init(s32 delay, s32 period);
void input_shutdown(void);
void input_poll(void);
int input_key_down(int key);

#endif /* IGN_INPUT_H */
