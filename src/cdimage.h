/*
 * cdimage.h - game data and CD audio read from a BIN/CUE disc image.
 *
 * The CUE sheet lists the tracks; the BIN files hold raw 2352 byte
 * sectors (or 2048 byte data sectors). The data track carries an
 * ISO 9660 file system with the game files, and audio tracks are 16 bit
 * 44.1 kHz stereo samples.
 */

#ifndef IGN_CDIMAGE_H
#define IGN_CDIMAGE_H

#include <stddef.h>

#include "types.h"

/* Opens a CUE sheet and indexes the file system of its first data track.
 * Returns 1 on success. Files found in the image are copied on first use
 * into `cache_dir` (the data folder), laid out as on the disc. */
int cdimage_open(const char *cue_path, const char *cache_dir);
void cdimage_close(void);
int cdimage_is_open(void);

/* Copies `name` (a path relative to the disc root, any
 * case, '/' separated) into the cache and writes its path to `out`.
 * Returns 1 when the disc has the file. */
int cdimage_resolve(const char *name, char *out, size_t out_size);

/* A file on the disc, read into memory without
 * copying it out (malloc'd; the caller frees). NULL when it is missing. */
void *cdimage_read_file(const char *name, u32 *size);

/* Copies every file on the disc into the cache. Returns the number of files copied. */
int cdimage_extract_all(void);

/* Number of tracks in the CUE sheet, and whether track `track` is audio. */
int cdimage_track_count(void);
int cdimage_track_number(int i);
int cdimage_track_is_audio(int track);

/* Where track `track` (1 based) starts on the disc, and the disc's
 * length, in frames of 1/75 s from the first track. */
s32 cdimage_track_start(int track);
s32 cdimage_disc_frames(void);

/* Audio track `track` (1 based, as on the disc) as 16 bit stereo samples
 * at 44.1 kHz. Returns a malloc'd buffer the caller frees, or NULL when
 * the track is missing or not audio. `frames` receives the frame count. */
short *cdimage_read_audio(int track, u32 *frames);

#endif /* IGN_CDIMAGE_H */
