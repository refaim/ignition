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

/* Plays `frames` of 16 bit stereo; the mixer takes the buffer and frees
 * it when it is replaced or stopped. */
void audio_cd_play(s16 *pcm, u32 frames);
void audio_cd_stop(void);
int audio_cd_playing(void);
void audio_cd_mute(int mute);

#endif /* IGN_AUDIO_H */
