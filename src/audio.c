/*
 * audio.c - the mixer.
 *
 * The platform calls mix() on its audio thread. The game thread hands
 * over a new CD buffer through s_cd_next; the audio thread picks it up,
 * and the game thread frees what the audio thread let go of.
 */

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "platform.h"

struct cd_stream {
    s16 *pcm;
    u32 frames;
};

static _Atomic(struct cd_stream *) s_cd_next;  /* game to audio */
static _Atomic(struct cd_stream *) s_cd_done;  /* audio to game, to free */
static struct cd_stream *s_cd;                 /* audio thread only */
static u32 s_cd_pos;                           /* audio thread only */
static atomic_int s_cd_playing;
static atomic_int s_cd_mute;
static int s_open;

static void stream_free(struct cd_stream *s)
{
    if (s != NULL) {
        free(s->pcm);
        free(s);
    }
}

static void collect(void)
{
    stream_free(atomic_exchange(&s_cd_done, NULL));
}

static void mix(float *out, s32 frames, void *user)
{
    struct cd_stream *next = atomic_exchange(&s_cd_next, NULL);
    s32 i;

    (void)user;
    if (next != NULL) {
        struct cd_stream *old = s_cd;

        s_cd = next->pcm != NULL ? next : NULL;
        s_cd_pos = 0;
        if (next->pcm == NULL) {
            stream_free(next);
        }
        if (old != NULL) {
            stream_free(atomic_exchange(&s_cd_done, old));
        }
    }
    for (i = 0; i < frames; i++) {
        float l = 0.0f, r = 0.0f;

        if (s_cd != NULL && s_cd_pos < s_cd->frames) {
            if (!atomic_load(&s_cd_mute)) {
                l = s_cd->pcm[2 * s_cd_pos] * (1.0f / 32768.0f);
                r = s_cd->pcm[2 * s_cd_pos + 1] * (1.0f / 32768.0f);
            }
            if (++s_cd_pos == s_cd->frames) {
                atomic_store(&s_cd_playing, 0);
            }
        }
        out[2 * i] = l;
        out[2 * i + 1] = r;
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

static void hand_over(s16 *pcm, u32 frames)
{
    struct cd_stream *s = malloc(sizeof(*s));

    collect();
    s->pcm = pcm;
    s->frames = frames;
    atomic_store(&s_cd_playing, pcm != NULL && frames > 0);
    stream_free(atomic_exchange(&s_cd_next, s));
}

void audio_cd_play(s16 *pcm, u32 frames)
{
    if (!s_open) {
        free(pcm);
        return;
    }
    hand_over(pcm, frames);
}

void audio_cd_stop(void)
{
    if (s_open) {
        hand_over(NULL, 0);
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
