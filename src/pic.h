/*
 * pic.h - .PIC pictures, as the game keeps them: the whole file in
 * memory, addressed by offset.
 *
 *   0x00  u32  file size
 *   0x04  u16  0x9500
 *   0x06  u16  width
 *   0x08  u16  height
 *   0x0a  u16  x, u16 y    position the picture was cut from (unused)
 *   0x12  u16  8, bits per pixel
 *   0x4e  768  palette, 256 x RGB, 8 bits a channel
 *   0x34e      width x height pixels, rows top to bottom
 *
 * The game reads pixels from 0x34e with a stride of the width (for
 * example 0x100 for N_SYSGFX.PIC in game_video_init, 0x417ea0) or loads
 * only the pixels with file_read_at(.., 0x34e) (the menu, 0x403e80).
 */

#ifndef IGN_PIC_H
#define IGN_PIC_H

#include "types.h"

enum {
    PIC_MAGIC = 0x9500,
    PIC_PALETTE = 0x4e,
    PIC_PIXELS = 0x34e
};

static inline u16 pic_u16(const u8 *pic, int at)
{
    return (u16)(pic[at] | (pic[at + 1] << 8));
}

static inline int pic_width(const u8 *pic)
{
    return pic_u16(pic, 6);
}

static inline int pic_height(const u8 *pic)
{
    return pic_u16(pic, 8);
}

static inline const u8 *pic_palette(const u8 *pic)
{
    return pic + PIC_PALETTE;
}

static inline const u8 *pic_pixels(const u8 *pic)
{
    return pic + PIC_PIXELS;
}

#endif /* IGN_PIC_H */
