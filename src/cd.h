/*
 * cd.h - CD audio, played from the disc image.
 */

#ifndef IGN_CD_H
#define IGN_CD_H

#include "types.h"

extern s32 g_cd_present;
extern s32 g_cd_track_count;
extern s32 g_cd_start[101];
extern s32 g_cd_tracks[101];

/* The CUE sheet to play from; without one, cd_open looks next to the
 * data folder. */
void cd_set_image(const char *cue);

int cd_open(void);
int cd_close(void);
int cd_is_playing(void);
int cd_read_status(void);
int cd_set_volume(int volume);
int cd_read_tracks(void);
int cd_play_track(int track);
int cd_stop_play(void);

#endif /* IGN_CD_H */
