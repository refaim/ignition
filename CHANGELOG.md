# Changelog

All notable changes to this project are documented in this file.

## [Unreleased]

### Fixed
- A championship no longer quits after its second race: the end of a race frees what the race loaded.
- NATIVE's track results, track score and totals are drawn at the menus' size.
- NATIVE's menus sit closer under the logo, with their rows spread out, and the last options (RESET GHOST CAR and its YES / NO) draw in full.

### Added
- The Vita's LiveArea bubble, backdrop and gate are made from the game's installer picture and logo when the build finds an extracted disc.

## [0.1.0] - 2026-10-23

The first version: the front end and single-player races are playable from
start to finish, from the files of your own copy of the game.

### Added
- Game data from the disc image: the game reads the `.cue` and `.bin` files of your own disc. `make extract` (or `cd_extract`) copies the files and the audio tracks out of an image, and the packaged builds do it by themselves at the first start when they find an image next to the program.
- Front end: the intro film, the language page, the main menu and every page under it, the options, the best times, the cheat codes, and the 3D cars on the car select page over the animated track.
- Races: the tracks, the cars on the grid, the car physics and ground contact, the computer drivers, the player controls, the race view and the HUD, sound effects, the race effects and animated textures, the results, and two players on a split screen.
- CD music, played from the disc image, and the game's sound banks and voices.
- Graphics on the GPU at the display's resolution. The **NATIVE** choice in the resolution option uses the window's own size, with the menus, the loading screen and the race HUD laid out for it. `--software` (or Alt+R) draws the 2D on the CPU, as the original does.
- Platforms: macOS, Linux (also as an AppImage for the Steam Deck), Windows x86_64, the web (WebGL 2), Android, iOS and iPadOS, and the PlayStation Vita. Each host uses only its system's own APIs. On every platform but macOS, a gamepad (or the Vita's buttons) presses the game's keys, and touch screens get an on-screen pad.
- `--autopilot`, where the computer drives the player's car, and `--press`, `--shot` and `--background` for scripted test runs.
- `--version`, and the port's version in the log after the original game's version line.
- Release packages for every platform from GitHub Actions, published with the changelog when a version is tagged.
