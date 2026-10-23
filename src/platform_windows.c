/*
 * platform_windows.c - Win32 window with a WGL OpenGL 3.2 core context,
 * keyboard, XInput gamepads, waveOut audio and the clock.
 *
 * The message loop is the original's (WinMain 0x4120a0): while the app
 * is active (WM_ACTIVATEAPP) it drains waiting messages and runs one
 * app_tick; while inactive it only waits for messages.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <xinput.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "dik.h"
#include "platform.h"
#include "platform_common.h"
#include "rnd_gl.h"

static HWND s_window;
static HDC s_dc;
static HGLRC s_context;
static HMODULE s_opengl32;
static int s_fullscreen;
static RECT s_windowed;
static int s_active = 1;
static int s_quit;
static int s_close_requested;

/* ------------------------------------------------------------ clock */

u32 plat_time_ms(void)
{
    static LARGE_INTEGER freq, start;
    LARGE_INTEGER now;

    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
    }
    QueryPerformanceCounter(&now);
    return (u32)((now.QuadPart - start.QuadPart) * 1000 / freq.QuadPart);
}

/* ------------------------------------------------------------ keys */

/* The scan code in a key message's lParam, with the extended bit as
 * DirectInput's 0x80. */
static int map_key(LPARAM lp)
{
    int sc = (int)((lp >> 16) & 0xff);

    if (lp & (1 << 24)) {
        sc |= 0x80;
    }
    return sc;
}

static void key_message(UINT msg, WPARAM wp, LPARAM lp)
{
    int dik = map_key(lp);
    int down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;

    if (dik == 0) {
        return;
    }
    if (down) {
        int repeat = (lp & (1 << 30)) != 0;
        int alt = (lp & (1 << 29)) != 0;

        if (repeat) {
            return;
        }
        /* Alt+Enter, the original's WM_SYSKEYDOWN with VK_RETURN. */
        if (alt && wp == VK_RETURN) {
            app_toggle_fullscreen();
            return;
        }
        /* Alt+R switches between the GPU and the original's CPU 2D. */
        if (alt && wp == 'R') {
            app_toggle_renderer();
            return;
        }
        if (!g_plat_keys[dik]) {
            plat_key_event(dik, 1);
        }
    } else if (g_plat_keys[dik]) {
        plat_key_event(dik, 0);
    }
}

/* ------------------------------------------------------------ window */

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ACTIVATEAPP:
        s_active = wp != 0;
        return 0;
    case WM_CLOSE:
        s_close_requested = 1;
        return 0;
    case WM_KEYDOWN:
    case WM_KEYUP:
        key_message(msg, wp, lp);
        return 0;
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        if (wp == VK_F4) {
            break;
        }
        /* Not to DefWindowProc: Alt would open the window menu. */
        key_message(msg, wp, lp);
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_KEYMENU) {
            return 0;
        }
        break;
    default:
        break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void *gl_proc(const char *name)
{
    void *p = (void *)wglGetProcAddress(name);

    /* OpenGL 1.1's entry points come from opengl32.dll itself, and
     * wglGetProcAddress may answer them with these values. */
    if (p == NULL || p == (void *)1 || p == (void *)2 || p == (void *)3 || p == (void *)-1) {
        p = (void *)GetProcAddress(s_opengl32, name);
    }
    return p;
}

typedef HGLRC (WINAPI *create_context_fn)(HDC, HGLRC, const int *);
typedef BOOL (WINAPI *swap_interval_fn)(int);

/* WGL_ARB_create_context and WGL_ARB_create_context_profile. */
enum {
    WGL_CONTEXT_MAJOR_VERSION_ARB = 0x2091, WGL_CONTEXT_MINOR_VERSION_ARB = 0x2092,
    WGL_CONTEXT_FLAGS_ARB = 0x2094, WGL_CONTEXT_PROFILE_MASK_ARB = 0x9126,
    WGL_CONTEXT_CORE_PROFILE_BIT_ARB = 1, WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB = 2
};

static int make_context(void)
{
    static const int attrs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 3, WGL_CONTEXT_MINOR_VERSION_ARB, 2,
        WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB, 0
    };
    PIXELFORMATDESCRIPTOR pfd;
    HGLRC legacy;
    create_context_fn create;
    swap_interval_fn swap_interval;
    int format;

    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    format = ChoosePixelFormat(s_dc, &pfd);
    if (format == 0 || !SetPixelFormat(s_dc, format, &pfd)) {
        return 0;
    }
    /* A legacy context first, to look up wglCreateContextAttribsARB. */
    legacy = wglCreateContext(s_dc);
    if (legacy == NULL) {
        return 0;
    }
    wglMakeCurrent(s_dc, legacy);
    create = (create_context_fn)(void *)wglGetProcAddress("wglCreateContextAttribsARB");
    if (create != NULL) {
        s_context = create(s_dc, NULL, attrs);
    }
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(legacy);
    if (s_context == NULL) {
        return 0;
    }
    wglMakeCurrent(s_dc, s_context);
    swap_interval = (swap_interval_fn)(void *)wglGetProcAddress("wglSwapIntervalEXT");
    if (swap_interval != NULL) {
        swap_interval(1);
    }
    s_opengl32 = LoadLibraryA("opengl32.dll");
    return rnd_gl_load(gl_proc);
}

/* Full screen is a borderless window over the whole monitor. */
static void set_fullscreen(int on)
{
    if (on) {
        MONITORINFO mi;

        mi.cbSize = sizeof(mi);
        GetWindowRect(s_window, &s_windowed);
        GetMonitorInfoA(MonitorFromWindow(s_window, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongA(s_window, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(s_window, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED);
    } else {
        SetWindowLongA(s_window, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(s_window, NULL, s_windowed.left, s_windowed.top,
                     s_windowed.right - s_windowed.left, s_windowed.bottom - s_windowed.top,
                     SWP_FRAMECHANGED | SWP_NOZORDER);
    }
    s_fullscreen = on;
}

int plat_window_open(const char *title, int width, int height, int fullscreen)
{
    if (s_window == NULL) {
        WNDCLASSA wc;
        RECT r = { 0, 0, 0, 0 };
        HINSTANCE inst = GetModuleHandleA(NULL);

        /* Pixels, not scaled points, on a high DPI display. */
        SetProcessDPIAware();
        memset(&wc, 0, sizeof(wc));
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = window_proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hIcon = LoadIcon(inst, MAKEINTRESOURCE(1));
        wc.lpszClassName = "Ignition";
        RegisterClassA(&wc);
        r.right = width;
        r.bottom = height;
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        s_window = CreateWindowA("Ignition", title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                 CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL,
                                 inst, NULL);
        if (s_window == NULL) {
            return 0;
        }
        s_dc = GetDC(s_window);
        if (!make_context()) {
            fprintf(stderr, "ignition: no OpenGL 3.2 core context\n");
            return 0;
        }
        ShowWindow(s_window, SW_SHOW);
        SetForegroundWindow(s_window);
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
    RECT r;

    GetClientRect(s_window, &r);
    *width = r.right - r.left;
    *height = r.bottom - r.top;
}

void plat_present(void)
{
    SwapBuffers(s_dc);
}

void plat_request_quit(void)
{
    s_quit = 1;
}

/* ------------------------------------------------------------ gamepads */

typedef DWORD (WINAPI *xinput_get_state_fn)(DWORD, XINPUT_STATE *);

static xinput_get_state_fn s_xinput_get_state;
static int s_xinput_tried;

static u32 xinput_buttons(void)
{
    u32 b = 0;
    DWORD i;

    if (!s_xinput_tried) {
        HMODULE lib = LoadLibraryA("xinput1_4.dll");

        s_xinput_tried = 1;
        if (lib == NULL) {
            lib = LoadLibraryA("xinput9_1_0.dll");
        }
        if (lib != NULL) {
            s_xinput_get_state = (xinput_get_state_fn)(void *)GetProcAddress(lib, "XInputGetState");
        }
    }
    if (s_xinput_get_state == NULL) {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        XINPUT_STATE st;
        WORD w;

        if (s_xinput_get_state(i, &st) != ERROR_SUCCESS) {
            continue;
        }
        w = st.Gamepad.wButtons;
        b |= (w & XINPUT_GAMEPAD_DPAD_LEFT) || st.Gamepad.sThumbLX < -16384 ? PAD_LEFT : 0;
        b |= (w & XINPUT_GAMEPAD_DPAD_RIGHT) || st.Gamepad.sThumbLX > 16384 ? PAD_RIGHT : 0;
        b |= (w & XINPUT_GAMEPAD_DPAD_UP) || st.Gamepad.sThumbLY > 16384 ? PAD_UP : 0;
        b |= (w & XINPUT_GAMEPAD_DPAD_DOWN) || st.Gamepad.sThumbLY < -16384 ? PAD_DOWN : 0;
        b |= (w & XINPUT_GAMEPAD_A) ? PAD_A : 0;
        b |= (w & XINPUT_GAMEPAD_B) ? PAD_B : 0;
        b |= (w & XINPUT_GAMEPAD_X) ? PAD_X : 0;
        b |= (w & XINPUT_GAMEPAD_Y) ? PAD_Y : 0;
        b |= (w & XINPUT_GAMEPAD_LEFT_SHOULDER) || st.Gamepad.bLeftTrigger > 64 ? PAD_L : 0;
        b |= (w & XINPUT_GAMEPAD_RIGHT_SHOULDER) || st.Gamepad.bRightTrigger > 64 ? PAD_R : 0;
        b |= (w & XINPUT_GAMEPAD_START) ? PAD_START : 0;
        b |= (w & XINPUT_GAMEPAD_BACK) ? PAD_SELECT : 0;
    }
    return b;
}

static void drain_messages(void)
{
    MSG msg;

    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            s_quit = 1;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

void plat_pump_events(void)
{
    plat_run_scheduled();
    drain_messages();
    plat_pad_update(xinput_buttons());
}

/* ------------------------------------------------------------ audio */

enum { WAVE_BUFFERS = 4, WAVE_FRAMES = 1024 };

static HWAVEOUT s_wave;
static WAVEHDR s_hdr[WAVE_BUFFERS];
static s16 s_pcm[WAVE_BUFFERS][WAVE_FRAMES * 2];
static float s_mix[WAVE_FRAMES * 2];
static HANDLE s_wave_event, s_wave_thread;
static volatile LONG s_wave_running;
static plat_audio_fn s_audio_cb;
static void *s_audio_user;

static void fill(int i)
{
    int k;

    s_audio_cb(s_mix, WAVE_FRAMES, s_audio_user);
    for (k = 0; k < WAVE_FRAMES * 2; k++) {
        float v = s_mix[k];

        v = v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
        s_pcm[i][k] = (s16)(v * 32767.0f);
    }
    waveOutWrite(s_wave, &s_hdr[i], sizeof(WAVEHDR));
}

/* Refills each buffer the device has played. */
static DWORD WINAPI wave_thread(LPVOID arg)
{
    (void)arg;
    while (s_wave_running) {
        int i;

        WaitForSingleObject(s_wave_event, 100);
        for (i = 0; i < WAVE_BUFFERS && s_wave_running; i++) {
            if (s_hdr[i].dwFlags & WHDR_DONE) {
                fill(i);
            }
        }
    }
    return 0;
}

int plat_audio_open(s32 rate, plat_audio_fn cb, void *user)
{
    WAVEFORMATEX wf;
    int i;

    memset(&wf, 0, sizeof(wf));
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 2;
    wf.nSamplesPerSec = (DWORD)rate;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = 4;
    wf.nAvgBytesPerSec = (DWORD)rate * 4;
    s_wave_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (waveOutOpen(&s_wave, WAVE_MAPPER, &wf, (DWORD_PTR)s_wave_event, 0, CALLBACK_EVENT)
        != MMSYSERR_NOERROR) {
        CloseHandle(s_wave_event);
        return 0;
    }
    s_audio_cb = cb;
    s_audio_user = user;
    for (i = 0; i < WAVE_BUFFERS; i++) {
        memset(&s_hdr[i], 0, sizeof(WAVEHDR));
        s_hdr[i].lpData = (LPSTR)s_pcm[i];
        s_hdr[i].dwBufferLength = sizeof(s_pcm[i]);
        waveOutPrepareHeader(s_wave, &s_hdr[i], sizeof(WAVEHDR));
        fill(i);
    }
    s_wave_running = 1;
    s_wave_thread = CreateThread(NULL, 0, wave_thread, NULL, 0, NULL);
    return 1;
}

void plat_audio_close(void)
{
    int i;

    if (s_wave == NULL) {
        return;
    }
    s_wave_running = 0;
    SetEvent(s_wave_event);
    WaitForSingleObject(s_wave_thread, INFINITE);
    CloseHandle(s_wave_thread);
    waveOutReset(s_wave);
    for (i = 0; i < WAVE_BUFFERS; i++) {
        waveOutUnprepareHeader(s_wave, &s_hdr[i], sizeof(WAVEHDR));
    }
    waveOutClose(s_wave);
    CloseHandle(s_wave_event);
    s_wave = NULL;
}

/* ------------------------------------------------------------ loop */

static int run(int (*frame)(void *), void *user, int only_active)
{
    if (g_plat_run_inactive) {
        only_active = 0;
    }
    while (!s_quit) {
        int active = s_active || !only_active;

        g_app_active = active;
        if (!active) {
            MsgWaitForMultipleObjects(0, NULL, FALSE, 100, QS_ALLINPUT);
        }
        plat_pump_events();
        if (s_close_requested) {
            s_close_requested = 0;
            app_on_destroy();
            continue;
        }
        if (active && !s_quit && !frame(user)) {
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

const char *plat_host_exe_dir(void)
{
    static char dir[MAX_PATH];
    char *slash;

    if (GetModuleFileNameA(NULL, dir, sizeof(dir)) == 0) {
        return NULL;
    }
    slash = strrchr(dir, '\\');
    if (slash == NULL) {
        return NULL;
    }
    *slash = '\0';
    return dir;
}

/* %APPDATA%\ignition */
const char *plat_host_pref_dir(void)
{
    static char dir[MAX_PATH];
    const char *appdata = getenv("APPDATA");

    if (appdata == NULL || appdata[0] == '\0') {
        return NULL;
    }
    snprintf(dir, sizeof(dir), "%s\\ignition", appdata);
    return dir;
}
