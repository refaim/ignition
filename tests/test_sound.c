/*
 * test_sound.c - loads the race's sound groups, one of each sample
 * format, and checks the records against the files' headers.
 */

#include <stdio.h>
#include <stdlib.h>

#include "platform/common.h"
#include "sound.h"
#include "vfs.h"

#define DEFAULT_DATA "data/Ignition/disc"

static int s_failed;

#define CHECK(cond) do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
            s_failed = 1; \
        } \
    } while (0)

/* A decoded waveform changes less from one sample to the next than it
 * is far from zero; a wrongly signed or undecoded one does not. */
static int smooth(const struct sound_sample *s)
{
    double level = 0.0, change = 0.0;
    int i;

    for (i = 1; i < s->length; i++) {
        int a, b;

        if (s->bits == 8) {
            a = ((const s8 *)s->data)[i];
            b = ((const s8 *)s->data)[i - 1];
        } else {
            a = ((const s16 *)s->data)[i];
            b = ((const s16 *)s->data)[i - 1];
        }
        level += abs(a);
        change += abs(a - b);
    }
    return change < level;
}

int main(int argc, char **argv)
{
    const struct sound_sample *s;

    vfs_set_root(argc > 1 ? argv[1] : plat_find_data(DEFAULT_DATA));

    CHECK(sound_load_group("LEVELS\\CANADA\\SOUND", 1, 0) == 1);
    s = sound_sample_of(1, 0, 4);       /* 04_WATER.PAT */
    CHECK(s != NULL);
    if (s != NULL) {
        CHECK(s->bits == 8);
        CHECK(s->rate == 11025);
        CHECK(s->length == 0x31b8);
        CHECK(s->loop_start == 0);
        CHECK(s->loop_length == s->length);
        CHECK(smooth(s));
    }
    s = sound_sample_of(1, 0, 0);       /* 00_RAIN.WAV */
    CHECK(s != NULL);

    CHECK(sound_load_group("GENERAL\\SOUND\\ROLL", 0, 1) == 1);
    s = sound_sample_of(0, 1, 0);       /* 00_R_ASF.XI */
    CHECK(s != NULL);
    if (s != NULL) {
        CHECK(s->rate == 22050);
        CHECK(s->length > 0);
        CHECK(smooth(s));
        printf("00_R_ASF.XI: %d bit, %d samples, loop %d+%d\n",
               s->bits, s->length, s->loop_start, s->loop_length);
    }

    sound_close();
    if (s_failed) {
        return 1;
    }
    printf("ok\n");
    return 0;
}
