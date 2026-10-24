/*
 * audio.c - the mixer.
 *
 * The platform calls mix() on its audio thread. The CD music is a
 * stream: a ring of samples the game thread fills from the disc image
 * (cd.c) and the audio thread plays. The game thread hands over a new
 * stream through s_cd_next; the audio thread picks it up, and the game
 * thread frees what the audio thread let go of.
 */

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "sound.h"
#include "platform/platform.h"

/* Four seconds of 16 bit stereo: enough that a frame's wait, or a slow
 * read, does not run it dry. */
enum { RING_FRAMES = 4 * AUDIO_RATE };

struct cd_stream {
    _Atomic u32 r, w;           /* frames read and written, ever */
    atomic_int ended;           /* no more will be written */
    s16 pcm[RING_FRAMES * 2];
};

static _Atomic(struct cd_stream *) s_cd_next;  /* game to audio */
static _Atomic(struct cd_stream *) s_cd_done;  /* audio to game, to free */
static struct cd_stream *s_cd;                 /* audio thread only */
static atomic_int s_cd_playing;
static atomic_int s_cd_mute;
static int s_open;

enum { MAX_VOICES = 64 };

static struct sound_voice *s_voices;
static int s_voice_count;
static int s_channels;
static atomic_int s_active[MAX_VOICES];
static atomic_int s_master = 0x10000;

/* Handed over in place of a stream: the music stops. */
static struct cd_stream s_stop;

static void stream_free(struct cd_stream *s)
{
    if (s != &s_stop) {
        free(s);
    }
}

static void collect(void)
{
    stream_free(atomic_exchange(&s_cd_done, NULL));
}

/* How loud a voice is now, as 0x467240 ranks them. */
static s32 loudness(const struct sound_voice *v)
{
    const struct sound_sample *s = v->sample;
    int at = s->length > 0 ? v->pos * 64 / s->length : 0;

    if (at > 63) {
        at = 63;
    }
    return ((s32)s->envelope[at] * v->rate >> s->shift) * (v->volume >> 10) / 256 * (v->pan + 128);
}

static float sample_at(const struct sound_sample *s, s32 pos)
{
    return s->bits == 8 ? ((const s8 *)s->data)[pos] * (1.0f / 128.0f)
                        : ((const s16 *)s->data)[pos] * (1.0f / 32768.0f);
}

/* A voice past the channels plays on unheard (0x465d90): it moves on by
 * the frames' worth at its rate, loops, and stops at its end. */
static void skip(struct sound_voice *v, s32 frames)
{
    const struct sound_sample *s = v->sample;
    u64 step = ((u64)(u32)v->rate << 16) / AUDIO_RATE * (u64)frames + v->frac;

    v->pos += (s32)(step >> 16);
    v->frac = (u32)(step & 0xffff);
    while (v->loops != 0 && s->loop_length > 0 && v->pos >= s->loop_start + s->loop_length) {
        v->pos -= s->loop_length;
        v->loops--;
    }
    if (v->pos >= s->length) {
        v->rate = 0;
    }
}

/* Adds the loudest voices into `out`, stepping each at its rate and
 * looping as many times as asked (0x467180). */
static void mix_voices(float *out, s32 frames)
{
    int chosen[MAX_VOICES], n = 0, i, k;
    float master = atomic_load(&s_master) * (1.0f / 65536.0f);

    for (i = 0; i < s_voice_count; i++) {
        const struct sound_voice *v = &s_voices[i];

        if (atomic_load(&s_active[i]) && v->rate > 0 && v->sample != NULL) {
            chosen[n++] = i;
        }
    }
    while (n > s_channels) {
        int quiet = 0;

        for (k = 1; k < n; k++) {
            if (loudness(&s_voices[chosen[k]]) < loudness(&s_voices[chosen[quiet]])) {
                quiet = k;
            }
        }
        skip(&s_voices[chosen[quiet]], frames);
        chosen[quiet] = chosen[--n];
    }
    for (k = 0; k < n; k++) {
        struct sound_voice *v = &s_voices[chosen[k]];
        const struct sound_sample *s = v->sample;
        float gain = v->volume * (1.0f / 65536.0f) * master;
        float left = gain * (v->pan > 0 ? (128 - v->pan) / 128.0f : 1.0f);
        float right = gain * (v->pan < 0 ? (128 + v->pan) / 128.0f : 1.0f);
        u32 step = (u32)(((u64)(u32)v->rate << 16) / AUDIO_RATE);

        for (i = 0; i < frames && v->rate > 0; i++) {
            float x;
            u32 frac;

            if (v->loops != 0 && v->pos >= s->loop_start + s->loop_length) {
                v->pos -= s->loop_length;
                v->loops--;
            }
            if (v->pos >= s->length) {
                v->rate = 0;
                break;
            }
            x = sample_at(s, v->pos);
            out[2 * i] += x * left;
            out[2 * i + 1] += x * right;
            frac = v->frac + step;
            v->pos += (s32)(frac >> 16);
            v->frac = frac & 0xffff;
        }
    }
}

static void mix(float *out, s32 frames, void *user)
{
    struct cd_stream *next = atomic_exchange(&s_cd_next, NULL);
    s32 i;

    (void)user;
    if (next != NULL) {
        struct cd_stream *old = s_cd;

        s_cd = next != &s_stop ? next : NULL;
        if (old != NULL) {
            stream_free(atomic_exchange(&s_cd_done, old));
        }
    }
    for (i = 0; i < frames; i++) {
        out[2 * i] = out[2 * i + 1] = 0.0f;
    }
    if (s_cd != NULL) {
        u32 rd = atomic_load(&s_cd->r), avail = atomic_load(&s_cd->w) - rd;
        int mute = atomic_load(&s_cd_mute);

        for (i = 0; i < frames && avail > 0; i++, rd++, avail--) {
            const s16 *p = s_cd->pcm + 2 * (rd % RING_FRAMES);

            if (!mute) {
                out[2 * i] = p[0] * (1.0f / 32768.0f);
                out[2 * i + 1] = p[1] * (1.0f / 32768.0f);
            }
        }
        atomic_store(&s_cd->r, rd);
        if (avail == 0 && atomic_load(&s_cd->ended)) {
            atomic_store(&s_cd_playing, 0);
        }
    }
    if (s_voices != NULL) {
        mix_voices(out, frames);
    }
}

void audio_voices_set(struct sound_voice *voices, int count, int channels)
{
    int i;

    for (i = 0; i < MAX_VOICES; i++) {
        atomic_store(&s_active[i], 0);
    }
    s_voice_count = count < MAX_VOICES ? count : MAX_VOICES;
    s_channels = channels;
    s_voices = voices;
}

void audio_voice_start(struct sound_voice *v)
{
    atomic_store(&s_active[v - s_voices], 1);
}

void audio_voices_volume(s32 volume)
{
    atomic_store(&s_master, volume);
}

void audio_voices_collect(struct sound_voice **free_list, int *free_count)
{
    int i;

    for (i = 0; i < s_voice_count; i++) {
        struct sound_voice *v = &s_voices[i];

        if (atomic_load(&s_active[i]) && v->rate == 0 && v->keep != 1) {
            atomic_store(&s_active[i], 0);
            free_list[(*free_count)++] = v;
        }
    }
}

int audio_init(void)
{
    if (!s_open) {
        s_open = plat_audio_open(AUDIO_RATE, mix, NULL);
    }
    return s_open;
}

void audio_shutdown(void)
{
    if (s_open) {
        plat_audio_close();
        s_open = 0;
    }
    stream_free(s_cd);
    s_cd = NULL;
    stream_free(atomic_exchange(&s_cd_next, NULL));
    collect();
}

static void hand_over(struct cd_stream *s)
{
    collect();
    atomic_store(&s_cd_playing, s != NULL);
    stream_free(atomic_exchange(&s_cd_next, s != NULL ? s : &s_stop));
}

struct cd_stream *audio_cd_open(void)
{
    struct cd_stream *s;

    if (!s_open || (s = calloc(1, sizeof(*s))) == NULL) {
        return NULL;
    }
    hand_over(s);
    return s;
}

u32 audio_cd_room(const struct cd_stream *s)
{
    return RING_FRAMES - (atomic_load(&s->w) - atomic_load(&s->r));
}

void audio_cd_write(struct cd_stream *s, const s16 *pcm, u32 frames)
{
    u32 w = atomic_load(&s->w), k;

    for (k = 0; k < frames; k++, w++) {
        s16 *p = s->pcm + 2 * (w % RING_FRAMES);

        p[0] = pcm[2 * k];
        p[1] = pcm[2 * k + 1];
    }
    atomic_store(&s->w, w);
}

void audio_cd_end(struct cd_stream *s)
{
    atomic_store(&s->ended, 1);
}

void audio_cd_stop(void)
{
    if (s_open) {
        hand_over(NULL);
    }
}

int audio_cd_playing(void)
{
    collect();
    return atomic_load(&s_cd_playing);
}

void audio_cd_mute(int mute)
{
    atomic_store(&s_cd_mute, mute);
}
