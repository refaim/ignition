/*
 * The real src/rnd_gl.c indexed DYN upload and draw, through the existing
 * GLES recording fixture. Buffer bytes, offsets, VAO/EBO state, draw mode
 * and profile counters are checked directly.
 */

#define TEST_EXPECT_FETCH_BUILD 1
#define main test_rnd_dyn_fetch_original_main
#include "test_rnd_dyn_fetch.c"
#undef main
#include "../src/rnd_gl.c"

#if !RND_INDEXED_DYN
#error "test_rnd_gl_indexed_dyn must be built with IGN_GLES_INDEXED_DYN=1"
#endif

#ifdef IGN_PROF
static uint64_t s_prof_counts[PROF_COUNTERS];

void prof_begin(int part)
{
    (void)part;
}

void prof_end(int part)
{
    (void)part;
}

void prof_frame(void)
{
}

void prof_count(int counter, uint64_t amount)
{
    s_prof_counts[counter] += amount;
}

static void prof_reset(void)
{
    memset(s_prof_counts, 0, sizeof(s_prof_counts));
}
#endif

enum { TEST_DYN_FLOATS = 9, TEST_INDEXED_BYTES = 1024 };

static unsigned char s_data[TEST_INDEXED_BYTES];

static void indexed_data(int vertex_count, int index_count)
{
    float *vertices = (float *)s_data;
    uint16_t *indices = (uint16_t *)(s_data + (size_t)vertex_count *
                                     TEST_DYN_FLOATS * sizeof(float));
    int i, k;

    memset(s_data, 0, sizeof(s_data));
    for (i = 0; i < vertex_count; i++) {
        for (k = 0; k < TEST_DYN_FLOATS; k++) {
            vertices[i * TEST_DYN_FLOATS + k] = (float)(i * 10 + k) + 0.25f;
        }
    }
    for (i = 0; i < index_count; i++) {
        indices[i] = (uint16_t)(i % vertex_count);
    }
}

static size_t indexed_bytes(int vertex_count, int index_count)
{
    return (size_t)vertex_count * TEST_DYN_FLOATS * sizeof(float) +
           (size_t)index_count * sizeof(uint16_t);
}

static struct rec_vao *dyn_vao(void)
{
    return rec_vao(s_prog[RND_DYN].vao);
}

static void start_dyn(void)
{
    fake_reset();
    CHECK(rnd_init());
    gl_record_reset();
    CHECK(rnd_stream_indexed(s_data, 4, 6) == 0);
    CHECK(s_rec_bind_calls == 0);
    rnd_use(RND_DYN);
}

static void test_upload_and_draw(void)
{
    GLuint vbo;
    int started = s_failures;

    start_dyn();
    vbo = s_prog[RND_DYN].vbo;
    CHECK(s_prog[RND_DYN].cap == 6 * TEST_DYN_FLOATS * sizeof(float));
    indexed_data(4, 6);
    gl_record_reset();
    rnd_use(RND_DYN);
#ifdef IGN_PROF
    prof_reset();
#endif

    CHECK(rnd_stream_indexed(s_data, 4, 6) == 1);
    CHECK(s_rec_buffer_data_calls == 0);
    CHECK(s_rec_subdata_calls == 1);
    CHECK(s_rec_subdata_target == GL_ARRAY_BUFFER);
    CHECK(s_rec_subdata_offset == 0);
    CHECK(s_rec_subdata_size == 156);
    CHECK(s_rec_array_buffer == vbo);
    CHECK(dyn_vao() != NULL);
    if (dyn_vao() != NULL) {
        CHECK(dyn_vao()->element == vbo);
    }
    CHECK(memcmp(s_rec_upload, s_data, 156) == 0);

#ifdef IGN_PROF
    CHECK(s_prof_counts[PROF_UPLOAD_BYTES] == 156);
#endif

    rnd_draw_indexed(RND_TRIANGLES, 0, 6);
    CHECK(s_rec_draw_elements_calls == 1);
    CHECK(s_rec_draw_elements_mode == GL_TRIANGLES);
    CHECK(s_rec_draw_elements_count == 6);
    CHECK(s_rec_draw_elements_type == GL_UNSIGNED_SHORT);
    CHECK(s_rec_draw_elements_offset == 4 * TEST_DYN_FLOATS * sizeof(float));
    CHECK(s_rec_draw_array_buffer == vbo);
    CHECK(s_rec_draw_element_buffer == vbo);
    CHECK(s_rec_draw_vao == s_prog[RND_DYN].vao);
    CHECK(dyn_vao() != NULL && dyn_vao()->element == 0);
#ifdef IGN_PROF
    CHECK(s_prof_counts[PROF_DRAW_CALLS] == 1);
#endif

    if (s_failures == started) {
        puts("real GL indexed upload/draw      ok");
    }
}

static void test_arrays_indexed_arrays_and_switches(void)
{
    float arrays[3 * TEST_DYN_FLOATS];
    GLuint dyn_vbo;
    int started = s_failures;
    int i;

    start_dyn();
    dyn_vbo = s_prog[RND_DYN].vbo;
    for (i = 0; i < 3 * TEST_DYN_FLOATS; i++) {
        arrays[i] = (float)i;
    }
    indexed_data(4, 6);

    gl_record_reset();
    rnd_use(RND_DYN);
    rnd_stream(arrays, 3);
    rnd_draw(RND_TRIANGLES, 0, 3);
    CHECK(s_rec_draw_arrays_calls == 1);
    CHECK(s_rec_draw_arrays_mode == GL_TRIANGLES);
    CHECK(s_rec_draw_arrays_count == 3);
    CHECK(dyn_vao() != NULL && dyn_vao()->element == 0);

    CHECK(rnd_stream_indexed(s_data, 4, 6) == 1);
    rnd_draw_indexed(RND_TRIANGLES, 0, 6);
    CHECK(s_rec_draw_elements_calls == 1);
    CHECK(dyn_vao() != NULL && dyn_vao()->element == 0);

    rnd_stream(arrays, 3);
    rnd_draw(RND_TRIANGLES, 0, 3);
    CHECK(s_rec_draw_arrays_calls == 2);
    CHECK(dyn_vao() != NULL && dyn_vao()->element == 0);

    rnd_use(RND_MESH);
    CHECK(rec_vao(s_prog[RND_MESH].vao) != NULL);
    CHECK(rec_vao(s_prog[RND_MESH].vao)->element == 0);
    rnd_use(RND_DYN);
    CHECK(dyn_vao() != NULL && dyn_vao()->element == 0);
    CHECK(rnd_stream_indexed(s_data, 4, 6) == 1);
    rnd_draw_indexed(RND_STRIP, 3, 3);
    CHECK(s_rec_draw_elements_mode == GL_TRIANGLE_STRIP);
    CHECK(s_rec_draw_elements_offset == 4 * TEST_DYN_FLOATS * sizeof(float) +
                                        3 * sizeof(uint16_t));
    CHECK(s_rec_draw_element_buffer == dyn_vbo);

    if (s_failures == started) {
        puts("arrays/indexed/VAO switching     ok");
    }
}

static void test_growth_and_reuse(void)
{
    int started = s_failures;

    start_dyn();
    indexed_data(10, 6);
    gl_record_reset();
    rnd_use(RND_DYN);
    CHECK(rnd_stream_indexed(s_data, 10, 6) == 1);
    CHECK(s_rec_buffer_data_calls == 1);
    CHECK(s_rec_buffer_data_size == (GLsizeiptr)indexed_bytes(10, 6));
    CHECK(s_rec_subdata_calls == 1);
    CHECK(s_rec_subdata_size == (GLsizeiptr)indexed_bytes(10, 6));
    CHECK(s_prog[RND_DYN].cap == (GLsizeiptr)indexed_bytes(10, 6));

    gl_record_reset();
    rnd_use(RND_DYN);
    CHECK(rnd_stream_indexed(s_data, 10, 6) == 1);
    CHECK(s_rec_buffer_data_calls == 0);
    CHECK(s_rec_subdata_calls == 1);

    indexed_data(1, 3);
    gl_record_reset();
    rnd_use(RND_DYN);
    CHECK(rnd_stream_indexed(s_data, 1, 3) == 1);
    CHECK(s_rec_buffer_data_calls == 0);
    CHECK(s_rec_subdata_size == (GLsizeiptr)indexed_bytes(1, 3));
    if (s_failures == started) {
        puts("GL buffer growth and reuse       ok");
    }
}

static void test_invalid_counts(void)
{
    int started = s_failures;
    int binds;

    start_dyn();
    indexed_data(4, 6);
    gl_record_reset();
    rnd_use(RND_DYN);
    binds = s_rec_bind_calls;

    CHECK(rnd_stream_indexed(NULL, 4, 6) == 0);
    CHECK(rnd_stream_indexed(s_data, 0, 6) == 0);
    CHECK(rnd_stream_indexed(s_data, 4, 0) == 0);
    CHECK(rnd_stream_indexed(s_data, 65537, 1) == 0);
    CHECK(rnd_stream_indexed(s_data, 1, 65537) == 0);
    CHECK(s_rec_bind_calls == binds);
    CHECK(s_rec_subdata_calls == 0);

    rnd_use(RND_PRESENT);
    CHECK(rnd_stream_indexed(s_data, 4, 6) == 0);
    CHECK(s_rec_bind_calls == binds);
    rnd_use(RND_DYN);
    CHECK(rnd_stream_indexed(s_data, 1, 3) == 1);
    if (s_failures == started) {
        puts("indexed count/state rejection    ok");
    }
}

static void test_shutdown_and_reinit(void)
{
    int started = s_failures;

    start_dyn();
    indexed_data(4, 6);
    gl_record_reset();
    rnd_use(RND_DYN);
    CHECK(rnd_stream_indexed(s_data, 4, 6) == 1);
    rnd_draw_indexed(RND_TRIANGLES, 0, 6);

    gl_record_reset();
    rnd_shutdown();
    CHECK(s_indexed_offset == 0);
    CHECK(s_rec_deleted_buffers > 0);

    fake_reset();
    CHECK(rnd_init());
    gl_record_reset();
    rnd_use(RND_DYN);
    CHECK(rnd_stream_indexed(s_data, 4, 6) == 1);
    rnd_draw_indexed(RND_TRIANGLES, 0, 6);
    CHECK(s_rec_draw_elements_calls == 1);

    rnd_shutdown();
    fake_reset();
    s_fail_normal_dyn = 1;
    CHECK(rnd_init() == 0);
    fake_reset();
    CHECK(rnd_init());
    gl_record_reset();
    rnd_use(RND_DYN);
    CHECK(rnd_stream_indexed(s_data, 4, 6) == 1);
    CHECK(s_rec_subdata_calls == 1);
    rnd_shutdown();
    if (s_failures == started) {
        puts("backend cleanup/failure/reinit   ok");
    }
}

int main(void)
{
    test_upload_and_draw();
    test_arrays_indexed_arrays_and_switches();
    test_growth_and_reuse();
    test_invalid_counts();
    test_shutdown_and_reinit();

    if (s_failures != 0) {
        fprintf(stderr, "test_rnd_gl_indexed_dyn: %d failures\n", s_failures);
        return 1;
    }
    puts("ok");
    return 0;
}
