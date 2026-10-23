/*
 * anim_tex.c - the animated textures (0x401850..0x402210): water, signs
 * and the like, made by moving the texture coordinates of the faces
 * that use them.
 *
 * An animation is a mesh (a placement of type 250) whose leading faces
 * are its key frames: each the corner of a 64 x 64 cell of a texture
 * page. Every face of the world's objects that lies in a key's cell is
 * listed under that key; each step moves each list on by one key, all
 * of a face's coordinates by the difference between the keys' cells.
 */

#include <stdlib.h>

#include "anim_tex.h"
#include "gl3d.h"
#include "scene.h"

/* A textured face, 0x2c bytes: type, three vertices, (u, v) for each in
 * 16.16 of a page, then the page in the high half of +0x28. */
enum { FACE_BYTES = 0x2c, CELL = 0x4000 };

struct anim {
    s32 keys;                   /* +0x00 */
    s32 next;                   /* +0x04, the key frame being moved to */
    s32 cur;                    /* +0x08 */
    s32 (*uv)[2];               /* +0x0c, each key's cell */
    s32 *page;                  /* +0x10, and page word */
    s32 ***faces;               /* +0x14, each key's faces, 0 ended */
    /* The port's: the meshes those faces are in, for gl3d's cache. */
    const s32 **meshes;
    int mesh_count;
};

static struct anim **s_anims;   /* 0x47c044 */
static int s_count;             /* 0x47c048 */

/* Whether a face is textured: types 17, 18 and 19 (not 20). */
static int textured(const u8 *f)
{
    int type = f[0];

    return type >= 0x11 && type != 0x14;
}

static const u8 *first_face(const s32 *mesh)
{
    return (const u8 *)(mesh + 2 + 3 * mesh[0]);
}

/* 0x402210: one vertex of the face in the cell. */
static int in_cell(s32 face_page, s32 u, s32 v, s32 page, s32 u0, s32 v0, s32 u1, s32 v1)
{
    return face_page == page && u >= u0 && u <= u1 && v >= v0 && v <= v1;
}

/* 0x402170: all three. */
static int face_in_cell(const s32 *w, s32 page, s32 u0, s32 v0, s32 u1, s32 v1)
{
    return in_cell(w[10], w[4], w[5], page, u0, v0, u1, v1)
        && in_cell(w[10], w[6], w[7], page, u0, v0, u1, v1)
        && in_cell(w[10], w[8], w[9], page, u0, v0, u1, v1);
}

/* 0x4020e0 (out NULL, counting) and 0x402050 (listing): the leading
 * textured faces of `mesh` in the cell. */
static int collect(s32 **out, const s32 *mesh, s32 page, s32 u0, s32 v0, s32 u1, s32 v1)
{
    const u8 *f = first_face(mesh);
    int i, n = 0;

    for (i = 0; i < mesh[1] && textured(f); i++, f += FACE_BYTES) {
        if (face_in_cell((const s32 *)f, page, u0, v0, u1, v1)) {
            if (out != NULL) {
                out[n] = (s32 *)f;
            }
            n++;
        }
    }
    return n;
}

static void note_mesh(struct anim *a, const s32 *mesh)
{
    int i;

    for (i = 0; i < a->mesh_count; i++) {
        if (a->meshes[i] == mesh) {
            return;
        }
    }
    a->meshes = realloc(a->meshes, (size_t)(a->mesh_count + 1) * sizeof(*a->meshes));
    a->meshes[a->mesh_count++] = mesh;
}

/* 0x401850 */
void anim_tex_free(void)
{
    int i, k;

    for (i = 0; i < s_count; i++) {
        struct anim *a = s_anims[i];

        for (k = 0; k < a->keys; k++) {
            free(a->faces[k]);
        }
        free(a->faces);
        free(a->uv);
        free(a->page);
        free(a->meshes);
        free(a);
    }
    free(s_anims);
    s_anims = NULL;
    s_count = 0;
}

/* 0x401a50
 * Adds the animation `mesh` describes, over the objects of texture slot
 * `tex`. A key whose cell an earlier key has already lists nothing. */
void anim_tex_add(const s32 *mesh, int tex)
{
    struct anim *a = calloc(1, sizeof(*a));
    const u8 *f = first_face(mesh);
    struct scene_obj *pool;
    int i, k, j, keys = 0, total;

    s_anims = realloc(s_anims, (size_t)(s_count + 1) * sizeof(*s_anims));
    s_anims[s_count] = a;
    /* 0x401e90: the key frames are the leading textured faces. */
    for (i = 0; i < mesh[1] && textured(f + i * FACE_BYTES); i++) {
        keys++;
    }
    a->faces = calloc((size_t)keys + 1, sizeof(*a->faces));
    a->uv = calloc((size_t)keys + 1, sizeof(*a->uv));
    a->page = calloc((size_t)keys + 1, sizeof(*a->page));
    a->next = 1;
    a->cur = 0;
    for (k = 0; k < keys; k++) {
        const s32 *w = (const s32 *)(f + k * FACE_BYTES);

        a->uv[k][0] = w[4] & ~(CELL - 1);
        a->uv[k][1] = w[5] & ~(CELL - 1);
        a->page[k] = w[10] & 0xffff0000;
        a->keys++;
    }
    pool = scene_obj_pool(&total);
    for (k = 0; k < a->keys; k++) {
        s32 u = a->uv[k][0], v = a->uv[k][1];
        int seen = 0, n = 0;

        for (j = 0; j < k; j++) {
            if (a->page[j] == a->page[k] && a->uv[j][0] == u && a->uv[j][1] == v) {
                seen = 1;
            }
        }
        if (!seen) {
            for (i = 0; i < total; i++) {
                if (pool[i].mesh != NULL && pool[i].tex == tex) {
                    n += collect(NULL, pool[i].mesh, a->page[k], u, v, u + CELL, v + CELL);
                }
            }
        }
        a->faces[k] = calloc((size_t)n + 2, sizeof(**a->faces));
        if (!seen) {
            s32 **out = a->faces[k];

            for (i = 0; i < total; i++) {
                if (pool[i].mesh != NULL && pool[i].tex == tex) {
                    int m = collect(out, pool[i].mesh, a->page[k], u, v, u + CELL, v + CELL);

                    if (m > 0) {
                        note_mesh(a, pool[i].mesh);
                    }
                    out += m;
                }
            }
        }
    }
    s_count++;
}

/* 0x401f00: each animation's next frame. */
void anim_tex_step(void)
{
    int i, k, m;

    for (i = 0; i < s_count; i++) {
        struct anim *a = s_anims[i];

        for (k = 0; k < a->keys; k++) {
            int from = (a->cur + k) % a->keys, to = (a->next + k) % a->keys;
            s32 dp = a->page[to] - a->page[from];
            s32 du = a->uv[to][0] - a->uv[from][0], dv = a->uv[to][1] - a->uv[from][1];
            s32 **f;

            for (f = a->faces[k]; *f != NULL; f++) {
                s32 *w = *f;

                w[10] += dp;
                w[4] += du;
                w[6] += du;
                w[8] += du;
                w[5] += dv;
                w[7] += dv;
                w[9] += dv;
            }
        }
        a->cur = a->next;
        a->next++;
        for (m = 0; m < a->mesh_count; m++) {
            gl3d_mesh_changed(a->meshes[m]);
        }
    }
}
