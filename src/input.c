/*
 * input.c - Lisa 2 keyboard input.
 */

#include <string.h>

#include "app.h"
#include "input.h"
#include "platform/platform.h"

struct joystick g_joysticks[2];    /* 0x4ba9f0 */
struct plat_stick g_plat_sticks[2];
u8 g_key_down[256];         /* 0x50de60 */
u8 g_key_trigger[256];      /* 0x50df68 */
u8 g_key_pressed[256];      /* 0x50e068 */
u8 g_key_held[256];         /* 0x50e168 */
char g_typed[65];           /* 0x50de18 */
char *g_typed_at;           /* 0x50de10 */
void (*g_key_hook)(int down, int key);     /* 0x50de14 */

static void (*s_hook2)(void);       /* 0x50e678 */
static s32 s_repeat_timer[256];     /* 0x50e278 */
static s32 s_repeat_delay;          /* 0x50df64 */
static s32 s_repeat_period;         /* 0x50e274 */
static u32 s_last_poll;             /* 0x50e268 */

/* Scan code to character for codes 0..0x53 (0x4ba660); 0xff for none. */
static const u8 s_key_chars[0x54] = {
    0xff, 0xff, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0xff, 0xff,
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 0xff, 0xff, 'A', 'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ';', '\'', 0xff, 0xff, '\\', 'Z', 'X', 'C', 'V',
    'B', 'N', 'M', ',', '.', '/', 0xff, '*', 0xff, ' ', 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, '7', '8', '9', '-', '4', '5', '6', '+', '1',
    '2', '3', '0', '.'
};

/* 0x455ab0 */
void input_reset(void)
{
    g_key_hook = NULL;
    s_hook2 = NULL;
}

/* 0x455ac0
 * After creating the DirectInput keyboard: clears the typed ring and the
 * key tables and keeps the repeat times. The original's two callers pass
 * no arguments, so the times are whatever is on the stack; see
 * app_init_devices. */
int input_init(s32 delay, s32 period)
{
    memset(g_typed, 0xff, 64);
    g_typed[64] = '\0';
    g_typed_at = g_typed;
    memset(g_key_trigger, 0, sizeof(g_key_trigger));
    memset(g_key_down, 0, sizeof(g_key_down));
    memset(g_key_pressed, 0, sizeof(g_key_pressed));
    memset(s_repeat_timer, 0, sizeof(s_repeat_timer));
    memset(g_key_held, 0, sizeof(g_key_held));
    s_repeat_delay = delay;
    s_repeat_period = period;
    s_last_poll = g_frame_time;
    return 1;
}

/* 0x455e20 */
void input_shutdown(void)
{
}

/* 0x4560c0 */
static void call_key_hook(int down, int key)
{
    if (g_key_hook != NULL) {
        g_key_hook(down, key);
    }
}

/* 0x4560e0 */
static void type_key(int down, int key)
{
    (void)down;
    if (key > 0x53) {
        return;
    }
    g_typed_at[0] = (char)s_key_chars[key];
    g_typed_at[32] = (char)s_key_chars[key];
    if (++g_typed_at == g_typed + 32) {
        g_typed_at = g_typed;
    }
}

/* 0x455ef0
 * The original reads up to 32 buffered events a frame; the platform
 * queue is drained here. */
void input_poll(void)
{
    int key, down;

    plat_pump_events();
    for (key = 0; key < 256; key++) {
        if (g_key_down[key] == 1) {
            s_repeat_timer[key] += (s32)(g_frame_time - s_last_poll);
            if (s_repeat_delay + s_repeat_period <= s_repeat_timer[key]) {
                s_repeat_timer[key] -= s_repeat_period;
                g_key_trigger[key] = 1;
            }
        }
    }
    while (plat_next_key_event(&key, &down)) {
        key &= 0xff;
        if (down) {
            g_key_down[key] = 1;
            if (g_key_held[key] == 0) {
                g_key_pressed[key] = 1;
                g_key_held[key] = 1;
            }
            if (s_repeat_timer[key] == 0) {
                g_key_trigger[key] = 1;
            }
            call_key_hook(1, key);
            type_key(1, key);
        } else {
            g_key_down[key] = 0;
            g_key_pressed[key] = 0;
            g_key_held[key] = 0;
            g_key_trigger[key] = 0;
            s_repeat_timer[key] = 0;
            call_key_hook(0, key);
        }
    }
    s_last_poll = g_frame_time;
}

/* 0x456070 */
int input_key_down(int key)
{
    return g_key_down[key & 0xff];
}

static s16 axis(int v)
{
    return (s16)(v < -100 ? -100 : v > 100 ? 100 : v);
}

/* 0x4596a0: which joysticks there are. The original asks winmm for each
 * one's ranges, to scale its axes to -100..100; the hosts give them
 * scaled. */
int joystick_init(void)
{
    int i;

    for (i = 0; i < 2; i++) {
        g_joysticks[i].present = g_plat_sticks[i].present;
    }
    return 1;
}

/* 0x459a20: each chosen joystick's axes, clamped to -100..100, and its
 * first two buttons. The port also takes a pad plugged in after the
 * start. */
int joystick_read(void)
{
    int i;

    for (i = 0; i < 2; i++) {
        struct joystick *js = &g_joysticks[i];
        const struct plat_stick *p = &g_plat_sticks[i];

        js->present = p->present;
        if (!js->present || !js->enabled) {
            continue;
        }
        js->x = axis(p->x);
        js->y = axis(p->y);
        js->buttons[0] = (u8)(p->buttons[0] != 0);
        js->buttons[1] = (u8)(p->buttons[1] != 0);
    }
    return 1;
}
