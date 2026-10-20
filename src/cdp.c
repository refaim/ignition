/*
 * cdp.c - the intro's .CDP animations (0x412560..0x412640 and the byte
 * code decoder at 0x499abc, in the data section).
 */

#include <string.h>

#include "cdp.h"
#include "video.h"

/* 0x412580 */
int cdp_open(struct cdp_anim *a)
{
    const u8 *f = a->file;

    a->open = 0;
    if (memcmp(f, "CDP", 4) != 0) {
        return 0;
    }
    if ((f[4] | f[5] << 8) != 100) {
        return 0;
    }
    a->palette = a->file + 0x10;
    a->start = f + 0x310;
    a->cursor = a->start;
    a->frames = (u16)(f[6] | f[7] << 8);
    a->loop = (u16)(f[8] | f[9] << 8);
    a->width = (u16)(f[0xa] | f[0xb] << 8);
    a->height = (u16)(f[0xc] | f[0xd] << 8);
    a->open = 1;
    a->frame = 0;
    return 1;
}

static u16 rd16(const u8 *p)
{
    return (u16)(p[0] | p[1] << 8);
}

/* 0x499abc
 * Draws one frame over the last: bytes up to 245 are pixels; 246 takes
 * the next byte as a pixel; 247 to 250 skip 2 to 5 pixels, 251 skips a
 * byte's count and 252 a word's; 253 is a run (count, colour; a count
 * of 0 is 256); 254 a run with a word's count; 255 ends the
 * frame. Returns 0 after the last frame of an animation that does not
 * loop, 1 otherwise. */
static void put(struct cdp_anim *a, u32 *at, u8 c)
{
    if (*at < (u32)a->width * a->height) {
        a->screen[*at] = c;
    }
    (*at)++;
}

static int decode(struct cdp_anim *a)
{
    const u8 *in = a->cursor;
    const u8 *end = a->file + a->size;
    u32 at = 0;

    for (;;) {
        u8 op;

        if (in + 3 > end) {
            /* Out of data: the animation is over. */
            a->cursor = a->start;
            a->frame = 0;
            return a->loop == 1;
        }
        op = *in++;
        if (op < 246) {
            put(a, &at, op);
        } else if (op == 246) {
            put(a, &at, *in++);
        } else if (op <= 250) {
            at += op - 245;
        } else if (op == 251) {
            at += *in++;
        } else if (op == 252) {
            at += rd16(in);
            in += 2;
        } else if (op == 253) {
            u8 count = in[0], colour = in[1];

            in += 2;
            do {
                put(a, &at, colour);
            } while (--count != 0);
        } else if (op == 254) {
            u16 count = rd16(in);
            u8 colour = in[2];

            in += 3;
            do {
                put(a, &at, colour);
            } while (--count != 0);
        } else {
            break;
        }
    }
    a->cursor = in;
    a->frame++;
    if (a->frames == a->frame) {
        a->cursor = a->start;
        a->frame = 0;
        if (a->loop != 1) {
            return 0;
        }
    }
    return 1;
}

/* 0x412610: -1 when no animation is open. */
int cdp_next_frame(struct cdp_anim *a)
{
    if (!a->open) {
        return -1;
    }
    return decode(a);
}

/* 0x412560 */
int cdp_draw(struct cdp_anim *a)
{
    video_set_palette(a->file + 0x10);
    return 0;
}
