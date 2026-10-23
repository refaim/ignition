# Contributing to Ignition

This project reverse engineers Ignition's `IGN_WIN.EXE` and rewrites it in
portable C99. There are two kinds of work, and both are welcome:

- **Porting:** rewriting routines of the original in C.
- **Engine and platform work:** the renderer, the platform hosts, the
  build, and bug fixes.

## Before you start

- **The game data is not included.** To build and run anything, you need
  your own copy of the game. [README.md](README.md) explains where to put
  the disc image and how to extract it.
- **Check [docs/PROGRESS.md](docs/PROGRESS.md)** to see which routines are
  ported and which are still stubs.
- **No third-party dependencies.** Each platform host uses only its system's
  own APIs. Do not add libraries.

## Build

```bash
make        # builds ./ignition
make run    # builds, then starts the game
make test   # runs the tests that need no game data
```

[README.md](README.md) lists the command line options. `--press`, `--shot`
and `--autopilot` let you run the game without touching it, which is useful
to test a change.

## Code style

- C99, strict, with no warnings.
- `snake_case` and 4-space indentation.
- Comments explain why, or what the original does. Each ported function
  names its address: `/* 0x422680 */`.
- Structs that mirror the original's memory are generated from
  `tools/structs/*.def` with `tools/gen_structs.py`. Add a field to the
  `.def` file, then run the script; do not edit `src/structs/*.h` by hand.
- Globals whose use is not known yet are named by their address, for
  example `r_5285c0`. Rename one when you learn what it holds.

## Port a routine

1. Read the routine in the disassembly. Hex-Rays output is a lead, not the
   truth: check the assembly for integer widths, signed and unsigned
   shifts, rounding and the x87 maths.
2. Match the original's behaviour, including its quirks. If you fix a bug
   of the original, say so in a comment and in the commit message.
3. Check every struct field you use against the original's offsets.
4. Replace the stub, and run `tools/progress.py` to update
   [docs/PROGRESS.md](docs/PROGRESS.md).

## Test your change

There is no test suite for the game itself. Before you open a pull request:

- Build with `make`, and check that it gives no warnings.
- Run the game far enough to use your change, with real game data.
- For a change to rendering, compare screenshots from `--shot` before and
  after the change.
- In the pull request, say what you tested and on which platform.

## Pull requests

1. Fork the repository and branch from `main`.
2. Keep each pull request to one fix or feature.
3. Write commit subjects in the imperative ("Fix the lap count after a
   respawn"), and explain why in the body.
4. Open the pull request against `main`.

By contributing, you agree that your contribution is licensed under the
project's [GNU General Public License v3.0](LICENSE).
