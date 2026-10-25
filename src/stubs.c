/*
 * stubs.c - original routines not reimplemented yet.
 */

#include <stdlib.h>

#include "app.h"
#include "game.h"
#include "stubs.h"









#define STUB_VOID(addr, name) void name(void) { STUB(addr, #name); }

STUB_VOID(0x45f490, render_init_45f490)
STUB_VOID(0x460740, render_init_460740)
STUB_VOID(0x4614d0, render_init_4614d0)
STUB_VOID(0x4607c0, render_init_4607c0)
STUB_VOID(0x45f640, render_init_45f640)
STUB_VOID(0x45fa50, render_init_45fa50)
STUB_VOID(0x460b50, render_init_460b50)


/* No audio yet: the menu runs with sound off. */






