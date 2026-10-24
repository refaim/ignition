/*
 * texture.c - Lisa 2 textures.
 */

#include <stdlib.h>
#include <string.h>

#include "canvas.h"
#include "lisa/texture.h"

enum { TEX_GROW = 1000 };

/* The table: next free id, capacity, slots (0x520380, 0x520388,
 * 0x52038c). Id 0 is never handed out. */
static s32 s_hint;
static s32 s_count;
static struct tex_desc **s_slots;
static struct tex_desc s_default;   /* 0x51fb88 */

/* 0x4611d0 */
void tex_init(void)
{
    memset(&s_default, 0, sizeof(s_default));
    s_default.name = "default";
    s_hint = 1;
}

/* 0x45f560 */
static void grow(void)
{
    struct tex_desc **slots = calloc((size_t)(s_count + TEX_GROW), sizeof(*slots));

    if (s_slots != NULL) {
        memcpy(slots, s_slots, (size_t)s_count * sizeof(*slots));
    }
    free(s_slots);
    s_slots = slots;
    s_count += TEX_GROW;
}

/* 0x460480 */
static char *dup_name(const char *name)
{
    char *copy;
    size_t n;

    if (name == NULL) {
        return NULL;
    }
    n = strlen(name) + 1;
    copy = malloc(n);
    memcpy(copy, name, n);
    return copy;
}

static void release(struct tex_desc *t)
{
    /* The GPU's copy is keyed by the pixels' address, which malloc may
     * hand out again for the next sprite. */
    canvas_forget(t->pixels);
    free(t->name);
    free(t->pixels);
}

/* 0x461530, without the pages. The original skips a texture outside
 * 1..256 in either size and leaves its pixel pointer as given; here it
 * has no pixels. */
static void copy_pixels(struct tex_desc *t, const struct tex_desc *src)
{
    int y;

    t->pixels = NULL;
    if (t->width <= 0 || t->width > 256 || t->height <= 0 || t->height > 256) {
        return;
    }
    t->pixels = malloc((size_t)t->width * (size_t)t->height);
    for (y = 0; y < t->height; y++) {
        memcpy(t->pixels + y * t->width, src->pixels + y * src->stride, (size_t)t->width);
    }
    t->stride = t->width;
}

/* 0x461360 */
int obj_create(const struct tex_desc *desc, int id)
{
    struct tex_desc *t;

    if (desc != NULL && id == 0) {
        int next;

        while (s_count <= s_hint) {
            grow();
        }
        id = s_hint;
        for (next = id + 1; next < s_count && s_slots[next] != NULL; next++) {
        }
        s_hint = next;
    } else if (desc == NULL) {
        if (id > 0 && id < s_count) {
            if (s_slots[id] != NULL) {
                release(s_slots[id]);
            }
            if (s_hint > id) {
                s_hint = id;
            }
            free(s_slots[id]);
            s_slots[id] = NULL;
        }
        return 0;
    } else if (id < 0 || id >= s_count) {
        return 0;
    }
    t = s_slots[id];
    if (t == NULL) {
        t = malloc(sizeof(*t));
        s_slots[id] = t;
    } else {
        release(t);
    }
    *t = *desc;
    t->name = dup_name(desc->name);
    copy_pixels(t, desc);
    return id;
}

/* 0x4612e0 */
int tex_get_info(struct tex_desc *out, int id)
{
    if (id <= 0 || id >= s_count || s_slots[id] == NULL) {
        *out = s_default;
        return id;
    }
    *out = *s_slots[id];
    out->hot_x = 0;
    out->hot_y = 0;
    return 0;
}
