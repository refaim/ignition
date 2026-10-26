/*
 * Exact indexed DYN geometry through src/gl3d.c's real draw_dyns path.
 * The existing fetch fixture supplies the inclusion harness and rnd stubs;
 * this test records the prospective indexed upload without calling its
 * entry point directly.
 */

#include <stdlib.h>

static void *test_realloc(void *p, size_t size);

#define realloc test_realloc
#define main test_gl3d_dyn_fetch_original_main
#include "test_gl3d_dyn_fetch.c"
#undef main
#undef realloc

#if !RND_INDEXED_DYN
#error "test_gl3d_indexed_dyn must be built with IGN_GLES_INDEXED_DYN=1"
#endif

enum {
    INDEXED_MAX_VERTICES = MAX_DYN * 6,
    INDEXED_MAX_BYTES = INDEXED_MAX_VERTICES * DYN_FLOATS * (int)sizeof(float) +
                        INDEXED_MAX_VERTICES * (int)sizeof(uint16_t),
    MAX_INDEXED_EVENTS = 8,
    MAX_INDEXED_DRAWS = 16
};

struct indexed_rec {
    int vertices;
    int indices;
    int bytes;
    int cap;
    unsigned char *data;
};

struct indexed_draw_rec {
    int event;
    int prim;
    int first;
    int count;
    int tex;
    int table;
    int depth_write;
    int offset;
};

static struct indexed_rec s_indexed[MAX_INDEXED_EVENTS];
static int s_indexed_count;
static struct indexed_draw_rec s_indexed_draws[MAX_INDEXED_DRAWS];
static int s_indexed_draw_count;
static int s_fail_next_indexed_upload;
static int s_fail_realloc_count;
static size_t s_fail_realloc_size;

static const unsigned char *s_expected_seq[INDEXED_MAX_VERTICES];
static int s_expected_label[INDEXED_MAX_VERTICES];

static void *test_realloc(void *p, size_t size)
{
    if (s_fail_realloc_count > 0 && size == s_fail_realloc_size) {
        s_fail_realloc_count--;
        return NULL;
    }
    return realloc(p, size);
}

int rnd_stream_indexed(const void *data, int vertex_count, int index_count)
{
    size_t vertex_bytes = (size_t)vertex_count * DYN_FLOATS * sizeof(float);
    size_t index_bytes = (size_t)index_count * sizeof(uint16_t);
    size_t bytes = vertex_bytes + index_bytes;
    struct indexed_rec *r;

    if (s_fail_next_indexed_upload) {
        s_fail_next_indexed_upload = 0;
        return 0;
    }
    CHECK(data != NULL);
    CHECK(vertex_count > 0);
    CHECK(index_count > 0);
    CHECK(vertex_count <= 65536);
    CHECK(index_count <= 65536);
    CHECK(bytes <= INDEXED_MAX_BYTES);
    CHECK(s_indexed_count < MAX_INDEXED_EVENTS);
    if (data == NULL || vertex_count <= 0 || index_count <= 0 ||
        vertex_count > 65536 || index_count > 65536 ||
        bytes > INDEXED_MAX_BYTES || s_indexed_count >= MAX_INDEXED_EVENTS) {
        return 0;
    }
    r = &s_indexed[s_indexed_count];
    if ((size_t)r->cap < bytes) {
        unsigned char *grown = realloc(r->data, bytes);

        CHECK(grown != NULL);
        if (grown == NULL) {
            return 0;
        }
        r->data = grown;
        r->cap = (int)bytes;
    }
    r->vertices = vertex_count;
    r->indices = index_count;
    r->bytes = (int)bytes;
    memcpy(r->data, data, bytes);
    s_indexed_count++;
    return 1;
}

void rnd_draw_indexed(int prim, int first, int count)
{
    struct indexed_draw_rec *d;

    CHECK(count >= 0);
    CHECK(s_indexed_draw_count < MAX_INDEXED_DRAWS);
    if (s_indexed_draw_count >= MAX_INDEXED_DRAWS) {
        return;
    }
    d = &s_indexed_draws[s_indexed_draw_count++];
    d->event = s_indexed_count - 1;
    d->prim = prim;
    d->first = first;
    d->count = count;
    d->tex = s_texture_unit0;
    d->table = s_use_table_uniform;
    d->depth_write = s_last_depth_write;
    d->offset = s_last_offset;
}

static void reset_indexed(void)
{
    int i;

    for (i = 0; i < s_indexed_count; i++) {
        free(s_indexed[i].data);
        memset(&s_indexed[i], 0, sizeof(s_indexed[i]));
    }
    memset(s_indexed, 0, sizeof(s_indexed));
    memset(s_indexed_draws, 0, sizeof(s_indexed_draws));
    s_indexed_count = 0;
    s_indexed_draw_count = 0;
    s_fail_next_indexed_upload = 0;
    s_fail_realloc_count = 0;
    s_fail_realloc_size = 0;
    reset_calls();
}

static void quad(int i, float depth, unsigned tex, const u8 *table, float uv_shift)
{
    static const float x[4] = { -0.5f, 0.5f, 0.5f, -0.5f };
    static const float y[4] = { -0.5f, -0.5f, 0.5f, 0.5f };
    static const int order[6] = { 0, 1, 2, 0, 2, 3 };
    struct dyn *d = &s_dyns[i];
    int k;

    memset(d, 0, sizeof(*d));
    d->count = 6;
    d->depth = depth;
    d->tex = tex;
    d->table = table;
    for (k = 0; k < 6; k++) {
        int v = order[k];

        d->v[k][0] = x[v];
        d->v[k][1] = y[v];
        d->v[k][2] = 0.25f;
        d->v[k][3] = 1.0f;
        d->v[k][4] = (v < 2 ? 0.0f : 1.0f) + uv_shift;
        d->v[k][5] = (v == 0 || v == 3 ? 0.0f : 1.0f) + uv_shift;
        d->v[k][6] = 3.0f;
        d->v[k][7] = 77.0f + uv_shift;
        d->v[k][8] = 0.0f;
    }
}

static void set_vertex_bits(struct dyn *d, int at, uint32_t x_bits)
{
    memcpy(&d->v[at][0], &x_bits, sizeof(x_bits));
}

static int vertices_in(const struct dyn *const *faces, int face_count)
{
    int i, n = 0;

    for (i = 0; i < face_count; i++) {
        n += faces[i]->count;
    }
    return n;
}

static void check_indexed_event(int event, const struct dyn *const *faces, int face_count,
                                int unique_expected)
{
    const struct indexed_rec *r;
    const unsigned char *vertices;
    const uint16_t *indices;
    int total = vertices_in(faces, face_count);
    int next = 0, pos = 0, i, k, u, v;

    CHECK(event >= 0 && event < s_indexed_count);
    if (event < 0 || event >= s_indexed_count) {
        return;
    }
    r = &s_indexed[event];
    CHECK(r->vertices == unique_expected);
    CHECK(r->indices == total);
    CHECK(r->bytes == unique_expected * DYN_FLOATS * (int)sizeof(float) +
                      total * (int)sizeof(uint16_t));
    CHECK(r->data != NULL);
    if (r->data == NULL || r->vertices != unique_expected || r->indices != total) {
        return;
    }
    vertices = r->data;
    indices = (const uint16_t *)(vertices + (size_t)unique_expected * DYN_FLOATS *
                                 sizeof(float));
    CHECK((((uintptr_t)indices) & (sizeof(uint16_t) - 1u)) == 0);

    for (i = 0; i < face_count; i++) {
        for (k = 0; k < faces[i]->count; k++) {
            const unsigned char *original =
                (const unsigned char *)&faces[i]->v[k][0];
            int label = -1;

            for (u = 0; u < pos; u++) {
                if (memcmp(s_expected_seq[u], original, DYN_FLOATS * sizeof(float)) == 0) {
                    label = s_expected_label[u];
                    break;
                }
            }
            if (label < 0) {
                label = next++;
            }
            s_expected_seq[pos] = original;
            s_expected_label[pos] = label;
            CHECK(indices[pos] == label);
            CHECK(indices[pos] < r->vertices);
            if (indices[pos] < r->vertices) {
                CHECK(memcmp(vertices + (size_t)indices[pos] * DYN_FLOATS * sizeof(float),
                             original, DYN_FLOATS * sizeof(float)) == 0);
            }
            pos++;
        }
    }
    CHECK(next == unique_expected);

    for (u = 0; u < r->vertices; u++) {
        for (v = u + 1; v < r->vertices; v++) {
            CHECK(memcmp(vertices + (size_t)u * DYN_FLOATS * sizeof(float),
                         vertices + (size_t)v * DYN_FLOATS * sizeof(float),
                         DYN_FLOATS * sizeof(float)) != 0);
        }
    }
}

static void check_array_fallback(const struct dyn *const *faces, int face_count)
{
    int total = vertices_in(faces, face_count);
    int pos = 0, i, k;

    CHECK(s_indexed_count == 0);
    CHECK(s_stream_vert_count == total);
    CHECK(s_stream_count == total);
    for (i = 0; i < face_count; i++) {
        for (k = 0; k < faces[i]->count; k++) {
            CHECK(memcmp(s_stream + (size_t)pos * DYN_FLOATS, faces[i]->v[k],
                         DYN_FLOATS * sizeof(float)) == 0);
            pos++;
        }
    }
}

static void check_indexed_draw(int draw, int event, int vertices, int tex, int table)
{
    CHECK(draw >= 0 && draw < s_indexed_draw_count);
    if (draw < 0 || draw >= s_indexed_draw_count) {
        return;
    }
    CHECK(s_indexed_draws[draw].event == event);
    CHECK(s_indexed_draws[draw].prim == RND_TRIANGLES);
    CHECK(s_indexed_draws[draw].first == 0);
    CHECK(s_indexed_draws[draw].count == vertices);
    CHECK(s_indexed_draws[draw].tex == tex);
    CHECK(s_indexed_draws[draw].table == table);
    CHECK(s_indexed_draws[draw].depth_write == !table);
    CHECK(s_indexed_draws[draw].offset == table);
}

static void test_one_quad_indexed(void)
{
    static const u8 table[256 * 256];
    const struct dyn *faces[1];
    int started = s_failures;

    prepare(1);
    quad(0, 10.0f, 31, table, 0.0f);
    faces[0] = &s_dyns[0];
    reset_indexed();
    s_fetch_active = 1;

    draw_dyns();

    CHECK(s_indexed_count == 1);
    CHECK(s_indexed_draw_count == 1);
    check_indexed_event(0, faces, 1, 4);
    check_indexed_draw(0, 0, 6, 31, 1);
    CHECK(s_copy_count == 0);
    CHECK(s_dyn_count == 0);
    if (s_failures == started) {
        puts("one quad exact indexed upload    ok");
    }
}

static void test_multiple_quads_share_records(void)
{
    static const u8 table[256 * 256];
    const struct dyn *faces[2];
    int started = s_failures;

    prepare(2);
    quad(0, 20.0f, 41, table, 0.0f);
    quad(1, 10.0f, 41, table, 0.0f);
    faces[0] = &s_dyns[0];
    faces[1] = &s_dyns[1];
    reset_indexed();
    s_fetch_active = 1;

    draw_dyns();

    CHECK(s_indexed_count == 1);
    CHECK(s_indexed_draw_count == 1);
    check_indexed_event(0, faces, 2, 4);
    check_indexed_draw(0, 0, 12, 41, 1);
    CHECK(s_indexed[0].bytes == 168);
    if (s_failures == started) {
        puts("two quads share exact records    ok");
    }
}

static void test_run_barriers(void)
{
    static const u8 table[256 * 256];
    const struct dyn *faces[3];
    int started = s_failures;

    prepare(3);
    quad(0, 30.0f, 51, table, 0.0f);
    quad(1, 20.0f, 51, NULL, 0.0f);
    quad(2, 10.0f, 51, table, 0.0f);
    faces[0] = &s_dyns[0];
    faces[1] = &s_dyns[1];
    faces[2] = &s_dyns[2];
    reset_indexed();
    s_fetch_active = 1;

    draw_dyns();

    CHECK(s_indexed_count == 3);
    CHECK(s_indexed_draw_count == 3);
    check_indexed_event(0, faces, 1, 4);
    check_indexed_event(1, faces + 1, 1, 4);
    check_indexed_event(2, faces + 2, 1, 4);
    check_indexed_draw(0, 0, 6, 51, 1);
    check_indexed_draw(1, 1, 6, 51, 0);
    check_indexed_draw(2, 2, 6, 51, 1);
    if (s_failures == started) {
        puts("texture/table run barriers       ok");
    }
}

static void test_pure_triangles_fall_back(void)
{
    const struct dyn *faces[1];
    int started = s_failures;

    prepare(1);
    face(0, 10.0f, 61, NULL, 1.0f);
    faces[0] = &s_dyns[0];
    reset_indexed();
    s_fetch_active = 1;

    draw_dyns();

    check_array_fallback(faces, 1);
    CHECK(s_indexed_draw_count == 0);
    CHECK(s_draw_count == 1);
    CHECK(s_draws[0].vertices == 3);
    if (s_failures == started) {
        puts("pure triangle arrays on no gain  ok");
    }
}

static void test_mixed_triangle_quad(void)
{
    const struct dyn *faces[2];
    int started = s_failures;

    prepare(2);
    quad(0, 20.0f, 71, NULL, 0.0f);
    face(1, 10.0f, 71, NULL, 9.0f);
    faces[0] = &s_dyns[0];
    faces[1] = &s_dyns[1];
    reset_indexed();
    s_fetch_active = 1;

    draw_dyns();

    CHECK(s_indexed_count == 1);
    CHECK(s_indexed_draw_count == 1);
    check_indexed_event(0, faces, 2, 7);
    check_indexed_draw(0, 0, 9, 71, 0);
    CHECK(s_indexed[0].bytes == 270);
    if (s_failures == started) {
        puts("mixed triangle and quad run      ok");
    }
}

static void test_identical_positions_keep_attributes(void)
{
    const struct dyn *faces[3];
    int started = s_failures;

    prepare(3);
    quad(0, 30.0f, 81, NULL, 0.0f);
    quad(1, 20.0f, 81, NULL, 4.0f);
    quad(2, 10.0f, 81, NULL, 0.0f);
    faces[0] = &s_dyns[0];
    faces[1] = &s_dyns[1];
    faces[2] = &s_dyns[2];
    reset_indexed();
    s_fetch_active = 1;

    draw_dyns();

    CHECK(s_indexed_count == 1);
    check_indexed_event(0, faces, 3, 8);
    CHECK(s_indexed_draw_count == 1);
    CHECK(s_indexed_draws[0].count == 18);
    if (s_failures == started) {
        puts("same positions, distinct attrs   ok");
    }
}

static void test_no_exact_duplicates_falls_back(void)
{
    const struct dyn *faces[1];
    int started = s_failures;

    prepare(1);
    quad(0, 10.0f, 91, NULL, 0.0f);
    s_dyns[0].v[3][4] += 0.25f;
    s_dyns[0].v[4][8] += 0.75f;
    s_dyns[0].v[5][5] += 0.5f;
    faces[0] = &s_dyns[0];
    reset_indexed();
    s_fetch_active = 1;

    draw_dyns();

    check_array_fallback(faces, 1);
    CHECK(s_indexed_draw_count == 0);
    if (s_failures == started) {
        puts("no exact duplicates fall back    ok");
    }
}

static void test_all_same_vertices(void)
{
    const struct dyn *faces[1];
    int k, started = s_failures;

    prepare(1);
    quad(0, 10.0f, 101, NULL, 0.0f);
    for (k = 1; k < 6; k++) {
        memcpy(s_dyns[0].v[k], s_dyns[0].v[0], DYN_FLOATS * sizeof(float));
    }
    faces[0] = &s_dyns[0];
    reset_indexed();
    s_fetch_active = 1;

    draw_dyns();

    CHECK(s_indexed_count == 1);
    check_indexed_event(0, faces, 1, 1);
    CHECK(s_indexed[0].bytes == 48);
    if (s_failures == started) {
        puts("all records same, one vertex     ok");
    }
}

static void test_signed_zero_and_nan_payloads(void)
{
    const struct dyn *faces[2];
    int k;
    int started = s_failures;

    prepare(2);
    face(0, 20.0f, 111, NULL, 0.0f);
    face(1, 10.0f, 111, NULL, 0.0f);
    set_vertex_bits(&s_dyns[0], 0, 0x00000000u);
    set_vertex_bits(&s_dyns[0], 1, 0x80000000u);
    set_vertex_bits(&s_dyns[0], 2, 0x7fc00001u);
    for (k = 0; k < 3; k++) {
        memcpy(s_dyns[1].v[k], s_dyns[0].v[k], DYN_FLOATS * sizeof(float));
    }
    set_vertex_bits(&s_dyns[1], 2, 0x7fc00002u);
    faces[0] = &s_dyns[0];
    faces[1] = &s_dyns[1];
    reset_indexed();
    s_fetch_active = 1;

    draw_dyns();

    CHECK(s_indexed_count == 1);
    check_indexed_event(0, faces, 2, 4);
    CHECK(s_indexed[0].bytes == 156);
    if (s_failures == started) {
        puts("signed zero and NaN payloads     ok");
    }
}

static void test_zero_count(void)
{
    int started = s_failures;

    prepare(0);
    reset_indexed();
    s_fetch_active = 1;

    draw_dyns();

    CHECK(s_indexed_count == 0);
    CHECK(s_draw_count == 0);
    CHECK(s_copy_count == 0);
    if (s_failures == started) {
        puts("zero DYN records                 ok");
    }
}

static void test_count_bounds(void)
{
    int unique = -1, started = s_failures;

    reset_indexed();
    CHECK(indexed_build(0, &unique) == 0);
    CHECK(indexed_build(-1, &unique) == 0);
    CHECK(indexed_build(65537, &unique) == 0);
    CHECK(unique == -1);
    if (s_failures == started) {
        puts("negative and >uint16 run bounds  ok");
    }
}

static void test_staging_allocation_failures(void)
{
    const struct dyn *faces[2];
    int started = s_failures;

    prepare(1);
    quad(0, 10.0f, 121, NULL, 0.0f);
    faces[0] = &s_dyns[0];
    reset_indexed();
    s_ready = 1;
    gl3d_flush();
    CHECK(s_indexed_data == NULL);
    CHECK(s_indexed_hash == NULL);
    CHECK(s_indexed_cap == 0);
    CHECK(s_indexed_hash_cap == 0);
    s_fetch_active = 1;

    s_fail_realloc_count = 1;
    s_fail_realloc_size = 6 * DYN_FLOATS * sizeof(float) + 6 * sizeof(uint16_t);
    draw_dyns();
    check_array_fallback(faces, 1);
    CHECK(s_indexed_draw_count == 0);
    reset_indexed();

    prepare(1);
    quad(0, 10.0f, 121, NULL, 0.0f);
    faces[0] = &s_dyns[0];
    draw_dyns();
    CHECK(s_indexed_count == 1);
    check_indexed_event(0, faces, 1, 4);
    reset_indexed();

    prepare(2);
    quad(0, 20.0f, 121, NULL, 0.0f);
    quad(1, 10.0f, 121, NULL, 0.0f);
    faces[0] = &s_dyns[0];
    faces[1] = &s_dyns[1];
    s_fail_realloc_count = 1;
    s_fail_realloc_size = 32 * sizeof(int);
    draw_dyns();
    check_array_fallback(faces, 2);
    CHECK(s_indexed_draw_count == 0);
    reset_indexed();

    prepare(2);
    quad(0, 20.0f, 121, NULL, 0.0f);
    quad(1, 10.0f, 121, NULL, 0.0f);
    faces[0] = &s_dyns[0];
    faces[1] = &s_dyns[1];
    draw_dyns();
    CHECK(s_indexed_count == 1);
    check_indexed_event(0, faces, 2, 4);
    reset_indexed();

    s_ready = 1;
    gl3d_flush();
    CHECK(s_indexed_data == NULL);
    CHECK(s_indexed_hash == NULL);
    prepare(1);
    quad(0, 10.0f, 121, NULL, 0.0f);
    faces[0] = &s_dyns[0];
    draw_dyns();
    CHECK(s_indexed_count == 1);
    check_indexed_event(0, faces, 1, 4);
    s_ready = 0;
    if (s_failures == started) {
        puts("staging allocation/cleanup       ok");
    }
}

static void test_backend_reject_falls_back(void)
{
    const struct dyn *faces[1];
    int started = s_failures;

    prepare(1);
    quad(0, 10.0f, 131, NULL, 0.0f);
    faces[0] = &s_dyns[0];
    reset_indexed();
    s_fetch_active = 1;
    s_fail_next_indexed_upload = 1;

    draw_dyns();

    check_array_fallback(faces, 1);
    CHECK(s_indexed_draw_count == 0);
    if (s_failures == started) {
        puts("backend rejection arrays         ok");
    }
}

static void test_nonfetch_keeps_array_path(void)
{
    const struct dyn *faces[1];
    int started = s_failures;

    prepare(1);
    quad(0, 10.0f, 141, NULL, 0.0f);
    faces[0] = &s_dyns[0];
    reset_indexed();
    s_fetch_active = 0;
    s_batch_copies = 0;

    draw_dyns();

    check_array_fallback(faces, 1);
    CHECK(s_indexed_draw_count == 0);
    CHECK(s_draw_count == 1);
    if (s_failures == started) {
        puts("nonfetch path stays on arrays    ok");
    }
}

/* Batch copies and no fetch also use draw_batch, but their dependency
 * layers must still stream the original arrays. */
static void test_nonfetch_layers_keep_array_path(void)
{
    static const u8 table[256 * 256];
    const struct dyn *faces[2];
    int started = s_failures;

    prepare(2);
    quad(0, 20.0f, 142, table, 1.0f);
    quad(1, 10.0f, 142, table, 3.0f);
    faces[0] = &s_dyns[0];
    faces[1] = &s_dyns[1];
    reset_indexed();
    s_fetch_active = 0;
    s_batch_copies = 1;

    draw_dyns();

    CHECK(s_indexed_count == 0);
    CHECK(s_indexed_draw_count == 0);
    CHECK(s_copy_count == 2);
    CHECK(s_draw_count == 2);
    if (s_draw_count == 2) {
        CHECK(s_draws[0].vertices == 6);
        CHECK(s_draws[0].first_uv == 1.0f);
        CHECK(s_draws[0].tex == 142);
        CHECK(s_draws[0].table == 1);
        CHECK(s_draws[0].copies_before == 1);
        CHECK(s_draws[1].vertices == 6);
        CHECK(s_draws[1].first_uv == 3.0f);
        CHECK(s_draws[1].tex == 142);
        CHECK(s_draws[1].table == 1);
        CHECK(s_draws[1].copies_before == 2);
    }
    check_array_fallback(faces + 1, 1);
    CHECK(s_dyn_count == 0);
    if (s_failures == started) {
        puts("nonfetch layers stay on arrays   ok");
    }
}

static void test_maximum_run(void)
{
    const struct dyn *first;
    int i, k, started = s_failures;

    prepare(MAX_DYN);
    for (i = 0; i < MAX_DYN; i++) {
        quad(i, (float)(MAX_DYN - i), 151, NULL, 0.0f);
        for (k = 1; k < 6; k++) {
            memcpy(s_dyns[i].v[k], s_dyns[i].v[0], DYN_FLOATS * sizeof(float));
        }
    }
    first = &s_dyns[0];
    reset_indexed();
    s_fetch_active = 1;

    draw_dyns();

    CHECK(s_indexed_count == 1);
    CHECK(s_indexed[0].vertices == 1);
    CHECK(s_indexed[0].indices == MAX_DYN * 6);
    CHECK(s_indexed[0].bytes == DYN_FLOATS * (int)sizeof(float) +
                              MAX_DYN * 6 * (int)sizeof(uint16_t));
    if (s_indexed_count == 1) {
        const uint16_t *indices =
            (const uint16_t *)(s_indexed[0].data + DYN_FLOATS * sizeof(float));

        CHECK(memcmp(s_indexed[0].data, first->v[0],
                     DYN_FLOATS * sizeof(float)) == 0);
        for (k = 0; k < MAX_DYN * 6; k++) {
            CHECK(indices[k] == 0);
        }
    }
    CHECK(s_indexed_draw_count == 1);
    if (s_failures == started) {
        puts("maximum 4096 faces / 24576 idx   ok");
    }
}

int main(void)
{
    test_one_quad_indexed();
    test_multiple_quads_share_records();
    test_run_barriers();
    test_pure_triangles_fall_back();
    test_mixed_triangle_quad();
    test_identical_positions_keep_attributes();
    test_no_exact_duplicates_falls_back();
    test_all_same_vertices();
    test_signed_zero_and_nan_payloads();
    test_zero_count();
    test_count_bounds();
    test_staging_allocation_failures();
    test_backend_reject_falls_back();
    test_nonfetch_keeps_array_path();
    test_nonfetch_layers_keep_array_path();
    test_maximum_run();

    if (s_failures != 0) {
        fprintf(stderr, "test_gl3d_indexed_dyn: %d failures\n", s_failures);
        return 1;
    }
    puts("ok");
    return 0;
}
