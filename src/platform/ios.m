/*
 * platform/ios.m - iOS and iPadOS: UIKit, an OpenGL ES 3.0 context on a
 * CAEAGLLayer, touch, hardware keyboards (GCKeyboard), game controllers
 * (GameController) and a RemoteIO audio unit.
 *
 * Objective-C with ARC. UIKit keeps the main thread; the game runs
 * main.c's main (built as ign_main) on a thread of its own, which owns
 * the GL context, so the game's loops run unchanged. Touches and keys
 * arrive on other threads and are queued for the game thread, which
 * takes them in plat_pump_events. While the app is in the background
 * the game thread only waits.
 *
 * The window is a framebuffer object with the layer's renderbuffer;
 * rnd_gl.c binds it as the screen (rnd_gl_screen_fbo). The game data
 * goes in the app's Documents folder, which the Files app shows.
 */

#import <AVFoundation/AVFoundation.h>
#import <AudioUnit/AudioUnit.h>
#import <GameController/GameController.h>
#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIKit.h>
#ifndef GLES_SILENCE_DEPRECATION
#define GLES_SILENCE_DEPRECATION
#endif
#import <OpenGLES/EAGL.h>
#import <OpenGLES/ES3/gl.h>

#include <mach/mach_time.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "app.h"
#include "dik.h"
#include "platform/platform.h"
#include "platform/common.h"
#include "platform/touch.h"
#include "rnd_gl.h"

int ign_main(int argc, char **argv);

static UIView *s_view;
static CAEAGLLayer *s_layer;
static EAGLContext *s_context;
static GLuint s_fbo, s_color, s_depth;
static GLint s_width, s_height;
static volatile int s_background;
static volatile int s_layout_changed;
static int s_quit;
static dispatch_semaphore_t s_view_ready;

/* ------------------------------------------------------------ events */

/* Touches and keys, from UIKit's and GameController's threads to the
 * game thread. */
enum { EVENTS = 256 };

static struct { int kind; long id; float x, y; int down; } s_events[EVENTS];
static unsigned s_ev_head, s_ev_tail;
static pthread_mutex_t s_ev_lock = PTHREAD_MUTEX_INITIALIZER;

enum { EV_TOUCH, EV_KEY };

static void post(int kind, long id, float x, float y, int down)
{
    pthread_mutex_lock(&s_ev_lock);
    if (s_ev_head - s_ev_tail < EVENTS) {
        unsigned i = s_ev_head++ % EVENTS;

        s_events[i].kind = kind;
        s_events[i].id = id;
        s_events[i].x = x;
        s_events[i].y = y;
        s_events[i].down = down;
    }
    pthread_mutex_unlock(&s_ev_lock);
}

static void take_events(void)
{
    pthread_mutex_lock(&s_ev_lock);
    while (s_ev_tail != s_ev_head) {
        unsigned i = s_ev_tail++ % EVENTS;

        if (s_events[i].kind == EV_TOUCH) {
            touch_finger(s_events[i].id, s_events[i].x, s_events[i].y, s_events[i].down);
        } else {
            int dik = (int)s_events[i].id;

            if (s_events[i].down && !g_plat_keys[dik]) {
                plat_key_event(dik, 1);
            } else if (!s_events[i].down && g_plat_keys[dik]) {
                plat_key_event(dik, 0);
            }
        }
    }
    pthread_mutex_unlock(&s_ev_lock);
}

/* HID keyboard usages to DirectInput scan codes. */
static int map_usage(GCKeyCode usage)
{
    static const u8 letters[26] = {
        DIK_A, DIK_B, DIK_C, DIK_D, DIK_E, DIK_F, DIK_G, DIK_H, DIK_I, DIK_J, DIK_K, DIK_L, DIK_M,
        DIK_N, DIK_O, DIK_P, DIK_Q, DIK_R, DIK_S, DIK_T, DIK_U, DIK_V, DIK_W, DIK_X, DIK_Y, DIK_Z
    };
    static const u8 digits[10] = {
        DIK_1, DIK_2, DIK_3, DIK_4, DIK_5, DIK_6, DIK_7, DIK_8, DIK_9, DIK_0
    };
    long u = (long)usage;

    if (u >= 0x04 && u <= 0x1d) {
        return letters[u - 0x04];
    }
    if (u >= 0x1e && u <= 0x27) {
        return digits[u - 0x1e];
    }
    if (u >= 0x3a && u <= 0x43) {
        return DIK_F1 + (int)(u - 0x3a);
    }
    switch (u) {
    case 0x28: return DIK_RETURN;
    case 0x29: return DIK_ESCAPE;
    case 0x2a: return DIK_BACK;
    case 0x2b: return DIK_TAB;
    case 0x2c: return DIK_SPACE;
    case 0x2d: return DIK_MINUS;
    case 0x2e: return DIK_EQUALS;
    case 0x2f: return DIK_LBRACKET;
    case 0x30: return DIK_RBRACKET;
    case 0x31: return DIK_BACKSLASH;
    case 0x33: return DIK_SEMICOLON;
    case 0x34: return DIK_APOSTROPHE;
    case 0x35: return DIK_GRAVE;
    case 0x36: return DIK_COMMA;
    case 0x37: return DIK_PERIOD;
    case 0x38: return DIK_SLASH;
    case 0x44: return DIK_F11;
    case 0x45: return DIK_F12;
    case 0x4a: return DIK_HOME;
    case 0x4b: return DIK_PRIOR;
    case 0x4c: return DIK_DELETE;
    case 0x4d: return DIK_END;
    case 0x4e: return DIK_NEXT;
    case 0x4f: return DIK_RIGHT;
    case 0x50: return DIK_LEFT;
    case 0x51: return DIK_DOWN;
    case 0x52: return DIK_UP;
    case 0x58: return DIK_NUMPADENTER;
    case 0xe0: return DIK_LCONTROL;
    case 0xe1: return DIK_LSHIFT;
    case 0xe2: return DIK_LMENU;
    case 0xe4: return DIK_RCONTROL;
    case 0xe5: return DIK_RSHIFT;
    case 0xe6: return DIK_RMENU;
    default: return 0;
    }
}

/* ------------------------------------------------------------ the view */

@interface IgnView : UIView
@end

@implementation IgnView
+ (Class)layerClass
{
    return [CAEAGLLayer class];
}

- (void)touches:(NSSet<UITouch *> *)touches down:(int)down
{
    CGFloat scale = self.contentScaleFactor;

    for (UITouch *t in touches) {
        CGPoint p = [t locationInView:self];

        post(EV_TOUCH, (long)(uintptr_t)(__bridge void *)t, (float)(p.x * scale),
             (float)(p.y * scale), down);
    }
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    [self touches:touches down:1];
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    [self touches:touches down:1];
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    [self touches:touches down:0];
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    [self touches:touches down:0];
}

- (void)layoutSubviews
{
    [super layoutSubviews];
    s_layout_changed = 1;
}
@end

@interface IgnViewController : UIViewController
@end

@implementation IgnViewController
- (void)loadView
{
    IgnView *view = [[IgnView alloc] initWithFrame:[UIScreen mainScreen].bounds];

    view.multipleTouchEnabled = YES;
    view.contentScaleFactor = [UIScreen mainScreen].nativeScale;
    self.view = view;
}

- (BOOL)prefersStatusBarHidden
{
    return YES;
}

- (BOOL)prefersHomeIndicatorAutoHidden
{
    return YES;
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations
{
    return UIInterfaceOrientationMaskLandscape;
}
@end

/* ------------------------------------------------------------ the app */

@interface IgnAppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow *window;
@end

static void *game_thread(void *arg)
{
    static char *argv[] = { "ignition", NULL };

    (void)arg;
    ign_main(1, argv);
    exit(0);
    return NULL;
}

@implementation IgnAppDelegate
- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)options
{
    IgnViewController *vc = [[IgnViewController alloc] init];
    pthread_attr_t attr;
    pthread_t thread;

    [[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryAmbient error:nil];
    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    self.window.rootViewController = vc;
    [self.window makeKeyAndVisible];
    s_view = vc.view;
    s_layer = (CAEAGLLayer *)s_view.layer;
    s_layer.opaque = YES;
    if (@available(iOS 14.0, *)) {
        [[NSNotificationCenter defaultCenter]
            addObserverForName:GCKeyboardDidConnectNotification object:nil queue:nil
                    usingBlock:^(NSNotification *note) {
            GCKeyboard *kb = note.object;

            kb.keyboardInput.keyChangedHandler = ^(GCKeyboardInput *input,
                                                   GCControllerButtonInput *key,
                                                   GCKeyCode code, BOOL pressed) {
                int dik = map_usage(code);

                if (dik != 0) {
                    post(EV_KEY, dik, 0, 0, pressed ? 1 : 0);
                }
            };
        }];
    }
    dispatch_semaphore_signal(s_view_ready);
    /* The game's loops go deep: a big stack. */
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 16 * 1024 * 1024);
    pthread_create(&thread, &attr, game_thread, NULL);
    return YES;
}

- (void)applicationWillResignActive:(UIApplication *)application
{
    /* No GL from the background: the game thread stops at its next
     * frame. */
    s_background = 1;
}

- (void)applicationDidBecomeActive:(UIApplication *)application
{
    s_background = 0;
}
@end

int main(int argc, char **argv)
{
    @autoreleasepool {
        char docs[1024];
        const char *home = getenv("HOME");

        s_view_ready = dispatch_semaphore_create(0);
        /* The game data and the log (Default.fil) live in Documents. */
        if (home != NULL) {
            snprintf(docs, sizeof(docs), "%s/Documents", home);
            if (chdir(docs) != 0) {
                fprintf(stderr, "ignition: cannot enter %s\n", docs);
            }
        }
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([IgnAppDelegate class]));
    }
}

/* ------------------------------------------------------------ clock */

u32 plat_time_ms(void)
{
    static mach_timebase_info_data_t tb;
    static u64 start;
    u64 t;

    if (tb.denom == 0) {
        mach_timebase_info(&tb);
        start = mach_absolute_time();
    }
    t = (mach_absolute_time() - start) * tb.numer / tb.denom;
    return (u32)(t / 1000000u);
}

/* ------------------------------------------------------------ display */

/* The renderbuffers, sized to the layer. */
static void make_buffers(void)
{
    if (s_color != 0) {
        glDeleteRenderbuffers(1, &s_color);
        glDeleteRenderbuffers(1, &s_depth);
    }
    glGenRenderbuffers(1, &s_color);
    glBindRenderbuffer(GL_RENDERBUFFER, s_color);
    [s_context renderbufferStorage:GL_RENDERBUFFER fromDrawable:s_layer];
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &s_width);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &s_height);
    glGenRenderbuffers(1, &s_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, s_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, s_width, s_height);
    glBindFramebuffer(GL_FRAMEBUFFER, s_fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, s_color);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, s_depth);
    s_layout_changed = 0;
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
    (void)title;
    (void)width;
    (void)height;
    (void)fullscreen;
    if (s_context != nil) {
        return 1;
    }
    dispatch_semaphore_wait(s_view_ready, DISPATCH_TIME_FOREVER);
    s_context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
    if (s_context == nil || ![EAGLContext setCurrentContext:s_context]) {
        fprintf(stderr, "ignition: no OpenGL ES 3 context\n");
        return 0;
    }
    glGenFramebuffers(1, &s_fbo);
    make_buffers();
    rnd_gl_screen_fbo = s_fbo;
    return 1;
}

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
    touch_draw(s_width, s_height);
    glBindRenderbuffer(GL_RENDERBUFFER, s_color);
    [s_context presentRenderbuffer:GL_RENDERBUFFER];
    if (s_layout_changed) {
        make_buffers();
    }
}

void plat_request_quit(void)
{
    s_quit = 1;
}

/* ------------------------------------------------------------ gamepads */

static u32 controller_buttons(void)
{
    u32 b = 0;

    for (GCController *c in [GCController controllers]) {
        GCExtendedGamepad *g = c.extendedGamepad;

        if (g == nil) {
            continue;
        }
        b |= g.dpad.left.pressed || g.leftThumbstick.xAxis.value < -0.5f ? PAD_LEFT : 0;
        b |= g.dpad.right.pressed || g.leftThumbstick.xAxis.value > 0.5f ? PAD_RIGHT : 0;
        b |= g.dpad.up.pressed || g.leftThumbstick.yAxis.value > 0.5f ? PAD_UP : 0;
        b |= g.dpad.down.pressed || g.leftThumbstick.yAxis.value < -0.5f ? PAD_DOWN : 0;
        b |= g.buttonA.pressed ? PAD_A : 0;
        b |= g.buttonB.pressed ? PAD_B : 0;
        b |= g.buttonX.pressed ? PAD_X : 0;
        b |= g.buttonY.pressed ? PAD_Y : 0;
        b |= g.leftShoulder.pressed || g.leftTrigger.pressed ? PAD_L : 0;
        b |= g.rightShoulder.pressed || g.rightTrigger.pressed ? PAD_R : 0;
        b |= g.buttonMenu.pressed ? PAD_START : 0;
        if (g.buttonOptions != nil) {
            b |= g.buttonOptions.pressed ? PAD_SELECT : 0;
        }
    }
    return b;
}

void plat_pump_events(void)
{
    plat_run_scheduled();
    take_events();
    @autoreleasepool {
        plat_pad_update(controller_buttons() | touch_buttons(s_width, s_height));
    }
}

/* ------------------------------------------------------------ audio */

static AudioUnit s_audio_unit;
static plat_audio_fn s_audio_cb;
static void *s_audio_user;

static OSStatus audio_render(void *ref, AudioUnitRenderActionFlags *flags,
                             const AudioTimeStamp *ts, UInt32 bus, UInt32 frames,
                             AudioBufferList *io)
{
    (void)ref;
    (void)flags;
    (void)ts;
    (void)bus;
    s_audio_cb((float *)io->mBuffers[0].mData, (s32)frames, s_audio_user);
    return noErr;
}

/* RemoteIO, 32 bit float stereo interleaved. */
int plat_audio_open(s32 rate, plat_audio_fn cb, void *user)
{
    AudioComponentDescription desc = { kAudioUnitType_Output, kAudioUnitSubType_RemoteIO,
                                       kAudioUnitManufacturer_Apple, 0, 0 };
    AudioComponent comp = AudioComponentFindNext(NULL, &desc);
    AudioStreamBasicDescription fmt;
    AURenderCallbackStruct render;

    if (comp == NULL || AudioComponentInstanceNew(comp, &s_audio_unit) != noErr) {
        return 0;
    }
    [[AVAudioSession sharedInstance] setActive:YES error:nil];
    memset(&fmt, 0, sizeof(fmt));
    fmt.mSampleRate = rate;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel = 32;
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerFrame = 8;
    fmt.mBytesPerPacket = 8;
    render.inputProc = audio_render;
    render.inputProcRefCon = NULL;
    s_audio_cb = cb;
    s_audio_user = user;
    if (AudioUnitSetProperty(s_audio_unit, kAudioUnitProperty_StreamFormat,
                             kAudioUnitScope_Input, 0, &fmt, sizeof(fmt)) != noErr
        || AudioUnitSetProperty(s_audio_unit, kAudioUnitProperty_SetRenderCallback,
                                kAudioUnitScope_Input, 0, &render, sizeof(render)) != noErr
        || AudioUnitInitialize(s_audio_unit) != noErr
        || AudioOutputUnitStart(s_audio_unit) != noErr) {
        AudioComponentInstanceDispose(s_audio_unit);
        s_audio_unit = NULL;
        return 0;
    }
    return 1;
}

void plat_audio_close(void)
{
    if (s_audio_unit != NULL) {
        AudioOutputUnitStop(s_audio_unit);
        AudioUnitUninitialize(s_audio_unit);
        AudioComponentInstanceDispose(s_audio_unit);
        s_audio_unit = NULL;
    }
}

/* ------------------------------------------------------------ loop */

static int run(int (*frame)(void *), void *user)
{
    while (!s_quit) {
        if (s_background) {
            g_app_active = 0;
            usleep(100000);
            continue;
        }
        g_app_active = 1;
        plat_pump_events();
        if (!frame(user)) {
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

/* Documents, which the Files app shows. */
const char *plat_host_pref_dir(void)
{
    static char dir[1024];
    const char *home = getenv("HOME");

    if (home == NULL) {
        return NULL;
    }
    snprintf(dir, sizeof(dir), "%s/Documents", home);
    return dir;
}
