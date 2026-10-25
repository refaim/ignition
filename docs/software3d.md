# The software 3D

`IGN_WIN.EXE` draws its 3D on the CPU, in 8 bit colour indices, into the
same buffers as the 2D. The port draws the 3D on the GPU by default
(`src/gl3d.c`). With `--soft3d` it runs the original's own pipeline and
rasteriser instead, ported in `src/soft3d/`, so that the game can run
without a GPU at 320 x 200, 640 x 480 and 800 x 600. This document maps
the original's routines to the port's functions.

Function names are the port's; the address in brackets is the original
routine. Globals named `r_<address>` are not understood yet.

## A frame

The race's view (`race_view` [0x436990]) and the front end's car
(`car3d_draw` [0x40c5a0]) each draw a scene in two calls:

1. `soft3d_scene_render` [0x4466d0] runs the scene's stages and returns
   a list of screen space commands, far to near.
2. `sw_raster_draw` [0x44f0e9] draws the list into the target buffer,
   clipped to a rectangle.

The context of the second call (`g_sw_raster` [0x498730]) holds the list,
the target, the clip rectangle and a colour table. The race draws into
the back buffer, clipped to the player's view, with the level's `.TAB`.
The front end draws into the menu screen with `menu.tab`. The front
end's animated background builds its commands itself and draws them
with `sw_raster_draw` too.

`sw_raster_set_screen` [0x44f070] sets the screen the rasteriser draws
into: its width (the row pitch), the row offsets, a table of
reciprocals, and each filler's set-up hook.

## The pipeline

`soft3d_scene_render` [0x4466d0] runs each stage whose switch is on in
the scene (`struct scene`, `src/scene.h`).

| Stage | Routine | Port | What it does |
|---|---|---|---|
| look at | 0x448990 | `scene_look_at` (`src/scene.c`) | Aims the camera at its target. |
| gather | 0x448e70 | `gather_cull` | Takes the objects of the grid cells ahead of the camera, then keeps those whose bounding sphere can be on the screen. |
| | 0x44b340 | `cull_rows` | The camera's rows in 1.14 fixed point, for the cull. |
| transform | 0x449e70, 0x44a3d0 | `transform`, `transform_44a3d0` | Projects each object's vertices to screen x, y in 8.8 and a depth. 0x44a3d0 serves focal lengths over 300 (640 x 480 and up). |
| | 0x44a900, 0x44ae20 | `project_turned_44a900`, `project_turned_44ae20` | The same for an object turned by its angles. |
| emit | 0x44c1f0 | `emit` | Gives each face of each object to the handler of its type. |
| | 0x44b480 | `sw_faces_select` | Picks the textured face handlers for PERSP. POLY and MIP MAPPING. |
| collect | 0x4466d0 | `soft3d_scene_render` | Lists the commands of the 6000 depth buckets, far to near. |

All of it is in `src/soft3d/pipeline.c`. The transform works in 32 bit
fixed point with the rows rounded through floats where the original
stores them, so the vertices land on the same pixels.

## Face handlers

A face handler (`src/soft3d/faces.c`) reads a face, tests it against
the clip rectangle, its facing and its depth, and makes a command in a
depth bucket. The bucket is the sum of the vertices' depths over 16;
objects of class 100 and over are pulled 0x5c nearer, so that cars and
effects draw over the road they stand on.

| Face type | Routine | Port | Command made |
|---|---|---|---|
| 0, 3 to 6, 9, 10, 14 | 0x44b560 | `face_end` | none: the mesh ends |
| 1 | 0x44b5e0 | `face_point` | 1, a point |
| 2 | 0x44dbb0 | `face_point_table` | 2, a point through the `.SHD` |
| 7 | 0x44d0f0 | `face_sprite` | 7, a sprite through the `.SHD`, sized by its depth |
| 8 | 0x44d230 | `face_sprite_fixed` | 7, a sprite through the `.PAN` (lens flares) |
| 11 | 0x44d3a0 | `face_line_11` | 11, a line |
| 12 | 0x44b690 | `face_line_12` | 12, a line |
| 13 | 0x44d470 | `face_line_shade` | 13, a line through a table (rain) |
| 15 | 0x44b770 | `face_flat` | 15, a flat triangle |
| 16 | 0x44b980 | `face_flat16` | 16, a shaded triangle |
| 17 | 0x44c2e0 | `face_texture` | 17, an affine textured triangle |
| 17, MIP MAPPING on | 0x44dc60, 0x44c610 | `face_texture_mip`, `face_texture_mip_b` | 22 from a magnified tile, or 17 from a blurred level |
| 17, PERSP. POLY on | 0x44bb90 | `face_texture_persp` | 20 near the eye, else 17 |
| 18, 19, 22, 23 | 0x44caa0, 0x44cac0, 0x44cae0, 0x44cb00, 0x44cb20 | `face_texture_18` and so on, `face_texture_table` | 18 through the colour key (18, 22) or the `.SHD` (19, 23); 21 near the eye with PERSP. POLY on |
| 20 | 0x44cf00 | `face_flat_table` | 19, a flat triangle through the `.SHD` (shadows) |
| 21 | 0x44d550 | `face_texture_21` | 20 near the eye with PERSP. POLY on, else 17 |
| 17, 21 with both on | 0x44e1b0, 0x44e900 | `face_texture_persp_mip`, `face_texture_21_mip` | 20 or 17 from a level |

## Mip levels

With MIP MAPPING on, `sw_tex_make` [0x447280] (`src/soft3d/mip.c`)
builds a texture slot's blurred levels when the race makes its world:
`blur_box` [0x447fb0] and `blur_wide` [0x4481f0] filter in RGB and map
back to the palette through `make_lookup`'s [0x448620] 64 x 64 x 64
table, and `magnify` [0x4475c0] makes the 4 x 4 magnified tiles that
command 22 draws near the eye. The face handlers pick a level from the
face's area on the screen against its area in the texture.

## The rasteriser

`sw_raster_draw` [0x44f0e9] dispatches each command on its type (the
jump table at 0x4abe10). The fillers are hand written assembly in the
original; the port keeps their fixed point arithmetic, edge rules and
order of pixels, one pixel at a time where the original unrolls.

| Command | Routine | Port | Draws |
|---|---|---|---|
| 0 | 0x454548 | `sw_raster_point` (`flat.c`) | the clip rectangle in one colour (no face makes it) |
| 1 | 0x450630 | `sw_raster_point_flat` | a pixel of `table[colour << 8]` |
| 2 | 0x453790 | `sw_raster_point_table` | a pixel through a table |
| 7 | 0x455648 | `sw_raster_sprite` (`tex.c`) | a sprite, as two type 18 triangles |
| 11 | 0x451747 | `sw_raster_line` (`flat.c`) | a line |
| 12 | 0x4549a3 | `sw_raster_line_12` | a line with a shade ramp |
| 13 | 0x44f0e9 | `line_shade` (`raster.c`) | a line through a table, its colour ramped |
| 15 | 0x450693 | `sw_raster_flat` (`flat.c`) | a flat triangle |
| 16 | 0x452ecb, 0x4535ab | `sw_raster_flat16`, `gouraud_part` | a Gouraud shaded triangle |
| 17 | 0x452800 | `sw_raster_texture` (`tex.c`) | an affine textured triangle |
| 18 | 0x44ff60 | `sw_raster_texture_table` | the same, each texel `table[texel << 8 \| dst]` |
| 19 | 0x454604, 0x4547c8, 0x4548c0, 0x454926 | `sw_raster_flat_table` (`flat.c`) | a flat triangle through a table |
| 20, 21 | 0x4516f0, 0x4512e0, 0x450f90, 0x451470, 0x4537dc | `sw_raster_persp` (`persp.c`) | a perspective correct textured triangle, in runs of 16 pixels; 21 through a table |
| 22 | 0x452050 | `sw_raster_texture_mip` (`tex.c`) | an affine textured triangle from a mip level |

The affine textured triangles share edge and span code in the original's
`code` segment (0x64e000 to 0x64ea00) and two span drawers, 0x4559a8 and
0x4558c0. In `tex.c`, `gradients` [0x64e000] finds how u and v change
across a row, the edge routines [0x64e200 to 0x64ea00] fill the left and
right edges of each row, and `spans_texel` [0x4559a8] and `spans_table`
[0x4558c0] draw the rows from their right end leftwards. The texture
coordinates are 8.8 texels of a 256 x 256 page, `tex[v << 8 | u]`.

## In the port

`src/soft3d/port.c` connects the pipeline to the game:

- `soft3d_race_view` draws a race view into `g_back_buffer`, and
  `soft3d_race_fill` fills the rows the original fills before it (the
  band at the top of one track's view). `race_draw_view` and `race_view_fill`
  (`src/race/world.c`) call them under `--soft3d`.
- `soft3d_menu_car` draws the front end's car into the menu screen
  (`src/menu/car.c`).
- `soft3d_raster` takes the commands that `gl3d_raster` is given (the
  front end's background, the HUD's needle and bars) and draws them
  with `sw_raster_draw`.

`--soft3d`, or SOFT 3D for RENDERER in GFX OPTIONS, keeps the 2D on
the CPU, since the 3D draws into the same buffers. A race runs at its resolution's original size; NATIVE races
at 800 x 600.

## Not ported

- The rotated sprite fillers of the 2D (0x45ca50, 0x45d170, 0x45d8a0,
  0x45dfb0 and their code segment helpers 0x64ea80 to 0x64f4d0), which
  `blit_turned` [0x468c70] uses. The port draws those sprites its own
  way.
