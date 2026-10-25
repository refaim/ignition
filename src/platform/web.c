/*
 * platform/web.c - the browser, through Emscripten's own APIs: a
 * WebGL 2 context on the page's canvas (OpenGL ES 3.0 for rnd_gl.c),
 * keyboard, gamepads and touch from the HTML5 events, and Web Audio.
 *
 * The game's loops cannot block a page, so the build uses Asyncify:
 * after each frame plat_present waits for the browser's next animation
 * frame, and a loop that only polls the keyboard gives the browser
 * control every 16 ms.
 *
 * The page (platforms/web/index.html) mounts IndexedDB at /ignition,
 * imports the game into /ignition/disc and starts main().
 */

#include <emscripten.h>
#include <emscripten/html5.h>

#include <stdio.h>
#include <string.h>

#include "app.h"
#include "dik.h"
#include "platform/platform.h"
#include "platform/common.h"
#include "platform/touch.h"
#include "rnd_gl.h"

static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE s_context;
static int s_quit;
static u32 s_last_yield;

u32 plat_time_ms(void)
{
    static double start = -1.0;
    double now = emscripten_get_now();

    if (start < 0.0) {
        start = now;
    }
    return (u32)(now - start);
}

/* ------------------------------------------------------------ yielding */

EM_ASYNC_JS(void, web_wait_frame, (void), {
    await new Promise(function(resolve) { requestAnimationFrame(resolve); });
});

static void maybe_yield(void)
{
    if (plat_time_ms() - s_last_yield >= 16) {
        emscripten_sleep(0);
        s_last_yield = plat_time_ms();
    }
}

/* ------------------------------------------------------------ keys */

static const struct { const char *code; u8 dik; } s_keys[] = {
    { "Escape", DIK_ESCAPE }, { "Digit1", DIK_1 }, { "Digit2", DIK_2 }, { "Digit3", DIK_3 },
    { "Digit4", DIK_4 }, { "Digit5", DIK_5 }, { "Digit6", DIK_6 }, { "Digit7", DIK_7 },
    { "Digit8", DIK_8 }, { "Digit9", DIK_9 }, { "Digit0", DIK_0 }, { "Minus", DIK_MINUS },
    { "Equal", DIK_EQUALS }, { "Backspace", DIK_BACK }, { "Tab", DIK_TAB }, { "KeyQ", DIK_Q },
    { "KeyW", DIK_W }, { "KeyE", DIK_E }, { "KeyR", DIK_R }, { "KeyT", DIK_T }, { "KeyY", DIK_Y },
    { "KeyU", DIK_U }, { "KeyI", DIK_I }, { "KeyO", DIK_O }, { "KeyP", DIK_P },
    { "BracketLeft", DIK_LBRACKET }, { "BracketRight", DIK_RBRACKET }, { "Enter", DIK_RETURN },
    { "ControlLeft", DIK_LCONTROL }, { "KeyA", DIK_A }, { "KeyS", DIK_S }, { "KeyD", DIK_D },
    { "KeyF", DIK_F }, { "KeyG", DIK_G }, { "KeyH", DIK_H }, { "KeyJ", DIK_J }, { "KeyK", DIK_K },
    { "KeyL", DIK_L }, { "Semicolon", DIK_SEMICOLON }, { "Quote", DIK_APOSTROPHE },
    { "Backquote", DIK_GRAVE }, { "ShiftLeft", DIK_LSHIFT }, { "Backslash", DIK_BACKSLASH },
    { "KeyZ", DIK_Z }, { "KeyX", DIK_X }, { "KeyC", DIK_C }, { "KeyV", DIK_V }, { "KeyB", DIK_B },
    { "KeyN", DIK_N }, { "KeyM", DIK_M }, { "Comma", DIK_COMMA }, { "Period", DIK_PERIOD },
    { "Slash", DIK_SLASH }, { "ShiftRight", DIK_RSHIFT }, { "NumpadMultiply", DIK_MULTIPLY },
    { "AltLeft", DIK_LMENU }, { "Space", DIK_SPACE }, { "CapsLock", DIK_CAPITAL },
    { "F1", DIK_F1 }, { "F2", DIK_F2 }, { "F3", DIK_F3 }, { "F4", DIK_F4 }, { "F5", DIK_F5 },
    { "F6", DIK_F6 }, { "F7", DIK_F7 }, { "F8", DIK_F8 }, { "F9", DIK_F9 }, { "F10", DIK_F10 },
    { "NumLock", DIK_NUMLOCK }, { "ScrollLock", DIK_SCROLL }, { "Numpad7", DIK_NUMPAD7 },
    { "Numpad8", DIK_NUMPAD8 }, { "Numpad9", DIK_NUMPAD9 }, { "NumpadSubtract", DIK_SUBTRACT },
    { "Numpad4", DIK_NUMPAD4 }, { "Numpad5", DIK_NUMPAD5 }, { "Numpad6", DIK_NUMPAD6 },
    { "NumpadAdd", DIK_ADD }, { "Numpad1", DIK_NUMPAD1 }, { "Numpad2", DIK_NUMPAD2 },
    { "Numpad3", DIK_NUMPAD3 }, { "Numpad0", DIK_NUMPAD0 }, { "NumpadDecimal", DIK_DECIMAL },
    { "F11", DIK_F11 }, { "F12", DIK_F12 }, { "NumpadEnter", DIK_NUMPADENTER },
    { "ControlRight", DIK_RCONTROL }, { "NumpadDivide", DIK_DIVIDE }, { "AltRight", DIK_RMENU },
    { "Home", DIK_HOME }, { "ArrowUp", DIK_UP }, { "PageUp", DIK_PRIOR },
    { "ArrowLeft", DIK_LEFT }, { "ArrowRight", DIK_RIGHT }, { "End", DIK_END },
    { "ArrowDown", DIK_DOWN }, { "PageDown", DIK_NEXT }, { "Insert", DIK_INSERT },
    { "Delete", DIK_DELETE }, { "MetaLeft", DIK_LWIN },
};

static int map_code(const char *code)
{
    unsigned i;

    for (i = 0; i < sizeof(s_keys) / sizeof(s_keys[0]); i++) {
        if (strcmp(s_keys[i].code, code) == 0) {
            return s_keys[i].dik;
        }
    }
    return 0;
}

static EM_BOOL on_key(int type, const EmscriptenKeyboardEvent *e, void *user)
{
    int dik = map_code(e->code);

    (void)user;
    if (dik == 0) {
        return EM_FALSE;
    }
    if (type == EMSCRIPTEN_EVENT_KEYDOWN) {
        /* Alt+R switches between the GPU and the original's CPU 2D. */
        if (dik == DIK_R && e->altKey) {
            if (!e->repeat) {
                app_toggle_renderer();
            }
            return EM_TRUE;
        }
        /* Alt+F shows the race's debug lines. */
        if (dik == DIK_F && e->altKey) {
            if (!e->repeat) {
                app_toggle_debug();
            }
            return EM_TRUE;
        }
        if (!e->repeat && !g_plat_keys[dik]) {
            plat_key_event(dik, 1);
        }
    } else if (g_plat_keys[dik]) {
        plat_key_event(dik, 0);
    }
    return EM_TRUE;
}

/* ------------------------------------------------------------ touch */

static EM_BOOL on_touch(int type, const EmscriptenTouchEvent *e, void *user)
{
    double ratio = emscripten_get_device_pixel_ratio();
    int i;

    (void)user;
    for (i = 0; i < e->numTouches; i++) {
        const EmscriptenTouchPoint *t = &e->touches[i];

        if (!t->isChanged) {
            continue;
        }
        touch_finger(t->identifier, (float)(t->targetX * ratio), (float)(t->targetY * ratio),
                     type == EMSCRIPTEN_EVENT_TOUCHSTART || type == EMSCRIPTEN_EVENT_TOUCHMOVE);
    }
    return EM_TRUE;
}

/* ------------------------------------------------------------ gamepads */

/* The browser's standard mapping. */
static u32 gamepad_buttons(void)
{
    static const u32 map[17] = {
        PAD_A, PAD_B, PAD_X, PAD_Y, PAD_L, PAD_R, PAD_L, PAD_R, PAD_SELECT, PAD_START,
        0, 0, PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, 0
    };
    u32 b = 0;
    int n, i, k;

    if (emscripten_sample_gamepad_data() != EMSCRIPTEN_RESULT_SUCCESS) {
        return 0;
    }
    n = emscripten_get_num_gamepads();
    g_plat_sticks[0].present = g_plat_sticks[1].present = 0;
    for (i = 0; i < n; i++) {
        EmscriptenGamepadEvent g;
        struct plat_stick *s;

        if (emscripten_get_gamepad_status(i, &g) != EMSCRIPTEN_RESULT_SUCCESS || !g.connected) {
            continue;
        }
        s = !g_plat_sticks[0].present ? &g_plat_sticks[0]
            : !g_plat_sticks[1].present ? &g_plat_sticks[1] : NULL;
        if (s != NULL && g.numAxes >= 2) {
            /* The standard mapping: A is 0, B 1, the triggers 6 and 7. */
            s->present = 1;
            s->x = (int)(g.axis[0] * 100.0);
            s->y = (int)(g.axis[1] * 100.0);
            s->buttons[0] = (g.numButtons > 7 && g.analogButton[7] > 0.25) || g.digitalButton[0];
            s->buttons[1] = (g.numButtons > 6 && g.analogButton[6] > 0.25) || g.digitalButton[1];
        }
        for (k = 0; k < g.numButtons && k < 17; k++) {
            if (g.digitalButton[k] || g.analogButton[k] > 0.5) {
                b |= map[k];
            }
        }
        if (g.numAxes >= 2) {
            b |= g.axis[0] < -0.5 ? PAD_LEFT : g.axis[0] > 0.5 ? PAD_RIGHT : 0;
            b |= g.axis[1] < -0.5 ? PAD_UP : g.axis[1] > 0.5 ? PAD_DOWN : 0;
        }
    }
    return b;
}

/* ------------------------------------------------------------ display */

/* The canvas's pixels follow its size on the page. */
static void fit_canvas(void)
{
    double cw, ch, ratio = emscripten_get_device_pixel_ratio();
    int w, h;

    if (emscripten_get_element_css_size("#canvas", &cw, &ch) != EMSCRIPTEN_RESULT_SUCCESS) {
        return;
    }
    emscripten_get_canvas_element_size("#canvas", &w, &h);
    if (w != (int)(cw * ratio) || h != (int)(ch * ratio)) {
        emscripten_set_canvas_element_size("#canvas", (int)(cw * ratio), (int)(ch * ratio));
    }
}

/* The screen is the window: nothing to size. */
void plat_window_size(int width, int height, int *old_width, int *old_height)
{
    (void)width;
    (void)height;
    *old_width = *old_height = 0;
}

int plat_window_open(const char *title, int width, int height, int fullscreen)
{
    EmscriptenWebGLContextAttributes attrs;

    (void)width;
    (void)height;
    (void)fullscreen;
    if (s_context > 0) {
        return 1;
    }
    emscripten_set_window_title(title);
    fit_canvas();
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2;
    attrs.minorVersion = 0;
    attrs.alpha = EM_FALSE;
    attrs.depth = EM_TRUE;
    attrs.antialias = EM_FALSE;
    s_context = emscripten_webgl_create_context("#canvas", &attrs);
    if (s_context <= 0) {
        fprintf(stderr, "ignition: no WebGL 2\n");
        return 0;
    }
    emscripten_webgl_make_context_current(s_context);
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, EM_TRUE, on_key);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, EM_TRUE, on_key);
    emscripten_set_touchstart_callback("#canvas", NULL, EM_TRUE, on_touch);
    emscripten_set_touchmove_callback("#canvas", NULL, EM_TRUE, on_touch);
    emscripten_set_touchend_callback("#canvas", NULL, EM_TRUE, on_touch);
    emscripten_set_touchcancel_callback("#canvas", NULL, EM_TRUE, on_touch);
    return 1;
}

void plat_destroy_window(void)
{
}

void plat_drawable_size(int *width, int *height)
{
    emscripten_get_canvas_element_size("#canvas", width, height);
}

/* The page shows the frame when control goes back to the browser. */
void plat_present(void)
{
    int w, h;

    plat_drawable_size(&w, &h);
    touch_draw(w, h);
    web_wait_frame();
    s_last_yield = plat_time_ms();
}

void plat_request_quit(void)
{
    s_quit = 1;
}

void plat_pump_events(void)
{
    int w, h;

    plat_run_scheduled();
    maybe_yield();
    fit_canvas();
    plat_drawable_size(&w, &h);
    plat_pad_update(gamepad_buttons() | touch_buttons(w, h));
}

/* ------------------------------------------------------------ audio */

enum { AUDIO_FRAMES = 2048 };

static plat_audio_fn s_audio_cb;
static void *s_audio_user;
static float s_audio_buf[AUDIO_FRAMES * 2];

/* Called by the page's ScriptProcessorNode: fills `frames` interleaved
 * stereo floats and returns where they are. */
EMSCRIPTEN_KEEPALIVE float *web_audio_fill(int frames)
{
    if (frames > AUDIO_FRAMES) {
        frames = AUDIO_FRAMES;
    }
    if (s_audio_cb != NULL) {
        s_audio_cb(s_audio_buf, frames, s_audio_user);
    } else {
        memset(s_audio_buf, 0, sizeof(s_audio_buf));
    }
    return s_audio_buf;
}

EM_JS(int, web_audio_start, (int rate, int frames), {
    var Ctx = window.AudioContext || window.webkitAudioContext;
    if (!Ctx) return 0;
    var ctx;
    try { ctx = new Ctx({ sampleRate: rate }); } catch (e) { ctx = new Ctx(); }
    var node = ctx.createScriptProcessor(frames, 0, 2);
    node.onaudioprocess = function(ev) {
        var l = ev.outputBuffer.getChannelData(0);
        var r = ev.outputBuffer.getChannelData(1);
        var n = l.length;
        var p = Module._web_audio_fill(n) >> 2;
        var heap = HEAPF32;
        for (var i = 0; i < n; i++) {
            l[i] = heap[p + 2 * i];
            r[i] = heap[p + 2 * i + 1];
        }
    };
    node.connect(ctx.destination);
    /* The page resumes it on the first key press or touch. */
    window.ignitionAudio = ctx;
    return ctx.sampleRate | 0;
});

EM_JS(void, web_audio_stop, (void), {
    if (window.ignitionAudio) {
        window.ignitionAudio.close();
        window.ignitionAudio = null;
    }
});

/* Web Audio at `rate` when the browser allows it; resampling to another
 * rate is the browser's. */
int plat_audio_open(s32 rate, plat_audio_fn cb, void *user)
{
    s_audio_cb = cb;
    s_audio_user = user;
    if (web_audio_start(rate, AUDIO_FRAMES) == 0) {
        s_audio_cb = NULL;
        return 0;
    }
    return 1;
}

void plat_audio_close(void)
{
    web_audio_stop();
    s_audio_cb = NULL;
}

/* ------------------------------------------------------------ loop */

/* A page has no other window to lose focus to: the game always runs. */
static int run(int (*frame)(void *), void *user)
{
    while (!s_quit) {
        g_app_active = 1;
        plat_pump_events();
        if (!frame(user)) {
            plat_request_quit();
        }
        maybe_yield();
    }
    plat_audio_close();
    return 0;
}

static int tick(void *user)
{
    (void)user;
    return app_tick();
}

int plat_run(void)
{
    return run(tick, NULL);
}

int plat_run_with(int (*frame)(void *user), void *user)
{
    return run(frame, user);
}

/* ------------------------------------------------------------ folders */

const char *plat_host_exe_dir(void)
{
    return NULL;
}

/* IndexedDB, mounted by the page. */
const char *plat_host_pref_dir(void)
{
    return "/ignition";
}

/* ------------------------------------------------------------ network */

/* A browser has no UDP: network play is not available on the web. */
int plat_net_open(u16 port)
{
    (void)port;
    return 0;
}

void plat_net_close(void)
{
}

int plat_net_send(const struct plat_net_addr *to, const void *buf, int size)
{
    (void)to;
    (void)buf;
    (void)size;
    return 0;
}

int plat_net_recv(struct plat_net_addr *from, void *buf, int size)
{
    (void)from;
    (void)buf;
    (void)size;
    return -1;
}

void plat_net_wait(u32 ms)
{
    (void)ms;
}

int plat_net_resolve(const char *name, u32 *ip)
{
    (void)name;
    (void)ip;
    return 0;
}
