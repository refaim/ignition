/*
 * cd.c - CD audio, as the game drives it through MCI (0x457b10..
 * 0x4581b0), played from the disc image.
 *
 * The game asks MCI for the disc's length, its number of tracks and
 * where each track starts, in frames of 1/75 s, and plays a track from
 * its start for its length. Here the answers come from the CUE sheet and
 * the tracks play through the mixer. MCI's CD volume is only on or off;
 * so is this one.
 */

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "audio.h"
#include "cd.h"
#include "cdimage.h"
#include "stubs.h"
#include "vfs.h"

s32 g_cd_present;               /* 0x50ef4c */
s32 g_cd_track_count;           /* 0x50f070 */
/* Track starts from track 1 (0x50f074, index 0 for track 1), and the
 * disc's end after the last; the original stores the starts after its
 * count, so start[t] is at 0x50f070 + 4 t. */
s32 g_cd_start[101];
/* The disc's length, then each track's length from track 1 (0x50f2cc). */
s32 g_cd_tracks[101];

static char s_cue[1024];
static int s_open;
static int s_on = 1;
static int s_playing_track;

/* Finds a .cue next to the data folder (the image's folder holds the
 * extracted disc as disc/). */
static void find_cue(void)
{
    char dir[1024];
    char *slash;
    DIR *d;
    struct dirent *e;

    if (s_cue[0] != '\0') {
        return;
    }
    snprintf(dir, sizeof(dir), "%s", vfs_root());
    slash = strrchr(dir, '/');
    if (slash == NULL) {
        return;
    }
    *slash = '\0';
    d = opendir(dir);
    if (d == NULL) {
        return;
    }
    while ((e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name);

        /* "._" files are macOS's notes on a copied file, not sheets. */
        if (n > 4 && strncmp(e->d_name, "._", 2) != 0
            && strcasecmp(e->d_name + n - 4, ".cue") == 0) {
            snprintf(s_cue, sizeof(s_cue), "%s/%s", dir, e->d_name);
            break;
        }
    }
    closedir(d);
}

void cd_set_image(const char *cue)
{
    snprintf(s_cue, sizeof(s_cue), "%s", cue);
}

/* 0x458040 */
int cd_open(void)
{
    find_cue();
    if (s_open) {
        return 1;
    }
    if (s_cue[0] == '\0' || !cdimage_open(s_cue, vfs_root())) {
        return 0;
    }
    audio_init();
    s_open = 1;
    s_on = 1;
    return 1;
}

/* 0x4580a0 */
int cd_close(void)
{
    audio_cd_stop();
    cdimage_close();
    s_open = 0;
    return 1;
}

/* 0x457b10 */
int cd_is_playing(void)
{
    return s_open && audio_cd_playing();
}

/* 0x457b50
 * The original also reads where the disc is playing; the game only uses
 * whether a disc is there. */
int cd_read_status(void)
{
    g_cd_present = s_open && cdimage_track_count() > 0;
    return g_cd_present;
}

/* 0x457cf0: 0 switches CD audio off, 1..255 on. */
int cd_set_volume(int volume)
{
    if (volume < 0 || volume > 0xff) {
        return 0;
    }
    s_on = volume != 0;
    audio_cd_mute(!s_on);
    return s_open;
}

/* 0x457da0
 * Track 1 is the data track; as the original does, its start is moved
 * to track 2's, so only audio is ever played. */
int cd_read_tracks(void)
{
    int n, t;

    if (!s_open) {
        return 0;
    }
    n = cdimage_track_count();
    if (n > 100) {
        n = 100;
    }
    g_cd_track_count = n;
    g_cd_tracks[0] = cdimage_disc_frames();
    for (t = 1; t <= n; t++) {
        g_cd_start[t - 1] = cdimage_track_start(cdimage_track_number(t - 1));
    }
    g_cd_start[n] = g_cd_start[0] + g_cd_tracks[0];
    for (t = 1; t <= n; t++) {
        g_cd_tracks[t] = g_cd_start[t] - g_cd_start[t - 1];
    }
    if (n > 1) {
        g_cd_start[0] = g_cd_start[1];
    }
    return 1;
}

/* 0x457ed0 */
int cd_play_track(int track)
{
    s16 *pcm;
    u32 frames;

    if (track > g_cd_track_count || track < 1 || !s_open) {
        return 0;
    }
    pcm = cdimage_read_audio(track, &frames);
    if (pcm == NULL) {
        return 0;
    }
    s_playing_track = track;
    audio_cd_play(pcm, frames);
    audio_cd_mute(!s_on);
    return 1;
}

/* 0x457fc0 */
int cd_stop_play(void)
{
    audio_cd_stop();
    s_playing_track = 0;
    return 1;
}
