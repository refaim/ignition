/*
 * sound.h - Lisa 2 sound effects: sample banks and voices.
 *
 * A bank is a folder of group folders ("0_GRP0") of samples ("0_SEL.WAV"),
 * numbered by the digits their names start with. Samples are .WAV,
 * Gravis .PAT, FastTracker .XI or raw .RW8. A sample is played on
 * a voice; the game keeps the voice's handle and may change the voice's
 * record (setting its rate to 0 stops it).
 */

#ifndef IGN_SOUND_H
#define IGN_SOUND_H

#include "types.h"

enum {
    SOUND_BANKS = 8,
    SOUND_GROUPS = 128,
    SOUND_SAMPLES = 256,
    SOUND_VOICES = 64
};

/* 0x60 bytes in the original. */
struct sound_sample {
    void *data;         /* +0x00, signed 8 or 16 bit mono */
    s32 length;         /* +0x04, in samples */
    s32 loop_start;     /* +0x08 */
    s32 loop_length;    /* +0x0c */
    u8 envelope[64];    /* +0x10, loudness over the sample */
    s32 bits;           /* +0x50, 8 or 16 */
    s32 rate;           /* +0x54, Hz */
    s32 f58;            /* +0x58 */
    s32 shift;          /* +0x5c, scales the envelope */
};

/* 0x24 bytes in the original (0x63caa4). */
struct sound_voice {
    struct sound_sample *sample;    /* +0x00 */
    s32 pos;            /* +0x04, in samples */
    u32 frac;           /* +0x08 */
    s32 volume;         /* +0x0c, 0..65535 */
    s32 rate;           /* +0x10, Hz; 0 stops the voice */
    s32 loops;          /* +0x14 */
    s32 pan;            /* +0x18, -128..127 */
    s32 keep;           /* +0x1c, 1 keeps the record when stopped */
    struct sound_voice *next;       /* +0x20 */
};

/* What a play request carries (0x4be638 in the front end). */
struct sound_request {
    s32 bank;           /* +0x00 */
    s32 group;          /* +0x04 */
    s32 index;          /* +0x08 */
    s32 loops;          /* +0x0c */
    s32 volume;         /* +0x10 */
    s32 rate;           /* +0x14, 0 for the sample's own */
    s32 pan;            /* +0x18 */
    s32 keep;           /* +0x1c */
};

int sound_open(s32 *params);
int sound_close(void);
int sound_load_bank(const char *dir, int bank);
int sound_load_group(const char *dir, int bank, int group);
struct sound_sample *sound_sample_of(int bank, int group, int index);
int sound_unload(int bank, int group, int index);
s32 sound_start(const struct sound_request *req);
struct sound_voice *sound_voice_of(s32 handle);
int sound_set_volume(int volume);
void sound_stop_all(void);
void sound_tick(void);

#endif /* IGN_SOUND_H */
