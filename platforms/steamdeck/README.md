# Ignition on the Steam Deck

The Steam Deck runs the Linux AppImage. There is no separate build; see
[../linux/README.md](../linux/README.md) for how it is built.

## Install

1. Switch to Desktop Mode.
2. Make a folder for the game, for example `~/Games/Ignition`.
3. Copy `ignition-linux-x86_64-<version>.AppImage` from a release into it,
   and make it executable (right-click it, **Properties** → **Permissions**
   → **Is executable**).
4. Copy the disc image of your own copy of the game (the `.cue` file and
   its `.bin` files) into the same folder. At the first start, the game
   copies the files out of the image into a `disc/` folder there.
5. In Steam, select **Games** → **Add a Non-Steam Game to My Library…** →
   **Browse…**, and pick the AppImage.

In Gaming Mode, the game is in the library under **Non-Steam**.

## Controls

The game reads the Deck's controls as a gamepad through the kernel's
joystick API. In the game's **Properties** → **Controller**, use a
gamepad layout (the default for non-Steam games works). The button
layout is in [../linux/README.md](../linux/README.md#controls).

## Artwork

This repository has no Steam library artwork. You can set your own with
**Manage** → **Set Custom Artwork** on the game's page.
