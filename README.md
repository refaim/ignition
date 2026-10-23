# Ignition

A reimplementation of **Ignition**, the 1997 top-down racing game by Unique
Development Studios, published by Virgin Interactive. The game's Windows 95
executable, `IGN_WIN.EXE`, is reverse engineered and rewritten in portable
C99. The port runs the original game data, which you supply from your own
copy of the game.

![The main menu](docs/images/menu.png)
![Choosing a car](docs/images/cars.png)
![A race on Moosejaw Falls](docs/images/race.png)
![The track results](docs/images/results.png)

## Status

The front end and single-player races are playable from start to finish:
three laps, computer drivers, effects, sound, the HUD and the results.

- About 70% of the game's code is ported. [docs/PROGRESS.md](docs/PROGRESS.md)
  lists every routine of `IGN_WIN.EXE` and its status.
- Split-screen two-player races work. Network play is not ported.
- Joystick input is not ported. The keyboard works.

### Platforms

| Platform | Host | Renderer | Status |
|---|---|---|---|
| macOS | Cocoa (`src/platform_macos.m`) | OpenGL 3.2 (`src/rnd_gl.c`) | Playable |
| PlayStation Vita | VitaSDK (`src/platform_vita.c`) | GXM (`src/rnd_gxm.c`) | Builds; not yet run on hardware. See [docs/vita.md](docs/vita.md). |

The hosts use only each system's own APIs. The project has no third-party
dependencies.

### Rendering

The original draws in 8-bit colour at 320 x 200, 640 x 480 or 800 x 600.
This port keeps the 8-bit picture and its palette, and draws it on the GPU.
The RESOLUTION menu offers those three modes and one more, **NATIVE**:

- The 3D renders at the window's full resolution and fills the window's
  shape, so a wide window sees more of the track at the sides.
- The menus keep their 4:3 layout at a whole-number scale, and the track
  behind them fills the window.
- The race HUD sits at the window's edges.

`--software` (or Alt+R in the game) switches the 2D drawing back to the CPU,
as the original does it. The 3D is always drawn on the GPU.

## Game data

The game data is not in this repository. You need a disc image (`.cue` and
`.bin` files) of your own copy of Ignition.

1. Put the disc image in its own folder under `data/`, for example
   `data/Ignition (USA) (En,Es,It,Sv) (Rerelease)/`.
2. Run `make extract`. The tool copies the files from each disc image in
   `data/` into a `disc/` folder next to the image.

The game reads its files from `disc/`, and plays the CD music from the disc
image's audio tracks. To use another folder, pass `--data DIR` and
`--cue FILE`.

## Build

You need Xcode's command line tools on macOS. To build and run the game:

```bash
make        # builds ./ignition and the extract tool
make run    # builds, then starts the game
make test   # runs the tests that need no game data
make clean
```

For the PlayStation Vita, see [docs/vita.md](docs/vita.md):

```bash
make vita          # builds build-vita/ignition.vpk
make vita-deploy   # copies it to a Vita
```

### Command line

| Option | Effect |
|---|---|
| `--data DIR` | Reads the game files from `DIR`. |
| `--cue FILE` | Plays the CD music from the disc image `FILE`. |
| `--res N` | Forces the resolution: `0` 320 x 200, `1` 640 x 480, `2` 800 x 600, `3` NATIVE. |
| `--software` | Draws the 2D on the CPU. |
| `--view FILE.PIC` | Shows one of the game's pictures. |
| `--text TEXT` | Shows `TEXT` in the game's menu font. |

These options are for testing:

| Option | Effect |
|---|---|
| `--press MS:KEY[:HOLD]` | Presses `KEY` (a DirectInput scan code in hex) at `MS` milliseconds, held for `HOLD` milliseconds (60 by default). |
| `--shot MS:FILE.tga` | Saves a screenshot at `MS` milliseconds. |
| `--autopilot` | The computer drives the player's car. |
| `--background` | Keeps running when the window is not in front. |

For example, this starts a race in which the computer drives your car, and
saves a screenshot 30 seconds in:

```bash
./ignition --autopilot --press 1500:1c --press 3000:1c --press 5000:1c \
    --press 6500:1c --press 8000:1c --press 9500:1c --press 11000:1c \
    --shot 30000:race.tga
```

## How the port is made

Each routine of `IGN_WIN.EXE` is read in IDA and rewritten in C99 that
behaves the same way: the same fixed-point maths, struct layouts and
rounding, and the original's quirks unless a fix is noted. Every ported
function names its address in a comment, for example `/* 0x422680 */`.

- `src/` holds the game, named by the original's modules where they are
  known (`race_*.c` for the race, `menu_*.c` and `frontend.c` for the
  menus).
- `src/structs/` holds the structs, generated from `tools/structs/*.def`
  by `tools/gen_structs.py`, with an assert for every field's offset.
- `src/race_data.c` and `src/menu_data.c` hold the original's tables,
  generated from the executable by `tools/gen_race_data.py` and
  `tools/gen_menu_data.py`.
- `tools/progress.py` writes [docs/PROGRESS.md](docs/PROGRESS.md).

The disassembly and the IDA database are not in this repository.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) and the
[code of conduct](CODE_OF_CONDUCT.md).

## License

The code in this repository is licensed under the
[GNU General Public License v3.0](LICENSE).

Ignition, its executables and its data are © their respective owners, and
are not included in this repository. This is a preservation project; you
need your own copy of the game to play.
