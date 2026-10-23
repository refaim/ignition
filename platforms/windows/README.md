# Ignition on Windows

The Windows host is `src/platform_windows.c`: a Win32 window with a WGL
OpenGL 3.2 core context, the keyboard, XInput gamepads and waveOut
audio. `ignition.exe` is one file; it needs only the DLLs that come with
Windows. It runs on 64-bit Windows 10 and later, and under Wine.

## Build

The build cross-compiles with MinGW-w64 on Linux or macOS:

| System | Packages |
| --- | --- |
| Debian, Ubuntu | `gcc-mingw-w64-x86-64 make` |
| macOS (Homebrew) | `mingw-w64` |

```sh
make windows          # build-windows/ignition.exe and build-windows/cd_extract.exe
make windows-clean
```

Set `MINGW` to the compiler prefix if yours differs (the default is
`x86_64-w64-mingw32-`). In an MSYS2 MINGW64 shell, `make windows MINGW=`
uses MSYS2's own `gcc`.

## Game data

Put the disc image of your own copy of the game (the `.cue` file and its
`.bin` files) in the folder that holds `ignition.exe`. At the first
start, the game copies the files out of the image into a `disc\` folder
next to it, and then plays the CD music from the image.

These also work:

- `%APPDATA%\ignition\` instead of the program's folder;
- `ignition.exe --data DIR`, where `DIR` holds the CD's files (the folder
  with `BALTAZAR` and `LEVELS` in it), or the same folder in the
  `IGNITION_DATA` environment variable.

## Controls

The keyboard works as in the original game. Alt+Enter switches between a
window and full screen; Alt+R switches the 2D between the GPU and the
CPU. XInput gamepads (Xbox and compatible) use the layout in
[../linux/README.md](../linux/README.md#controls).
