/*
 * main.c - entry point. WinMain 0x4120a0.
 *
 *   ignition [--data DIR] [--view FILE.PIC]
 *
 * --data is the folder with the game files, the disc root as extracted
 * by `make extract`. --view shows one picture through the renderer, with
 * its own palette, and exits on Escape.
 */

#include <stdio.h>
#include <string.h>

#include "app.h"
#include "dik.h"
#include "lisa/file.h"
#include "lisa/log.h"
#include "lisa/mem.h"
#include "pic.h"
#include "platform.h"
#include "vfs.h"
#include "video.h"

#define DEFAULT_DATA "data/Ignition (USA) (En,Es,It,Sv) (Rerelease)/disc"

static int view_frame(void *user)
{
    (void)user;
    if (g_plat_keys[DIK_ESCAPE]) {
        return 0;
    }
    video_present();
    return 1;
}

static int view(const char *name)
{
    const u8 *pic;
    int y, w, h;

    mem_init();
    pic = file_load(name);
    if (pic == NULL || pic_u16(pic, 4) != PIC_MAGIC) {
        fprintf(stderr, "%s: not a picture (error 0x%x)\n", name, g_file_error);
        return 1;
    }
    w = pic_width(pic);
    h = pic_height(pic);
    printf("%s: %d x %d\n", name, w, h);
    if (!video_create_window()) {
        return 1;
    }
    g_video_mode.width = w;
    g_video_mode.height = h;
    g_video_mode.bpp = 8;
    g_video_mode.flag = 1;
    if (!video_set_mode()) {
        return 1;
    }
    for (y = 0; y < h; y++) {
        memcpy(g_video_screen + y * w, pic_pixels(pic) + y * w, (size_t)w);
    }
    memcpy(g_video_palette, pic_palette(pic), sizeof(g_video_palette));
    return plat_run_with(view_frame, NULL);
}

int main(int argc, char **argv)
{
    const char *data = DEFAULT_DATA;
    const char *view_name = NULL;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            data = argv[++i];
        } else if (strcmp(argv[i], "--view") == 0 && i + 1 < argc) {
            view_name = argv[++i];
        } else {
            fprintf(stderr, "usage: %s [--data DIR] [--view FILE.PIC]\n", argv[0]);
            return 1;
        }
    }
    vfs_set_root(data);
    if (view_name != NULL) {
        return view(view_name);
    }
    if (!app_startup()) {
        return 0;
    }
    return plat_run();
}
