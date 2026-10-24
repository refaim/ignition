# Platforms

Every build runs the same engine (`src/`). Each platform adds one host
file behind `src/platform/platform.h`, written on that system's own APIs, and one
renderer behind `src/rnd.h`. There are no third-party libraries.

| Platform | Host | System APIs | Renderer | Build | Guide |
| --- | --- | --- | --- | --- | --- |
| macOS | `platform/macos.m` | Cocoa, CoreAudio | `rnd_gl.c`, OpenGL 3.2 core | `make` | [README](../README.md#build) |
| Linux, Steam Deck | `platform/linux.c` | X11, GLX, ALSA, joystick API | `rnd_gl.c`, OpenGL 3.2 core | `make linux` | [linux](../platforms/linux/README.md), [steamdeck](../platforms/steamdeck/README.md) |
| Windows x86_64 | `platform/windows.c` | Win32, WGL, waveOut, XInput | `rnd_gl.c`, OpenGL 3.2 core | `make windows` | [windows](../platforms/windows/README.md) |
| Web | `platform/web.c` | Emscripten HTML5, WebGL 2, Web Audio | `rnd_gl.c`, OpenGL ES 3.0 | `make web` | [web](../platforms/web/README.md) |
| Android | `platform/android.c` | NativeActivity, EGL, AAudio | `rnd_gl.c`, OpenGL ES 3.0 | `make android` | [android](../platforms/android/README.md) |
| iOS, iPadOS | `platform/ios.m` | UIKit, OpenGL ES, GameController, AudioUnit | `rnd_gl.c`, OpenGL ES 3.0 | `make ios` | [ios](../platforms/ios/README.md) |
| PlayStation Vita | `platform/vita.c` | VitaSDK | `rnd_gxm.c`, GXM | `make vita` | [vita.md](vita.md) |

## Shared code

- `src/platform/common.c`: the DirectInput-style key queue, the
  scripted key presses (`--press`), the gamepad-to-key mapping, and
  finding the game data. Every host but the Vita's uses it.
- `src/platform/touch.c`: the on-screen pad for touch screens (web,
  Android, iOS), drawn through `rnd.h`.
- `src/rnd_gl.c` runs on three kinds of OpenGL (see `src/rnd_gl.h`):
  the system's OpenGL 3.2 headers on macOS; OpenGL 3.2 through function
  pointers that `rnd_gl_load` fills in with `glXGetProcAddressARB` or
  `wglGetProcAddress` on Linux and Windows; and OpenGL ES 3.0
  (`IGN_GLES`) on the web, Android and iOS, where each shader's
  `#version 150` becomes `#version 300 es`.
- `src/third_party/` holds Khronos' `GL/glcorearb.h` and
  `KHR/khrplatform.h` (MIT licence), the OpenGL declarations for Linux
  and Windows. They are headers only; nothing is linked from them.

## Finding the game data

Without `--data`, every host except the Vita's looks in this order:

1. the folder in `$IGNITION_DATA`;
2. the repository's layout, `data/Ignition (USA) (En,Es,It,Sv) (Rerelease)/disc`
   (what `make extract` makes);
3. a `disc/` folder next to the program (next to the AppImage file for
   an AppImage), or a disc image (`.cue` and `.bin`) there, which is
   extracted into `disc/` at the first start;
4. the same in the per-user folder: `~/Library/Application Support/Ignition`
   (macOS), `~/.local/share/ignition` (Linux), `%APPDATA%\ignition`
   (Windows), `/ignition` in IndexedDB (web), the app's folder on shared
   storage (Android), the app's Documents (iOS).

The CD music plays from the `.cue` next to the `disc/` folder.

## Platforms that are not ported

This port draws the race's 3D, and the front end's 3D cars and track,
with GPU shaders. The CPU path (`--software`, Alt+R) covers only the 2D.
The original's own CPU 3D drawing (the span routines of `IGN_WIN.EXE`,
still stubs here, and the DOS executable on the CD) is not ported. These
targets have no GPU of the OpenGL 3 or OpenGL ES 3 class, so each needs
that CPU 3D first:

- **DOS** and **Windows 9x**.
- **PlayStation Portable**: its GPU has no shaders, texture arrays or
  the render-to-texture paths the renderer uses.
- **Analogue Pocket / MiSTer** (openfpgaOS): no GPU at all.

Porting the original's CPU 3D would open all four.
