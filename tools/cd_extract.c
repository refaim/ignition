/*
 * cd_extract.c - copy the files and audio tracks out of a BIN/CUE image.
 *
 * Every file of the ISO 9660 data track goes to <out>/, laid out as on
 * the disc. Every audio track goes to <out>/audio/trackNN.wav as 16 bit
 * 44.1 kHz stereo, numbered as on the CD.
 *
 *   cd_extract <image.cue> <out>
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "cdimage.h"
#include "compat.h"

static void put16(FILE *f, unsigned v)
{
    fputc((int)(v & 255), f);
    fputc((int)(v >> 8 & 255), f);
}

static void put32(FILE *f, u32 v)
{
    put16(f, v & 0xFFFF);
    put16(f, v >> 16);
}

static int write_wav(const char *path, const short *pcm, u32 frames)
{
    FILE *f = fopen(path, "wb");
    u32 bytes = frames * 4;
    int ok;

    if (f == NULL) {
        perror(path);
        return 0;
    }
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + bytes);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);            /* PCM */
    put16(f, 2);            /* stereo */
    put32(f, 44100);
    put32(f, 44100 * 4);
    put16(f, 4);
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32(f, bytes);
    ok = fwrite(pcm, 4, frames, f) == frames;
    return fclose(f) == 0 && ok;
}

/* A whole audio track as 16 bit stereo, malloc'd. */
static short *read_track(int track, u32 *frames)
{
    const char *file;
    long offset;
    int big;
    short *pcm;
    FILE *fp;
    size_t got, k;

    if (!cdimage_audio_track(track, &file, &offset, frames, &big)) {
        return NULL;
    }
    pcm = malloc((size_t)*frames * 4);
    fp = fopen(file, "rb");
    if (pcm == NULL || fp == NULL || fseek(fp, offset, SEEK_SET) != 0) {
        free(pcm);
        if (fp != NULL) {
            fclose(fp);
        }
        return NULL;
    }
    got = fread(pcm, 4, *frames, fp);
    fclose(fp);
    *frames = (u32)got;
    if (big) {
        unsigned char *b = (unsigned char *)pcm;

        for (k = 0; k + 1 < got * 4; k += 2) {
            unsigned char t = b[k];

            b[k] = b[k + 1];
            b[k + 1] = t;
        }
    }
    return pcm;
}

int main(int argc, char **argv)
{
    char path[1024];
    int i, files;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <image.cue> <out>\n", argv[0]);
        return 1;
    }
    mkdir(argv[2], 0755);
    if (!cdimage_open(argv[1], argv[2])) {
        fprintf(stderr, "%s: cannot open the image\n", argv[1]);
        return 1;
    }
    files = cdimage_extract_all();
    printf("%d files\n", files);

    snprintf(path, sizeof(path), "%s/audio", argv[2]);
    for (i = 0; i < cdimage_track_count(); i++) {
        int track = cdimage_track_number(i);
        short *pcm;
        u32 frames;

        if (!cdimage_track_is_audio(track)) {
            continue;
        }
        pcm = read_track(track, &frames);
        if (pcm == NULL) {
            fprintf(stderr, "track %02d: read failed\n", track);
            continue;
        }
        mkdir(path, 0755);
        snprintf(path, sizeof(path), "%s/audio/track%02d.wav", argv[2], track);
        if (write_wav(path, pcm, frames)) {
            printf("track %02d: %u:%02u\n", track,
                   frames / 44100 / 60, frames / 44100 % 60);
        }
        free(pcm);
        snprintf(path, sizeof(path), "%s/audio", argv[2]);
    }
    cdimage_close();
    return 0;
}
