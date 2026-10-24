# Ignition on Linux

The Linux host is `src/platform/linux.c`: an X11 window with a GLX
OpenGL 3.2 core context, the keyboard, gamepads through the kernel's
joystick API (`/dev/input/js*`) and ALSA audio. It is also the Steam
Deck build; see [../steamdeck/README.md](../steamdeck/README.md).

## Build

You need a C compiler, GNU make, and the X11, OpenGL and ALSA
development files.

| Distribution | Packages |
| --- | --- |
| Debian, Ubuntu | `build-essential libx11-dev libgl-dev libasound2-dev` |
| Fedora | `gcc make libX11-devel mesa-libGL-devel alsa-lib-devel` |
| Arch, SteamOS | `base-devel libx11 libglvnd alsa-lib` |

```sh
make linux                          # build-linux/ignition and build-linux/cd_extract
sh platforms/linux/appimage.sh      # build-linux/ignition-linux-x86_64-v<version>.AppImage
```

On Linux, `make` with no target builds the same program as `./ignition`.

`appimage.sh` downloads `appimagetool` to pack the AppImage. The game
needs only what every Linux desktop has (libc, X11, libGL, ALSA, which
PulseAudio and PipeWire also serve), so nothing else goes inside. It
takes the version and the architecture as arguments: `appimage.sh v0.1.0
aarch64` on an arm64 machine.

## Game data

The game needs the files from your own copy of the game. Give them to it
in one of these ways:

- Put the disc image (the `.cue` file and its `.bin` files) in the folder
  that holds the AppImage or the `ignition` program. At the first start,
  the game copies the files out of the image into a `disc/` folder next
  to it, and then plays the CD music from the image.
- Put the disc image in `~/.local/share/ignition/` instead. The game uses
  that folder when it finds nothing next to the program.
- Start the game with `--data DIR`, where `DIR` holds the files of the
  CD (the folder with `BALTAZAR` and `LEVELS` in it), or set
  `IGNITION_DATA` to that folder.

The game saves its settings and best times in the data folder, so that
folder must be writable.

## Controls

The keyboard works as in the original game. Alt+Enter switches between a
window and full screen. Alt+R switches the 2D between the GPU and the
CPU.

A gamepad presses the game's keys:

| Button | In the menus | In a race |
| --- | --- | --- |
| D-pad, left stick | Move | Steer (up and down as in the menus) |
| A | Select | Accelerate |
| X | | Brake |
| B | Back | Action |
| Y | | Look back |
| RB, RT / LB, LT | | Gear up / gear down |
| Start | Select | Escape (pause) |
| Back | Back | Enter |

In a race, the buttons press the keys set for player 1 in the game's
control options. The same layout applies on every platform.
