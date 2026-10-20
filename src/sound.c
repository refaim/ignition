/*
 * sound.c - Lisa 2 sound effects (0x4577a0..0x459580).
 *
 * The banks, loaders, voice records and handles follow the original.
 * The mixing itself, which the original does in assembly into a
 * DirectSound buffer (0x466150), is the mixer's (audio.c): it plays the
 * five loudest voices, as 0x467240 chooses them, at their rate, volume
 * and pan, with their loops.
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "audio.h"
#include "sound.h"
#include "stubs.h"
#include "vfs.h"

static struct sound_sample **s_banks[SOUND_BANKS][SOUND_GROUPS];  /* 0x50f528 */
static struct sound_voice s_voices[SOUND_VOICES];  /* 0x63caa4 */
static struct sound_voice *s_free[SOUND_VOICES];   /* 0x63eea4 */
static int s_free_count;                           /* 0x63f2a4 */
static u16 s_seq[SOUND_VOICES];                    /* 0x50ed48 */
static int s_is_open = -1;                            /* 0x4ba7a4 */
static int s_channels;                             /* 0x5203f4 */
static s32 s_volume = 0x10000;                     /* 0x63f2a8 */

/* 0x4577a0
 * `params` is { channels, ?, ? } ({ 5, 100, 2 } from the front end).
 * Returns 1 with sound, 0 without, as no card. */
int sound_open(s32 *params)
{
    int i;

    if (s_is_open != -1) {
        return 0;
    }
    if (!audio_init()) {
        return 0;
    }
    s_is_open = 1;
    s_channels = params[0];
    s_free_count = 0;
    for (i = SOUND_VOICES - 1; i >= 0; i--) {
        memset(&s_voices[i], 0, sizeof(s_voices[i]));
        s_free[s_free_count++] = &s_voices[i];
    }
    memset(s_seq, 0, sizeof(s_seq));
    audio_voices_set(s_voices, SOUND_VOICES, s_channels);
    return 1;
}

/* 0x459360: a sample record with room for `size` bytes of data. */
static struct sound_sample *sample_alloc(u32 size)
{
    struct sound_sample *s = calloc(1, sizeof(*s));

    s->data = malloc(size != 0 ? size : 1);
    return s;
}

static void sample_free(struct sound_sample *s)
{
    if (s != NULL) {
        free(s->data);
        free(s);
    }
}

/* 0x458cb0 */
static void bank_free(int bank)
{
    int g, i;

    for (g = 0; g < SOUND_GROUPS; g++) {
        if (s_banks[bank][g] != NULL) {
            for (i = 0; i < SOUND_SAMPLES; i++) {
                sample_free(s_banks[bank][g][i]);
            }
            free(s_banks[bank][g]);
            s_banks[bank][g] = NULL;
        }
    }
}

/* 0x4578d0 */
int sound_close(void)
{
    int b;

    if (s_is_open == -1) {
        return 0;
    }
    audio_voices_set(NULL, 0, 0);
    for (b = 0; b < SOUND_BANKS; b++) {
        bank_free(b);
    }
    s_is_open = -1;
    return 1;
}

/* 0x459580
 * Loudness in 64 steps over the sample, the mean change from one
 * sample to the next, scaled so the largest is at most 255. */
static void make_envelope(struct sound_sample *s)
{
    s32 env[64];
    int step = (s->length - 1) / 64, i, k, shift = 0;
    s32 most = 0;

    for (i = 0; i < 64; i++) {
        s32 sum = 0;

        for (k = 0; k < step; k++) {
            int at = i * step + k;
            int d;

            if (s->bits == 8) {
                d = ((s8 *)s->data)[at] - ((s8 *)s->data)[at + 1];
            } else {
                d = ((s16 *)s->data)[at] - ((s16 *)s->data)[at + 1];
            }
            sum += d < 0 ? -d : d;
        }
        if (s->bits == 8) {
            sum <<= 8;
        }
        env[i] = step > 0 ? sum / step : 0;
        if (env[i] > most) {
            most = env[i];
        }
    }
    while (most > 0xff) {
        most >>= 1;
        shift++;
    }
    s->shift = 8 - shift;
    for (i = 0; i < 64; i++) {
        s->envelope[i] = (u8)(env[i] >> shift);
    }
}

static u32 rd32(const u8 *p)
{
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

/* 0x4587d0: a RIFF WAVE file, PCM mono. A "smpl" chunk's first loop is
 * the loop; otherwise the whole sample. */
static struct sound_sample *load_wav(const char *path)
{
    FILE *fp = vfs_fopen(path, "rb");
    u8 *file, *p, *end, *fmt = NULL, *data = NULL, *smpl = NULL;
    u32 data_size = 0;
    long size;
    struct sound_sample *s;

    if (fp == NULL) {
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    file = malloc((size_t)size);
    if (file == NULL || fread(file, 1, (size_t)size, fp) != (size_t)size || size < 12
        || memcmp(file, "RIFF", 4) != 0 || memcmp(file + 8, "WAVE", 4) != 0) {
        fclose(fp);
        free(file);
        return NULL;
    }
    fclose(fp);
    end = file + size;
    for (p = file + 12; p + 8 <= end; ) {
        u32 len = rd32(p + 4);

        if (memcmp(p, "fmt ", 4) == 0) {
            fmt = p + 8;
        } else if (memcmp(p, "data", 4) == 0) {
            data = p + 8;
            data_size = len;
        } else if (memcmp(p, "smpl", 4) == 0) {
            smpl = p + 8;
        }
        p += 8 + len + (len & 1);
    }
    if (fmt == NULL || data == NULL || fmt[0] != 1 || fmt[2] != 1 || data + data_size > end) {
        free(file);
        return NULL;
    }
    s = sample_alloc(data_size);
    s->rate = (s32)rd32(fmt + 4);
    s->bits = (fmt[14] + 7) & 0xf8;
    s->length = (s32)(data_size * 8 / (u32)s->bits);
    memcpy(s->data, data, data_size);
    if (smpl != NULL && rd32(smpl + 28) != 0) {
        s->loop_start = (s32)rd32(smpl + 44);
        s->loop_length = (s32)(rd32(smpl + 48) - rd32(smpl + 44));
    } else {
        s->loop_start = 0;
        s->loop_length = s->length;
    }
    if (s->bits == 8) {
        int i;

        for (i = 0; i < s->length; i++) {
            ((u8 *)s->data)[i] ^= 0x80;
        }
    }
    free(file);
    return s;
}

/* Reads `size` bytes at `offset` of a file, or returns 0. */
static int read_at(const char *path, long offset, void *buf, size_t size)
{
    FILE *fp = vfs_fopen(path, "rb");
    int ok;

    if (fp == NULL) {
        return 0;
    }
    ok = fseek(fp, offset, SEEK_SET) == 0 && fread(buf, 1, size, fp) == size;
    fclose(fp);
    return ok;
}

/* 0x459530: unsigned samples to signed. */
static void make_signed(struct sound_sample *s)
{
    int i;

    for (i = 0; i < s->length; i++) {
        if (s->bits == 8) {
            ((u8 *)s->data)[i] ^= 0x80;
        } else {
            ((u16 *)s->data)[i] ^= 0x8000;
        }
    }
}

/* Halves the counts of a 16 bit sample, from bytes to samples. */
static void to_samples(struct sound_sample *s)
{
    if (s->bits == 16) {
        s->length /= 2;
        s->loop_start /= 2;
        s->loop_length /= 2;
    }
}

/* 0x4582b0: a Gravis GF1 patch, its first wave. */
static struct sound_sample *load_pat(const char *path)
{
    u8 head[0x14f];
    struct sound_sample *s;
    u32 size;
    u8 modes;

    if (!read_at(path, 0, head, sizeof(head)) || memcmp(head, "GF1PATCH110", 12) != 0) {
        return NULL;
    }
    size = rd32(head + 0xf7);
    s = sample_alloc(size);
    if (!read_at(path, sizeof(head), s->data, size)) {
        sample_free(s);
        return NULL;
    }
    modes = head[0x126];
    s->length = (s32)size;
    s->loop_start = (s32)rd32(head + 0xfb);
    s->loop_length = (s32)(rd32(head + 0xff) - rd32(head + 0xfb));
    s->bits = (modes & 1) * 8 + 8;
    s->rate = head[0x103] | head[0x104] << 8;
    to_samples(s);
    if (modes & 2) {
        make_signed(s);
    }
    return s;
}

/* 0x4584d0: a FastTracker 2 instrument, its first sample, delta coded,
 * at 22050 Hz. */
static struct sound_sample *load_xi(const char *path)
{
    u8 head[0x28];
    struct sound_sample *s;
    u32 size;
    int i;

    if (!read_at(path, 0x12a, head, sizeof(head))) {
        return NULL;
    }
    size = rd32(head);
    s = sample_alloc(size);
    if (!read_at(path, 0x12a + sizeof(head), s->data, size)) {
        sample_free(s);
        return NULL;
    }
    s->length = (s32)size;
    s->loop_start = (s32)rd32(head + 4);
    s->loop_length = (s32)rd32(head + 8);
    s->bits = (head[0xe] & 0x10) / 2 + 8;
    s->rate = 22050;
    to_samples(s);
    if (s->bits == 8) {
        u8 v = 0;

        for (i = 0; i < s->length; i++) {
            v += ((u8 *)s->data)[i];
            ((u8 *)s->data)[i] = v;
        }
    } else {
        u16 v = 0;

        for (i = 0; i < s->length; i++) {
            v += ((u16 *)s->data)[i];
            ((u16 *)s->data)[i] = v;
        }
    }
    return s;
}

/* 0x4586a0: signed 8 bit at 22050 Hz, the whole file. */
static struct sound_sample *load_rw8(const char *path)
{
    FILE *fp = vfs_fopen(path, "rb");
    struct sound_sample *s;
    long size;

    if (fp == NULL) {
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    s = sample_alloc((u32)size);
    if (fread(s->data, 1, (size_t)size, fp) != (size_t)size) {
        fclose(fp);
        sample_free(s);
        return NULL;
    }
    fclose(fp);
    s->length = (s32)size;
    s->loop_start = 0;
    s->loop_length = (s32)size;
    s->bits = 8;
    s->rate = 22050;
    return s;
}

/* The number a name starts with, or -1. */
static int leading_number(const char *name)
{
    int n = 0, i;

    if (name[0] < '0' || name[0] > '9') {
        return -1;
    }
    for (i = 0; name[i] >= '0' && name[i] <= '9'; i++) {
        n = n * 10 + name[i] - '0';
    }
    return n;
}

static int has_ext(const char *name, const char *ext)
{
    size_t n = strlen(name), e = strlen(ext);

    return n > e && strcasecmp(name + n - e, ext) == 0;
}

/* 0x458e00: the samples of one group folder. */
int sound_load_group(const char *dir, int bank, int group)
{
    char host[1024];
    DIR *d;
    struct dirent *e;

    if (!vfs_resolve(dir, host, sizeof(host)) || (d = opendir(host)) == NULL) {
        return 0;
    }
    if (s_banks[bank][group] == NULL) {
        s_banks[bank][group] = calloc(SOUND_SAMPLES, sizeof(**s_banks[bank]));
    }
    while ((e = readdir(d)) != NULL) {
        int index = leading_number(e->d_name);
        char path[1024];
        struct sound_sample *s = NULL;

        if (index < 0 || index >= SOUND_SAMPLES) {
            continue;
        }
        snprintf(path, sizeof(path), "%s\\%s", dir, e->d_name);
        if (has_ext(e->d_name, ".WAV")) {
            s = load_wav(path);
        } else if (has_ext(e->d_name, ".PAT")) {
            s = load_pat(path);
        } else if (has_ext(e->d_name, ".RW8")) {
            s = load_rw8(path);
        } else if (has_ext(e->d_name, ".XI")) {
            s = load_xi(path);
        } else {
            continue;
        }
        if (s == NULL) {
            /* As the original, the first file that fails ends the group. */
            closedir(d);
            return 0;
        }
        make_envelope(s);
        if (s->loop_length == 0) {
            s->loop_length = s->length - s->loop_start;
        }
        sample_free(s_banks[bank][group][index]);
        s_banks[bank][group][index] = s;
    }
    closedir(d);
    return 1;
}

/* 0x459140: every numbered group folder of `dir` into bank `bank`. */
int sound_load_bank(const char *dir, int bank)
{
    char host[1024];
    DIR *d;
    struct dirent *e;

    if (bank < 0 || bank > 7) {
        return 0;
    }
    bank_free(bank);
    if (!vfs_resolve(dir, host, sizeof(host)) || (d = opendir(host)) == NULL) {
        return 1;
    }
    while ((e = readdir(d)) != NULL) {
        int group = leading_number(e->d_name);
        char path[1024];
        size_t n;

        if (group < 0 || group >= SOUND_GROUPS) {
            continue;
        }
        snprintf(path, sizeof(path), "%s", dir);
        n = strlen(path);
        if (n > 0 && path[n - 1] != '\\') {
            snprintf(path + n, sizeof(path) - n, "\\");
        }
        snprintf(path + strlen(path), sizeof(path) - strlen(path), "%s", e->d_name);
        sound_load_group(path, bank, group);
    }
    closedir(d);
    return 1;
}

/* 0x459330 */
struct sound_sample *sound_sample_of(int bank, int group, int index)
{
    if (s_banks[bank][group] == NULL) {
        return NULL;
    }
    return s_banks[bank][group][index];
}

/* 0x458d80: frees one sample; only the first 16 of a group. */
int sound_unload(int bank, int group, int index)
{
    if (bank < 0 || bank > 7 || group < 0 || group > 0x7f || index < 0 || index > 0xf) {
        return 0;
    }
    if (s_banks[bank][group] == NULL || s_banks[bank][group][index] == NULL) {
        return 0;
    }
    sample_free(s_banks[bank][group][index]);
    s_banks[bank][group][index] = NULL;
    return 1;
}

/* 0x4579b0
 * Starts a voice and returns its handle, slot << 16 | sequence, or 0. */
s32 sound_start(const struct sound_request *req)
{
    struct sound_sample *s;
    struct sound_voice *v;
    int slot;

    if (s_is_open == -1) {
        return 0;
    }
    s = sound_sample_of(req->bank, req->group, req->index);
    if (s == NULL || s_free_count == 0) {
        return 0;
    }
    v = s_free[--s_free_count];
    v->sample = s;
    v->pos = 0;
    v->frac = 0;
    v->volume = req->volume;
    v->rate = req->rate != 0 ? req->rate : s->rate;
    v->loops = req->loops;
    v->pan = req->pan;
    v->keep = req->keep;
    if (v->rate > 160000) {
        v->rate = 160000;
    }
    slot = (int)(v - s_voices);
    if (++s_seq[slot] == 0) {
        s_seq[slot] = 1;
    }
    audio_voice_start(v);
    return (s32)(slot << 16) + (s16)s_seq[slot];
}

/* 0x457aa0 */
struct sound_voice *sound_voice_of(s32 handle)
{
    int slot;

    if (handle == 0 || s_is_open == -1) {
        return NULL;
    }
    slot = handle >> 16;
    if (slot < 0 || slot >= SOUND_VOICES || s_seq[slot] != (u16)handle) {
        return NULL;
    }
    return &s_voices[slot];
}

/* 0x457ae0 */
int sound_set_volume(int volume)
{
    if (volume < 0 || volume > 0x10000 || s_is_open == -1) {
        return 0;
    }
    s_volume = volume;
    audio_voices_volume(volume);
    return 1;
}

/* 0x457910, through 0x4673d0 */
void sound_stop_all(void)
{
    int i;

    for (i = 0; i < SOUND_VOICES; i++) {
        s_voices[i].rate = 0;
    }
}

/* 0x457890: the original mixes into its DirectSound buffer here; the
 * mixer pulls on its own thread. Voices that stopped go back to the
 * free list unless kept (0x467120). */
void sound_tick(void)
{
    if (s_is_open == -1) {
        return;
    }
    audio_voices_collect(s_free, &s_free_count);
}
