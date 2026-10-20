/*
 * platform_macos.m - Cocoa window, OpenGL 3.2 core context, keyboard and
 * clock.
 *
 * Objective-C with ARC. Everything above this file is C99.
 *
 * The message loop keeps the original's shape (WinMain 0x4120a0): while
 * the app is active it drains waiting events and then runs one app_tick;
 * while inactive it only handles events, so the game and its clock stop.
 */

#import <Cocoa/Cocoa.h>
#import <OpenGL/OpenGL.h>

#include <mach/mach_time.h>
#include <string.h>

#include "app.h"
#include "dik.h"
#include "platform.h"

u8 g_plat_keys[256];

enum { KEY_QUEUE = 64 };

static struct { u8 key, down; } s_key_queue[KEY_QUEUE];
static unsigned s_key_head, s_key_tail;

static NSWindow *s_window;
static NSOpenGLContext *s_context;
static NSView *s_view;
static int s_quit;
static int s_close_requested;
static mach_timebase_info_data_t s_timebase;
static u64 s_start;

/* The close button only asks; the loop shuts down in order, so the GL
 * context stays valid until the game is done with it. */
@interface IgnWindowDelegate : NSObject <NSWindowDelegate>
@end

@implementation IgnWindowDelegate
- (BOOL)windowShouldClose:(NSWindow *)sender
{
    (void)sender;
    s_close_requested = 1;
    return NO;
}
@end

static IgnWindowDelegate *s_delegate;

/* The view takes key events so they do not beep. */
@interface IgnView : NSView
@end

@implementation IgnView
- (BOOL)acceptsFirstResponder
{
    return YES;
}
- (void)keyDown:(NSEvent *)event
{
    (void)event;
}
- (void)keyUp:(NSEvent *)event
{
    (void)event;
}
- (void)flagsChanged:(NSEvent *)event
{
    (void)event;
}
@end

u32 plat_time_ms(void)
{
    u64 t;

    if (s_timebase.denom == 0) {
        mach_timebase_info(&s_timebase);
        s_start = mach_absolute_time();
    }
    t = (mach_absolute_time() - s_start) * s_timebase.numer / s_timebase.denom;
    return (u32)(t / 1000000u);
}

/* macOS virtual key codes (kVK_*) to DirectInput scan codes. */
static int map_keycode(unsigned short kc)
{
    static const u8 map[128] = {
        [0x00] = DIK_A, [0x01] = DIK_S, [0x02] = DIK_D, [0x03] = DIK_F,
        [0x04] = DIK_H, [0x05] = DIK_G, [0x06] = DIK_Z, [0x07] = DIK_X,
        [0x08] = DIK_C, [0x09] = DIK_V, [0x0b] = DIK_B, [0x0c] = DIK_Q,
        [0x0d] = DIK_W, [0x0e] = DIK_E, [0x0f] = DIK_R, [0x10] = DIK_Y,
        [0x11] = DIK_T, [0x12] = DIK_1, [0x13] = DIK_2, [0x14] = DIK_3,
        [0x15] = DIK_4, [0x16] = DIK_6, [0x17] = DIK_5, [0x18] = DIK_EQUALS,
        [0x19] = DIK_9, [0x1a] = DIK_7, [0x1b] = DIK_MINUS, [0x1c] = DIK_8,
        [0x1d] = DIK_0, [0x1e] = DIK_RBRACKET, [0x1f] = DIK_O, [0x20] = DIK_U,
        [0x21] = DIK_LBRACKET, [0x22] = DIK_I, [0x23] = DIK_P, [0x24] = DIK_RETURN,
        [0x25] = DIK_L, [0x26] = DIK_J, [0x27] = DIK_APOSTROPHE, [0x28] = DIK_K,
        [0x29] = DIK_SEMICOLON, [0x2a] = DIK_BACKSLASH, [0x2b] = DIK_COMMA,
        [0x2c] = DIK_SLASH, [0x2d] = DIK_N, [0x2e] = DIK_M, [0x2f] = DIK_PERIOD,
        [0x30] = DIK_TAB, [0x31] = DIK_SPACE, [0x32] = DIK_GRAVE, [0x33] = DIK_BACK,
        [0x35] = DIK_ESCAPE, [0x37] = DIK_LWIN, [0x38] = DIK_LSHIFT,
        [0x39] = DIK_CAPITAL, [0x3a] = DIK_LMENU, [0x3b] = DIK_LCONTROL,
        [0x3c] = DIK_RSHIFT, [0x3d] = DIK_RMENU, [0x3e] = DIK_RCONTROL,
        [0x41] = DIK_DECIMAL, [0x43] = DIK_MULTIPLY, [0x45] = DIK_ADD,
        [0x47] = DIK_NUMLOCK, [0x4b] = DIK_DIVIDE, [0x4c] = DIK_NUMPADENTER,
        [0x4e] = DIK_SUBTRACT, [0x52] = DIK_NUMPAD0, [0x53] = DIK_NUMPAD1,
        [0x54] = DIK_NUMPAD2, [0x55] = DIK_NUMPAD3, [0x56] = DIK_NUMPAD4,
        [0x57] = DIK_NUMPAD5, [0x58] = DIK_NUMPAD6, [0x59] = DIK_NUMPAD7,
        [0x5b] = DIK_NUMPAD8, [0x5c] = DIK_NUMPAD9, [0x60] = DIK_F5,
        [0x61] = DIK_F6, [0x62] = DIK_F7, [0x63] = DIK_F3, [0x64] = DIK_F8,
        [0x65] = DIK_F9, [0x67] = DIK_F11, [0x6d] = DIK_F10, [0x6f] = DIK_F12,
        [0x73] = DIK_HOME, [0x74] = DIK_PRIOR, [0x75] = DIK_DELETE,
        [0x76] = DIK_F4, [0x77] = DIK_END, [0x78] = DIK_F2, [0x79] = DIK_NEXT,
        [0x7a] = DIK_F1, [0x7b] = DIK_LEFT, [0x7c] = DIK_RIGHT,
        [0x7d] = DIK_DOWN, [0x7e] = DIK_UP
    };

    return kc < 128 ? map[kc] : 0;
}

/* A full queue drops the event, as DirectInput's buffer overflows. */
static void key_event(int dik, int down)
{
    if (s_key_head - s_key_tail < KEY_QUEUE) {
        s_key_queue[s_key_head % KEY_QUEUE].key = (u8)dik;
        s_key_queue[s_key_head % KEY_QUEUE].down = (u8)down;
        s_key_head++;
    }
    g_plat_keys[dik] = down ? 0x80 : 0;
}

int plat_next_key_event(int *key, int *down)
{
    if (s_key_tail == s_key_head) {
        return 0;
    }
    *key = s_key_queue[s_key_tail % KEY_QUEUE].key;
    *down = s_key_queue[s_key_tail % KEY_QUEUE].down;
    s_key_tail++;
    return 1;
}

static void set_modifier(NSEventModifierFlags flags, NSEventModifierFlags bit, int dik)
{
    int down = (flags & bit) != 0;

    if (down != (g_plat_keys[dik] != 0)) {
        key_event(dik, down);
    }
}

static void handle_event(NSEvent *ev)
{
    switch ([ev type]) {
    case NSEventTypeKeyDown: {
        int dik = map_keycode([ev keyCode]);

        /* Alt+Enter, the original's WM_SYSKEYDOWN with VK_RETURN. */
        if (dik == DIK_RETURN && ([ev modifierFlags] & NSEventModifierFlagOption)
            && ![ev isARepeat]) {
            app_toggle_fullscreen();
            break;
        }
        if (dik != 0 && ![ev isARepeat]) {
            key_event(dik, 1);
        }
        break;
    }
    case NSEventTypeKeyUp: {
        int dik = map_keycode([ev keyCode]);

        if (dik != 0) {
            key_event(dik, 0);
        }
        break;
    }
    case NSEventTypeFlagsChanged: {
        NSEventModifierFlags f = [ev modifierFlags];

        set_modifier(f, NSEventModifierFlagShift, DIK_LSHIFT);
        set_modifier(f, NSEventModifierFlagControl, DIK_LCONTROL);
        set_modifier(f, NSEventModifierFlagOption, DIK_LMENU);
        break;
    }
    default:
        break;
    }
    [NSApp sendEvent:ev];
}

int plat_window_open(const char *title, int width, int height, int fullscreen)
{
    @autoreleasepool {
        if (s_window == nil) {
            NSRect frame = NSMakeRect(0, 0, width, height);
            NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                             | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
            NSOpenGLPixelFormatAttribute attrs[] = {
                NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
                NSOpenGLPFAColorSize, 24,
                NSOpenGLPFAAlphaSize, 8,
                NSOpenGLPFADepthSize, 24,
                NSOpenGLPFADoubleBuffer,
                NSOpenGLPFAAccelerated,
                0
            };
            NSOpenGLPixelFormat *pf;
            GLint interval = 1;

            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
            s_window = [[NSWindow alloc] initWithContentRect:frame
                                                   styleMask:style
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
            if (s_window == nil) {
                return 0;
            }
            /* ARC owns the window; closing must not release it again. */
            [s_window setReleasedWhenClosed:NO];
            s_delegate = [[IgnWindowDelegate alloc] init];
            [s_window setDelegate:s_delegate];
            [s_window setTitle:[NSString stringWithUTF8String:title]];
            [s_window setCollectionBehavior:NSWindowCollectionBehaviorFullScreenPrimary];
            [s_window center];

            pf = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
            if (pf == nil) {
                NSLog(@"ignition: no OpenGL 3.2 core pixel format");
                return 0;
            }
            s_context = [[NSOpenGLContext alloc] initWithFormat:pf shareContext:nil];
            if (s_context == nil) {
                return 0;
            }
            s_view = [[IgnView alloc] initWithFrame:frame];
            [s_view setWantsBestResolutionOpenGLSurface:YES];
            [s_window setContentView:s_view];
            [s_window makeFirstResponder:s_view];
            [s_context setView:s_view];
            [s_context makeCurrentContext];
            [s_context setValues:&interval forParameter:NSOpenGLContextParameterSwapInterval];

            [s_window makeKeyAndOrderFront:nil];
            [NSApp activateIgnoringOtherApps:YES];
            [NSApp finishLaunching];
        }
        if ((([s_window styleMask] & NSWindowStyleMaskFullScreen) != 0) != (fullscreen != 0)) {
            [s_window toggleFullScreen:nil];
        }
        return 1;
    }
}

/* The original destroys the window to switch modes; here the window and
 * its context live on and plat_window_open switches the mode. */
void plat_destroy_window(void)
{
}

void plat_drawable_size(int *width, int *height)
{
    NSRect px = [s_view convertRectToBacking:[s_view bounds]];

    *width = (int)px.size.width;
    *height = (int)px.size.height;
}

void plat_present(void)
{
    [s_context update];
    [s_context flushBuffer];
}

void plat_request_quit(void)
{
    s_quit = 1;
}

void plat_pump_events(void)
{
    @autoreleasepool {
        NSEvent *ev;

        while ((ev = [NSApp nextEventMatchingMask:NSEventMaskAny
                                         untilDate:[NSDate distantPast]
                                            inMode:NSDefaultRunLoopMode
                                           dequeue:YES]) != nil) {
            handle_event(ev);
        }
    }
}

static int run(int (*frame)(void *), void *user, int only_active)
{
    while (!s_quit) {
        @autoreleasepool {
            NSEvent *ev;

            g_app_active = [NSApp isActive] || !only_active ? 1 : 0;
            ev = [NSApp nextEventMatchingMask:NSEventMaskAny
                                    untilDate:g_app_active ? [NSDate distantPast]
                                                           : [NSDate dateWithTimeIntervalSinceNow:0.1]
                                       inMode:NSDefaultRunLoopMode
                                      dequeue:YES];
            if (ev != nil) {
                handle_event(ev);
                continue;
            }
            if (s_close_requested) {
                s_close_requested = 0;
                app_on_destroy();
                continue;
            }
            if (g_app_active && !frame(user)) {
                plat_request_quit();
            }
        }
    }
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
