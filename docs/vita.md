# PlayStation Vita port

The Vita build runs the same engine and the same OpenGL renderer as macOS.
The host is `src/platform_vita.c` (display, buttons, clock, audio, log).
OpenGL comes from [vitaGL](https://github.com/Rinnegatamante/vitaGL), and
`src/gl_compat.h` and `src/gl_compat.c` cover the places where vitaGL
differs from OpenGL 3.2 core.

Status: the package builds. It has not run on a Vita or in Vita3K yet,
and the Cg shaders have not been through the device's compiler.

## Build

You need VitaSDK, at `$VITASDK` or `~/vitasdk`, with these packages:

```
vdpm install vitaShaRK libmathneon taihen SceShaccCgExt
make vita                      # -> build-vita/ignition.vpk (title IGNT00001)
```

`make vita` first runs `tools/vita_deps.sh`. The script builds vitaGL into
`build-vita/deps` at the commit of the VitaSDK package, but with
`STORE_DEPTH_STENCIL=1` (see [Renderer](#renderer)). `make vita-clean`
removes `build-vita/`.

The LiveArea images are drawn by `tools/vita_sce_sys.py`, so the package
holds no art from the game.

## Install

1. Install `ignition.vpk` with VitaShell.
2. Copy the extracted disc (`make extract` makes
   `data/<image>/disc/`) to `ux0:data/ignition/disc/`.
3. For the CD music, copy the image's `.cue` and every `.bin` it names to
   `ux0:data/ignition/`, next to `disc/`.
4. vitaGL compiles the shaders on the device, so the Vita needs
   `ur0:data/libshacccg.suprx`. Most homebrew setups already have it.

Settings are saved in the `disc/` folder, as on the PC. The log goes to
`ux0:data/ignition/log.txt` and `err.txt`.

## Controls

The game reads a keyboard, so each button presses a key. In a race, the
keys come from player 1's settings (`g_race_cfg + 0x17f`). If you remap
the keyboard in the menu, the buttons follow.

| Vita | Menus | Race | Race paused | Y/N question |
|---|---|---|---|---|
| D-pad, left stick | arrows | left/right steer, up/down arrows | arrows | |
| Cross | Return | accelerate | Return | Yes |
| Square | | brake | | |
| Circle | Escape | action | Escape | No |
| Triangle | | look back | | |
| R / L | | gear up / gear down | | |
| Start | Return | Escape (pause) | Escape | Escape |
| Select | Escape | Return (go on) | Return | Return |

## Renderer

The screen is 960 x 544. The NATIVE resolution follows it, and the 2D menu
is drawn at 4:3 in the middle, as on macOS.

What changes on the Vita, all under `IGN_GL_VITA`:

- **Shaders.** Each renderer file has a Cg copy of its GLSL 150 shaders,
  passed to vitaGL as `GL_CG_*_SHADER_EXT`. Cg has no `texelFetch` or
  `textureSize`, so the shaders get texture sizes as uniforms and sample
  at texel centres. All the textures are nearest. `gl_FragCoord` is
  `WPOS`.
- **No texture arrays.** `gl3d.c` lays a slot's 256 x 256 pages out as
  tiles of one 2D texture, 16 to a row (4096 texels, the widest GXM
  allows). The shaders find a page's tile from `u_grid`.
- **No `gl_VertexID`.** `gfx_gl.c` gives the screen quad's corners in a
  buffer.
- **No `glBindFragDataLocation`.** Cg names the output by semantic.
- **No `GL_PACK_ROW_LENGTH`.** The CPU 2D raster path reads whole rows,
  then copies them to the pitch.
- **Copies.** The shaders that read the pixel under a face read a copy of
  the framebuffer. vitaGL's `glCopyTexSubImage2D` reads back to the CPU,
  so `ign_copy_rect` blits into a framebuffer made for the copy. Each blit
  starts a new GXM scene. vitaGL drops the depth buffer between scenes
  unless it is built with `STORE_DEPTH_STENCIL=1`, and the faces drawn
  after a copy test depth. This is why `tools/vita_deps.sh` builds vitaGL.
- **Index rounding.** A colour index is written as `(i + 0.25) / 255`, so
  GXM's pack to 8 bits gives `i` whether it rounds or truncates.

## Not done

- Not run on hardware or in Vita3K.
- The game's joystick (`g_joysticks`, analog steering) is not fed from
  the stick; the stick presses the steering keys.
- No touch input, and no way to type names in the menus.
- Each translucent face costs a blit and a scene change. Races with a
  lot of smoke may be slow.
- `gl3d_mesh_changed` updates part of one large vertex buffer. vitaGL
  copies the whole buffer when the GPU still uses it, which can be slow
  for animated textures.
