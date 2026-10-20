/*
 * cdp.h - the intro's .CDP animations.
 *
 *   0x000  "CDP\0"
 *   0x004  u16  100, the version
 *   0x006  u16  number of frames
 *   0x008  u16  1 to loop
 *   0x00a  u16  width
 *   0x00c  u16  height
 *   0x010  768  palette
 *   0x310       frames, each a byte code over the screen ending in 255
 *
 * IGN1.CDP and IGN2.CDP hold two frames fewer than their headers give.
 * The original reads past the file into whatever follows on the heap;
 * here the end of the data ends the animation, and writes stop at the
 * screen's end.
 */

#ifndef IGN_CDP_H
#define IGN_CDP_H

#include "types.h"

/* The player's record, 0x22 bytes in the original. */
struct cdp_anim {
    u8 *file;           /* +0x00, the whole file */
    u8 *screen;         /* +0x04, 8 bit, width a line */
    u16 frames;         /* +0x08 */
    u16 loop;           /* +0x0a */
    u16 width;          /* +0x0c */
    u16 height;         /* +0x0e */
    u8 *palette;        /* +0x10 */
    u16 frame;          /* +0x14, the next frame */
    s32 open;           /* +0x16 */
    const u8 *start;    /* +0x1a, the first frame */
    const u8 *cursor;   /* +0x1e */
    u32 size;           /* the file's size; not in the original */
};

int cdp_open(struct cdp_anim *a);
int cdp_next_frame(struct cdp_anim *a);
int cdp_draw(struct cdp_anim *a);

#endif /* IGN_CDP_H */
