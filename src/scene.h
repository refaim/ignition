/*
 * scene.h - the 3D engine's scene: the camera record, the objects and
 * the grid they are kept in (0x4466d0..0x447280).
 *
 * The original transforms, sorts and rasterises the objects' faces in
 * software. Here the camera, objects and meshes are the original's, and
 * gl3d.c draws the meshes with OpenGL.
 *
 * World Y is up; a mesh's Y points down. Angles are in tenths of a
 * degree. Yaw 0 looks along +Z and 900 along +X; positive pitch looks
 * down.
 */

#ifndef IGN_SCENE_H
#define IGN_SCENE_H

#include <stddef.h>

#include "types.h"

/* The camera and the pipeline's switches, 0xa8 bytes (0x63c5f0). */
struct scene {
    double x, y, z;             /* +0x00 */
    double pitch, yaw, roll;    /* +0x18 */
    double zoom;                /* +0x30, scales X only; <= 0 mirrors */
    s32 stage_lookat;           /* +0x38: aim at target */
    s32 stage_gather;           /* +0x3c: find the objects in the grid */
    s32 stage_transform;        /* +0x40 */
    s32 f44, f48, f4c;          /* +0x44 */
    s32 stage_emit;             /* +0x50 */
    s32 stage_collect;          /* +0x54 */
    s32 perspective;            /* +0x58 */
    s32 vert_count;             /* +0x5c */
    s32 obj_count;              /* +0x60, in the visible list */
    s32 obj_count_all;          /* +0x64 */
    s32 poly_count;             /* +0x68 */
    s32 f6c;                    /* +0x6c */
    s32 target_x, target_y, target_z;   /* +0x70 */
    s32 target_roll;            /* +0x7c */
    s32 focal_x, focal_y;       /* +0x80, 253 and 207 at 320 x 200 */
    s32 near_ofs;               /* +0x88 */
    s32 f8c, f90, f94, f98;     /* +0x8c */
    s32 center_x, center_y;     /* +0x9c */
    s32 gather_radius;          /* +0xa4, in cells; 0 for the disc */
};

_Static_assert(offsetof(struct scene, zoom) == 0x30, "scene");
_Static_assert(offsetof(struct scene, stage_emit) == 0x50, "scene");
_Static_assert(offsetof(struct scene, obj_count) == 0x60, "scene");
_Static_assert(offsetof(struct scene, target_x) == 0x70, "scene");
_Static_assert(offsetof(struct scene, focal_x) == 0x80, "scene");
_Static_assert(offsetof(struct scene, center_x) == 0x9c, "scene");
_Static_assert(sizeof(struct scene) == 0xa8, "scene");

/* An object, 0x2a bytes in the original (0x63c5dc). */
struct scene_obj {
    struct scene_obj *self;     /* +0x00, the cell list's check */
    const s32 *mesh;            /* +0x04 */
    s32 tex;                    /* +0x08, a texture slot */
    s32 x, y, z;                /* +0x0c */
    s16 ax, ay, az;             /* +0x18 */
    s16 klass;                  /* +0x1e, a depth bias from 100 */
    s16 f20;                    /* +0x20 */
    s16 radius;                 /* +0x22 */
    s16 group;                  /* +0x24, 0 always drawn */
    struct scene_obj *next;     /* +0x26, in its grid cell */
};

/* What the game keeps for an object (0x20 bytes). */
struct scene_desc {
    struct scene_obj *obj;      /* +0x00 */
    s32 x, y, z;                /* +0x04 */
    s32 ax, ay, az;             /* +0x10 */
    s32 in_grid;                /* +0x1c */
};

extern struct scene *g_scene;               /* 0x63c5f0 */
extern struct scene_obj **g_scene_visible;  /* 0x63c5cc, obj_count of them */

struct scene *scene_create(void);
void scene_free(void);
int raster_set_grid(int w, int h, int count);
void scene_set_screen(int w, int h);
/* 0x402be0: the rasteriser's screen (0x44f070); nothing to do here. */
int raster_set_screen(int w, int h, int flags);
int scene_obj_create(struct scene_desc *desc, const s32 *mesh, int tex, int klass,
                     int f20, int radius, int group);
int scene_obj_link(struct scene_desc *desc);
int scene_obj_move(struct scene_desc *desc);
int scene_obj_unlink(struct scene_desc *desc);
int scene_obj_set_mesh(struct scene_desc *desc, const s32 *mesh, int tex, int klass,
                       int f20, int radius, int group);
void scene_look_at(struct scene *sc);

/* Texture slots (0x63c5c8): each the first of the slot's 256 x 256
 * pages of 8 bit texels, which follow at 64 KB steps. */
enum { SCENE_TEXTURES = 1000 };
extern u8 *g_scene_textures[SCENE_TEXTURES];
extern int g_scene_texture_pages[SCENE_TEXTURES];

/* Rows of the camera's rotation, from its angles (0x449e70): view
 * coordinates are rows . (world - camera). */
void scene_view_rows(const struct scene *sc, double rows[3][3]);
/* An object's rotation of its mesh into the world, Y flipped
 * (0x44a900). */
void scene_obj_rows(const struct scene_obj *o, double rows[3][3]);

#endif /* IGN_SCENE_H */
