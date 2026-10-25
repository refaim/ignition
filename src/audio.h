/*
 * audio.h - the mixer behind the platform's audio output: the CD music
 * stream and the sound effect voices, at 44.1 kHz stereo.
 */

#ifndef IGN_AUDIO_H
#define IGN_AUDIO_H

#include "types.h"

enum { AUDIO_RATE = 44100 };

/* Opens the platform output. Returns 0 when there is none. */
int audio_init(void);
void audio_shutdown(void);

/* The CD music as a stream of 16 bit stereo. audio_cd_open starts a new
 * one in place of the last, which the mixer frees; the game thread fills
 * it with audio_cd_write, up to audio_cd_room frames at a time, and
 * audio_cd_end says it is complete. audio_cd_stop ends the music. */
struct cd_stream;
struct cd_stream *audio_cd_open(void);
u32 audio_cd_room(const struct cd_stream *s);
void audio_cd_write(struct cd_stream *s, const s16 *pcm, u32 frames);
void audio_cd_end(struct cd_stream *s);
void audio_cd_stop(void);
int audio_cd_playing(void);
void audio_cd_mute(int mute);
/* The music's gain, 0 to 1. */
void audio_cd_gain(float gain);

struct sound_voice;

/* The sound voices the mixer plays from: at most `channels` at once,
 * the loudest. */
void audio_voices_set(struct sound_voice *voices, int count, int channels);
void audio_voice_start(struct sound_voice *v);
void audio_voices_volume(s32 volume);
/* Puts voices that stopped, and are not kept, on the free list. */
void audio_voices_collect(struct sound_voice **free_list, int *free_count);

#endif /* IGN_AUDIO_H */
