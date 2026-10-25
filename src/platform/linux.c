/*
 * platform/linux.c - X11 window with a GLX OpenGL 3.2 core context,
 * keyboard, gamepads through the kernel's joystick API, ALSA audio and
 * the clock. Also the Steam Deck's host.
 *
 * The message loop keeps the Cocoa host's shape (platform/macos.m),
 * itself the original's WinMain 0x4120a0: while the window has the focus
 * it drains waiting events and runs one app_tick; without the focus it
 * only waits for events.
 */

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/Xutil.h>
#include <GL/glx.h>
#include <alsa/asoundlib.h>
#include <linux/joystick.h>
#include <linux/input.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#include "app.h"
#include "dik.h"
#include "platform/platform.h"
#include "platform/common.h"
#include "rnd_gl.h"

static Display *s_display;
static Window s_window;
static GLXContext s_context;
static Atom s_wm_delete;
static int s_width = 1280, s_height = 720;
static int s_fullscreen;
static int s_focused = 1;
static int s_quit;
static int s_close_requested;

/* ------------------------------------------------------------ clock */

u32 plat_time_ms(void)
{
    static struct timespec start;
    struct timespec now;

    if (start.tv_sec == 0 && start.tv_nsec == 0) {
        clock_gettime(CLOCK_MONOTONIC, &start);
    }
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (u32)((now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000);
}

/* ------------------------------------------------------------ window */

typedef GLXContext (*create_context_fn)(Display *, GLXFBConfig, GLXContext, Bool, const int *);
typedef void (*swap_interval_fn)(Display *, GLXDrawable, int);

static void *gl_proc(const char *name)
{
    return (void *)glXGetProcAddressARB((const GLubyte *)name);
}

/* Asks the window manager for full screen (EWMH), or back. */
static void set_fullscreen(int on)
{
    XEvent ev;

    memset(&ev, 0, sizeof(ev));
    ev.type = ClientMessage;
    ev.xclient.window = s_window;
    ev.xclient.message_type = XInternAtom(s_display, "_NET_WM_STATE", False);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = on ? 1 : 0;
    ev.xclient.data.l[1] = (long)XInternAtom(s_display, "_NET_WM_STATE_FULLSCREEN", False);
    ev.xclient.data.l[3] = 1;
    XSendEvent(s_display, DefaultRootWindow(s_display), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XFlush(s_display);
    s_fullscreen = on;
}

void plat_window_size(int width, int height, int *old_width, int *old_height)
{
    *old_width = s_width;
    *old_height = s_height;
    if (s_window == 0 || s_fullscreen) {
        return;
    }
    XResizeWindow(s_display, s_window, (unsigned)width, (unsigned)height);
}

int plat_window_open(const char *title, int width, int height, int fullscreen)
{
    if (s_window == 0) {
        static const int fb_attrs[] = {
            GLX_X_RENDERABLE, True, GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
            GLX_RENDER_TYPE, GLX_RGBA_BIT, GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
            GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, 8,
            GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, True, None
        };
        static const int ctx_attrs[] = {
            GLX_CONTEXT_MAJOR_VERSION_ARB, 3, GLX_CONTEXT_MINOR_VERSION_ARB, 2,
            GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
            GLX_CONTEXT_FLAGS_ARB, GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB, None
        };
        GLXFBConfig *configs;
        XVisualInfo *vi;
        XSetWindowAttributes swa;
        create_context_fn create;
        swap_interval_fn swap_interval;
        int n = 0;

        s_display = XOpenDisplay(NULL);
        if (s_display == NULL) {
            fprintf(stderr, "ignition: cannot open the X display\n");
            return 0;
        }
        configs = glXChooseFBConfig(s_display, DefaultScreen(s_display), fb_attrs, &n);
        if (configs == NULL || n == 0) {
            fprintf(stderr, "ignition: no GLX frame buffer configuration\n");
            return 0;
        }
        vi = glXGetVisualFromFBConfig(s_display, configs[0]);
        memset(&swa, 0, sizeof(swa));
        swa.colormap = XCreateColormap(s_display, DefaultRootWindow(s_display), vi->visual,
                                       AllocNone);
        swa.event_mask = KeyPressMask | KeyReleaseMask | FocusChangeMask | StructureNotifyMask
                       | ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
        s_width = width;
        s_height = height;
        s_window = XCreateWindow(s_display, DefaultRootWindow(s_display), 0, 0,
                                 (unsigned)width, (unsigned)height, 0, vi->depth, InputOutput,
                                 vi->visual, CWColormap | CWEventMask, &swa);
        XFree(vi);
        XStoreName(s_display, s_window, title);
        s_wm_delete = XInternAtom(s_display, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(s_display, s_window, &s_wm_delete, 1);
        XkbSetDetectableAutoRepeat(s_display, True, NULL);

        create = (create_context_fn)gl_proc("glXCreateContextAttribsARB");
        if (create != NULL) {
            s_context = create(s_display, configs[0], NULL, True, ctx_attrs);
        }
        XFree(configs);
        if (s_context == NULL) {
            fprintf(stderr, "ignition: no OpenGL 3.2 core context\n");
            return 0;
        }
        XMapWindow(s_display, s_window);
        glXMakeCurrent(s_display, s_window, s_context);
        swap_interval = (swap_interval_fn)gl_proc("glXSwapIntervalEXT");
        if (swap_interval != NULL) {
            swap_interval(s_display, s_window, 1);
        }
        if (!rnd_gl_load(gl_proc)) {
            return 0;
        }
    }
    if ((fullscreen != 0) != s_fullscreen) {
        set_fullscreen(fullscreen != 0);
    }
    return 1;
}

/* The original destroys the window to switch modes; here the window and
 * its context live on and plat_window_open switches the mode. */
void plat_destroy_window(void)
{
}

void plat_drawable_size(int *width, int *height)
{
    *width = s_width;
    *height = s_height;
}

void plat_present(void)
{
    glXSwapBuffers(s_display, s_window);
}

void plat_request_quit(void)
{
    s_quit = 1;
}

/* ------------------------------------------------------------ keys */

/* X key codes are evdev codes + 8, and evdev numbers the keys of the
 * main block as the PC's set 1 scan codes, which DirectInput uses.
 * Only the extended keys differ. */
static int map_keycode(unsigned keycode)
{
    unsigned code = keycode - 8;

    switch (code) {
    case KEY_KPENTER: return DIK_NUMPADENTER;
    case KEY_RIGHTCTRL: return DIK_RCONTROL;
    case KEY_KPSLASH: return DIK_DIVIDE;
    case KEY_RIGHTALT: return DIK_RMENU;
    case KEY_HOME: return DIK_HOME;
    case KEY_UP: return DIK_UP;
    case KEY_PAGEUP: return DIK_PRIOR;
    case KEY_LEFT: return DIK_LEFT;
    case KEY_RIGHT: return DIK_RIGHT;
    case KEY_END: return DIK_END;
    case KEY_DOWN: return DIK_DOWN;
    case KEY_PAGEDOWN: return DIK_NEXT;
    case KEY_INSERT: return DIK_INSERT;
    case KEY_DELETE: return DIK_DELETE;
    case KEY_LEFTMETA: return DIK_LWIN;
    default:
        return code >= 1 && code <= 0x58 ? (int)code : 0;
    }
}

static void handle_event(XEvent *ev)
{
    switch (ev->type) {
    case KeyPress: {
        int dik = map_keycode(ev->xkey.keycode);

        /* Alt+Enter, the original's WM_SYSKEYDOWN with VK_RETURN. */
        if (dik == DIK_RETURN && (ev->xkey.state & Mod1Mask)) {
            app_toggle_fullscreen();
            break;
        }
        /* Alt+R switches between the GPU and the original's CPU 2D. */
        if (dik == DIK_R && (ev->xkey.state & Mod1Mask)) {
            app_toggle_renderer();
            break;
        }
        /* Alt+F shows the race's debug lines. */
        if (dik == DIK_F && (ev->xkey.state & Mod1Mask)) {
            app_toggle_debug();
            break;
        }
        if (dik != 0 && !g_plat_keys[dik]) {
            plat_key_event(dik, 1);
        }
        break;
    }
    case KeyRelease: {
        int dik = map_keycode(ev->xkey.keycode);

        if (dik != 0 && g_plat_keys[dik]) {
            plat_key_event(dik, 0);
        }
        break;
    }
    case FocusIn:
        s_focused = 1;
        break;
    case FocusOut:
        s_focused = 0;
        break;
    case ConfigureNotify:
        s_width = ev->xconfigure.width;
        s_height = ev->xconfigure.height;
        break;
    case ClientMessage:
        if ((Atom)ev->xclient.data.l[0] == s_wm_delete) {
            s_close_requested = 1;
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------ gamepads */

/* /dev/input/js*. The driver tells which event code each button and
 * axis number has (JSIOCGBTNMAP, JSIOCGAXMAP), so pads are read by
 * meaning, not by position. */
enum { PADS = 4, BUTTONS = 64, AXES = 32, PUSH = 16384 };

static struct pad {
    int fd, index;
    u16 btn_code[BUTTONS];
    u8 ax_code[AXES];
    u8 buttons[BUTTONS];
    s16 axes[AXES];
    s16 rest[AXES];
    u8 seen[AXES];
} s_pads[PADS];
static u32 s_next_scan;

static void pad_scan(void)
{
    int n, i;

    for (n = 0; n < 16; n++) {
        char path[32];
        struct pad *p = NULL;
        u8 nbtn = 0;
        int fd, open_already = 0;

        for (i = 0; i < PADS; i++) {
            if (s_pads[i].fd > 0 && s_pads[i].index == n) {
                open_already = 1;
            }
            if (s_pads[i].fd <= 0 && p == NULL) {
                p = &s_pads[i];
            }
        }
        if (open_already || p == NULL) {
            continue;
        }
        snprintf(path, sizeof(path), "/dev/input/js%d", n);
        fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }
        memset(p, 0, sizeof(*p));
        if (ioctl(fd, JSIOCGBUTTONS, &nbtn) < 0 || ioctl(fd, JSIOCGBTNMAP, p->btn_code) < 0
            || ioctl(fd, JSIOCGAXMAP, p->ax_code) < 0) {
            close(fd);
            continue;
        }
        p->fd = fd;
        p->index = n;
    }
}

static u32 pad_read(struct pad *p)
{
    struct js_event ev;
    u32 b = 0;
    int i;

    errno = 0;
    while (read(p->fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        ev.type &= ~JS_EVENT_INIT;
        if (ev.type == JS_EVENT_BUTTON && ev.number < BUTTONS) {
            p->buttons[ev.number] = ev.value != 0;
        } else if (ev.type == JS_EVENT_AXIS && ev.number < AXES) {
            p->axes[ev.number] = ev.value;
            if (!p->seen[ev.number]) {
                p->seen[ev.number] = 1;
                p->rest[ev.number] = ev.value;
            }
        }
    }
    if (errno == ENODEV) {
        close(p->fd);
        p->fd = 0;
        return 0;
    }
    for (i = 0; i < BUTTONS; i++) {
        if (!p->buttons[i]) {
            continue;
        }
        switch (p->btn_code[i]) {
        case BTN_SOUTH: b |= PAD_A; break;
        case BTN_EAST: b |= PAD_B; break;
        /* BTN_X and BTN_Y are BTN_NORTH and BTN_WEST: the Xbox letters. */
        case BTN_X: b |= PAD_X; break;
        case BTN_Y: b |= PAD_Y; break;
        case BTN_TL: case BTN_TL2: b |= PAD_L; break;
        case BTN_TR: case BTN_TR2: b |= PAD_R; break;
        case BTN_START: b |= PAD_START; break;
        case BTN_SELECT: b |= PAD_SELECT; break;
        case BTN_DPAD_UP: b |= PAD_UP; break;
        case BTN_DPAD_DOWN: b |= PAD_DOWN; break;
        case BTN_DPAD_LEFT: b |= PAD_LEFT; break;
        case BTN_DPAD_RIGHT: b |= PAD_RIGHT; break;
        default: break;
        }
    }
    for (i = 0; i < AXES; i++) {
        int v = p->axes[i];

        switch (p->ax_code[i]) {
        case ABS_X: case ABS_HAT0X:
            b |= v < -PUSH ? PAD_LEFT : v > PUSH ? PAD_RIGHT : 0;
            break;
        case ABS_Y: case ABS_HAT0Y:
            b |= v < -PUSH ? PAD_UP : v > PUSH ? PAD_DOWN : 0;
            break;
        /* Triggers, measured from where they rest. */
        case ABS_Z: case ABS_HAT2Y:
            b |= v - p->rest[i] > PUSH ? PAD_L : 0;
            break;
        case ABS_RZ: case ABS_HAT2X:
            b |= v - p->rest[i] > PUSH ? PAD_R : 0;
            break;
        default:
            break;
        }
    }
    return b;
}

/* A pad as the game's joystick: its stick, and its accelerate and brake
 * buttons (A or the right trigger, B or the left). */
static void pad_stick(const struct pad *p, struct plat_stick *s)
{
    int i;

    s->present = 1;
    s->x = s->y = 0;
    s->buttons[0] = s->buttons[1] = 0;
    for (i = 0; i < AXES; i++) {
        int v = p->axes[i];

        switch (p->ax_code[i]) {
        case ABS_X: s->x = v * 100 / 32767; break;
        case ABS_Y: s->y = v * 100 / 32767; break;
        case ABS_RZ: s->buttons[0] |= v - p->rest[i] > PUSH; break;
        case ABS_Z: s->buttons[1] |= v - p->rest[i] > PUSH; break;
        default: break;
        }
    }
    for (i = 0; i < BUTTONS; i++) {
        if (p->buttons[i] && (p->btn_code[i] == BTN_SOUTH || p->btn_code[i] == BTN_TR2)) {
            s->buttons[0] = 1;
        }
        if (p->buttons[i] && (p->btn_code[i] == BTN_EAST || p->btn_code[i] == BTN_TL2)) {
            s->buttons[1] = 1;
        }
    }
}

static void poll_pads(void)
{
    u32 b = 0;
    int i, n = 0;

    if (plat_time_ms() >= s_next_scan) {
        s_next_scan = plat_time_ms() + 1000;
        pad_scan();
    }
    g_plat_sticks[0].present = g_plat_sticks[1].present = 0;
    for (i = 0; i < PADS; i++) {
        if (s_pads[i].fd > 0) {
            b |= pad_read(&s_pads[i]);
            if (s_pads[i].fd > 0 && n < 2) {
                pad_stick(&s_pads[i], &g_plat_sticks[n++]);
            }
        }
    }
    plat_pad_update(b);
}

void plat_pump_events(void)
{
    plat_run_scheduled();
    while (s_display != NULL && XPending(s_display)) {
        XEvent ev;

        XNextEvent(s_display, &ev);
        handle_event(&ev);
    }
    poll_pads();
}

/* ------------------------------------------------------------ audio */

enum { PERIOD = 1024 };

static snd_pcm_t *s_pcm;
static pthread_t s_audio_thread;
static volatile int s_audio_running;
static plat_audio_fn s_audio_cb;
static void *s_audio_user;

static void *audio_thread(void *arg)
{
    static float mix[PERIOD * 2];
    static s16 out[PERIOD * 2];

    (void)arg;
    while (s_audio_running) {
        snd_pcm_sframes_t n;
        int i;

        s_audio_cb(mix, PERIOD, s_audio_user);
        for (i = 0; i < PERIOD * 2; i++) {
            float v = mix[i];

            v = v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
            out[i] = (s16)(v * 32767.0f);
        }
        n = snd_pcm_writei(s_pcm, out, PERIOD);
        if (n < 0) {
            snd_pcm_recover(s_pcm, (int)n, 1);
        }
    }
    return NULL;
}

/* ALSA's default device, 16 bit stereo, fed by a thread that pulls `cb`. */
int plat_audio_open(s32 rate, plat_audio_fn cb, void *user)
{
    if (snd_pcm_open(&s_pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        s_pcm = NULL;
        return 0;
    }
    if (snd_pcm_set_params(s_pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2,
                           (unsigned)rate, 1, 80000) < 0) {
        snd_pcm_close(s_pcm);
        s_pcm = NULL;
        return 0;
    }
    s_audio_cb = cb;
    s_audio_user = user;
    s_audio_running = 1;
    if (pthread_create(&s_audio_thread, NULL, audio_thread, NULL) != 0) {
        s_audio_running = 0;
        snd_pcm_close(s_pcm);
        s_pcm = NULL;
        return 0;
    }
    return 1;
}

void plat_audio_close(void)
{
    if (s_pcm != NULL) {
        s_audio_running = 0;
        pthread_join(s_audio_thread, NULL);
        snd_pcm_drop(s_pcm);
        snd_pcm_close(s_pcm);
        s_pcm = NULL;
    }
}

/* ------------------------------------------------------------ loop */

/* Waits up to `ms` for an X event. */
static void wait_event(int ms)
{
    int fd = ConnectionNumber(s_display);
    fd_set set;
    struct timeval tv;

    if (XPending(s_display)) {
        return;
    }
    FD_ZERO(&set);
    FD_SET(fd, &set);
    tv.tv_sec = 0;
    tv.tv_usec = ms * 1000;
    select(fd + 1, &set, NULL, NULL, &tv);
}

static int run(int (*frame)(void *), void *user, int only_active)
{
    if (g_plat_run_inactive) {
        only_active = 0;
    }
    while (!s_quit) {
        int active = s_focused || !only_active;

        g_app_active = active;
        if (!active && s_display != NULL) {
            wait_event(100);
        }
        plat_pump_events();
        if (s_close_requested) {
            s_close_requested = 0;
            app_on_destroy();
            continue;
        }
        if (active && !frame(user)) {
            plat_request_quit();
        }
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
    return run(tick, NULL, 1);
}

int plat_run_with(int (*frame)(void *user), void *user)
{
    return run(frame, user, 0);
}

/* ------------------------------------------------------------ folders */

/* The executable's folder; in an AppImage, the folder holding the
 * AppImage file, not the read-only mount it runs from. */
const char *plat_host_exe_dir(void)
{
    static char dir[1024];
    const char *appimage = getenv("APPIMAGE");
    char *slash;
    ssize_t n;

    if (appimage != NULL && appimage[0] != '\0') {
        snprintf(dir, sizeof(dir), "%s", appimage);
    } else {
        n = readlink("/proc/self/exe", dir, sizeof(dir) - 1);
        if (n <= 0) {
            return NULL;
        }
        dir[n] = '\0';
    }
    slash = strrchr(dir, '/');
    if (slash == NULL) {
        return NULL;
    }
    *slash = '\0';
    return dir;
}

/* $XDG_DATA_HOME/ignition, or ~/.local/share/ignition */
const char *plat_host_pref_dir(void)
{
    static char dir[1024];
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");

    if (xdg != NULL && xdg[0] != '\0') {
        snprintf(dir, sizeof(dir), "%s/ignition", xdg);
    } else if (home != NULL && home[0] != '\0') {
        snprintf(dir, sizeof(dir), "%s/.local/share/ignition", home);
    } else {
        return NULL;
    }
    return dir;
}
