# Game file formats

This is a reference of the files the engine reads from the disc. For
each format it gives where the files are, their layout, and the code
that reads them. Paths are relative to the disc root, as `make extract`
lays it out under `data/<image>/disc/`. [engine.md](engine.md) explains
how the engine uses the data.

The layouts come from the code and were checked against the files on
the disc. Where a field's meaning is not known, the table says
**unknown**. Fields that the code names by their offset, such as `f0c`,
or by an address, such as `r_552f10`, are unknown too.

## Conventions

- All numbers are little endian. Offsets and sizes are in hex.
- `s32`, `u16` and the like are signed and unsigned integers of that
  many bits. `8.8` and `16.16` are fixed point numbers with that many
  bits after the point.
- Colours are 8 bit indices into a palette of 256 RGB entries, 8 bits a
  channel.
- The game opens files by their DOS paths, such as
  `LEVELS\CANADA\CANADA.MSH`, in any case. `vfs_fopen` (`src/vfs.c`)
  finds them under the data folder whatever their case on the host.
- The C functions name the original's address in a comment, for
  example `/* 0x418dd0 */`. This document gives those addresses in
  brackets.

## The files by folder

| Path | Format | Read by |
|---|---|---|
| `SYS.COL`, `N_SYSGFX.PIC`, `N_SYSG_2.PIC` | palette, pictures | `game_load_system_gfx` [0x418130], `src/startup.c` |
| `MENUBKG.DAT` | menu panel tiles | `menu_load_background` [0x401000], `src/startup.c` |
| `S_PANGFX.PIC`, `N_PANGFX.PIC`, `H_PAN1.PIC`, `H_PAN2.PIC`, `S_SIGNS.PIC`, `N_SIGNS.PIC`, `H_SIGNS.PIC`, `POKAL.PIC` | HUD pictures | `load_pictures` [0x41af70], `src/race/load.c` |
| `INSTALL.PIC` | picture | not by the game; `tools/vita_sce_sys.py` |
| `BALTAZAR/DATA/` | the front end's files | `src/frontend/`, `src/menu/` |
| `BALTAZAR/DATA/SOUND/0_GRP0/` | menu sounds | `menu_sound_init` [0x403bc0] |
| `BALTAZAR/DATA/TEXTURES/` | the menu background's textures | `menubg_load_pages` [0x40cd60] |
| `FONTS/` | fonts | `game_load_system_gfx` [0x418130], `load_fonts` [0x41ac40] |
| `LEVELS/<TRACK>/` | one folder a track | `src/race/load.c`, `init.c`, `ai.c` |
| `CARS/` | car meshes, textures and sounds | `src/race/load.c`, `cars.c`, `sound.c` |
| `GENERAL/` | effect textures and sounds | `load_textures` [0x419d10], `race_load_sounds` [0x41f9b0] |
| `GHOSTS/` | time trial ghosts, written by the game | `race_load_ghost` [0x420240] |
| `ign_win.btz` | settings, written by the game | `src/frontend/frontend.c`, `src/race/load.c` |

The disc also holds the Windows and DOS programs, DirectX, an installer
and a manual. The engine reads none of them.

## Pictures (.PIC)

An 8 bit picture with its own palette. `src/pic.h` has the accessors.

| Offset | Size | Field |
|---|---|---|
| 0x000 | u32 | file size |
| 0x004 | u16 | 0x9500, the magic |
| 0x006 | u16 | width |
| 0x008 | u16 | height |
| 0x00a | s16 | x, unknown; the game does not read it |
| 0x00c | s16 | y, unknown; the game does not read it |
| 0x00e | u32 | 0 |
| 0x012 | u16 | 8, bits per pixel |
| 0x014 | 0x2c | zero |
| 0x040 | u32 | 0x308, the palette block's size, as at the start of a `.COL` |
| 0x044 | u32 | 0 |
| 0x048 | 0x300 | palette, 256 x RGB |
| 0x348 | u32 | width x height + 6, the pixel block's size |
| 0x34c | u16 | 1, unknown |
| 0x34e | width x height | pixels, rows top down |

Every picture on the disc matches this layout, with nothing after the
pixels. A level's picture has the same palette as the level's `.COL`.

The game uses only the size and the pixels. It takes pixels from 0x34e
with a stride of the width, or reads only the pixels with
`file_read_at(name, buf, size, 0x34e)` (`menu_load_assets` [0x403e80]).
The pictures are sprite sheets: the code cuts sprites from fixed
rectangles of them (`cut_sprite` in `src/startup.c`, `cut` in
`src/race/hud.c`).

`src/pic.h` names these offsets (`PIC_PALETTE`, `PIC_PIXELS`); only
`--view` reads the palette.

| File | Size | Use |
|---|---|---|
| `N_SYSGFX.PIC` | 256 x 412 | car badges, position icons, the bolt, panels |
| `N_SYSG_2.PIC` | 242 x 172 | the large place numbers |
| `S_PANGFX.PIC`, `S_SIGNS.PIC` | 123 x 139, 56 x 56 | the HUD's small layout |
| `N_PANGFX.PIC`, `N_SIGNS.PIC` | 227 x 271, 112 x 112 | the HUD's normal layout, and sprites of every layout |
| `H_PAN1.PIC`, `H_PAN2.PIC`, `H_SIGNS.PIC` | 256 x 256, 256 x 320, 256 x 256 | the HUD's high layout |
| `POKAL.PIC` | 207 x 426 | the cups on the podium |
| `BALTAZAR/DATA/BILAR.PIC` | 186 x 671 | the cars on the car page |
| `BALTAZAR/DATA/TRK_SPR.PIC` | 115 x 693 | the tracks on the track page |
| `BALTAZAR/DATA/IGN_LOGO.PIC` | 173 x 57 | the logo |
| `BALTAZAR/DATA/CAR_SEL.PIC` | 255 x 74 | the car page's frame |
| `BALTAZAR/DATA/FLAGGOR.PIC` | 61 x 636 | the language flags |
| `LEVELS/<T>/<T>.PIC` | 320 x 200 | the track's loading screen |
| `INSTALL.PIC` | 640 x 480 | the DOS installer's backdrop; the Vita LiveArea art |

`BALTAZAR/DATA/CONSPR.PIC`, `DEMOCAR.PIC`, `DEMOTRK.PIC`, `MARK.PIC`,
`LEVELS/USA/USA0.PIC` to `USA2.PIC` and the `FLAGGOR.PIC` in the root
are not read.

## Palettes (.COL)

`SYS.COL`, `BALTAZAR/DATA/MENU.COL` and `LEVELS/<T>/<T>.COL`, 0x308
bytes each.

| Offset | Size | Field |
|---|---|---|
| 0x000 | u32 | 0x308, the file size |
| 0x004 | u32 | 0xb123, unknown |
| 0x008 | 0x300 | palette, 256 x RGB, 8 bits a channel |

The code skips the first 8 bytes. `menu_init` [0x4029a0] reads 0x300
bytes from offset 8 with `file_read_at`, and `race_init_hud` [0x43dea0]
sets `g_level_col + 8` as the palette.
`SYS.COL` is the palette from start up to the menu
(`game_video_init` [0x417ea0]). The rain darkens the level's palette
(`race_palette_tint`, `src/race/fx.c`).

## Colour tables (.TAB, .PAN, .SHD)

A colour table is 0x10000 bytes: `T[s << 8 | d]` is the colour that
source colour `s` gives over destination colour `d`. Sprites, text and
some faces draw through one (`src/lisa/sprite.c`, `src/canvas.c`). The
game also builds two tables of its own: the identity table,
`T[s << 8 | d] = s` (`game_video_init` [0x417ea0]), and the colour key,
`s ? s : d` (`race_make_tables` [0x41b400]).

| File | Read by | Use |
|---|---|---|
| `BALTAZAR/DATA/MENU.TAB` | `menu_init` [0x4029a0] | the front end's table: sprites, text, panels and the iris |
| `LEVELS/<T>/<T>.PAN` | `load_level_files` [0x418dd0] | the race's 2D: the loading screen, HUD, boxes, results, iris; type 8 sprites |
| `LEVELS/<T>/<T>.SHD` | `load_level_files`, `menubg_load_shades` [0x40ce00] | see-through faces and sprites (types 7, 19, 20, 23) |
| `LEVELS/<T>/<T>.TAB` | `load_level_files` | loaded; nothing in the port reads it |
| `INSTALL.TAB` | none | the DOS installer's |

The rows of `MENU.TAB` and `.PAN` have the same shape. Rows 0 to 5
blend with the colour under them, and so do rows 10 to 14 (in a `.PAN`,
all of 0 to 14). The iris and the panels darken the screen through
them: the cone table (`cone_table_init` [0x4200e0]) picks row 0 to 5 by
the distance from the circle's edge, and row 20 outside it, which
draws colour 0 (colour 140 in `CARIB.PAN`). From row 15 on, each row
draws one colour, the source colour itself or another.

In a `.SHD`, row 0 is the identity (colour 0 leaves the pixel as it
is). Many other rows blend; the rest draw one colour.

A level's `.TAB` is indexed `T[colour << 8 | shade]`. For a shade of
0x40 and up it gives the colour back; shades 0 to 0x3f give darker or
lighter colours. Row 0 is all 0. [NOTES.md](../NOTES.md) says the
original's flat faces look up `TAB[colour << 8 | shade]`; the port
draws flat faces in their colour and does not read the `.TAB`.

The colour tables of a level are aligned in memory: `.PAN` and `.SHD`
to 64 KB, `.TAB` to 256 bytes. The original's rasteriser adds the
pixels to the table's address.

## Fonts (.LFT)

A bitmap font. `src/lisa/font.h` has the layout; `font_parse`
[0x456270] reads it and `font_load` [0x456420] loads it.

| Offset | Size | Field |
|---|---|---|
| 0x000 | 4 | `"LFT\0"` |
| 0x004 | u16 | 100, the version |
| 0x006 | u16 | number of glyphs |
| 0x008 | u16 | cell width |
| 0x00a | u16 | height |
| 0x00c | 224 x s32 | for characters 32 to 255, the offset of the glyph's pixels from 0x84c, or -1 for none |
| 0x38c | 0x300 | palette; the game does not use it |
| 0x68c | 224 x s16 | glyph widths |
| 0x84c | | glyph pixels, `width` x height each, rows top down |

Each glyph becomes a sprite, so text draws through the colour table.
The menus use `BALTAZAR/DATA/RED_DARK.LFT`, `RED_LITE`, `BLUEDARK`,
`BLUELITE`, `RED_GREY`, `BLUEGREY`, `FONTS/MINI.LFT` and
`FONTS/SMALL.LFT` (`game_load_system_gfx` [0x418130]). The HUD uses
`YELLOW`, `POS` and `SPEED` (times, places and speed) in three sizes:
`_S` for the small layout, no suffix for the normal one and `_H` for the
high one (`load_fonts` [0x41ac40]). `race_hud_layout` [0x41f3a0] picks
the layout by the resolution, the view's size and the split screen.

| Font | Characters |
|---|---|
| the six menu fonts | `!` to `}` |
| `MINI`, `SMALL` | `!` to `j` |
| `RESULT` | `!` to `_`; not read |
| `POS*` | `/` to `6` |
| `SPEED*` | `0` to `9` |
| `YELLOW*` | `-` to `:` |

The `SMALL.LFT` in the disc's root has 70 glyphs and is not read.

`FONTS/IGNITION.FNT` (0x1518 bytes) is loaded by `load_fonts`
[0x41ac40] into `g_race_fnt`, and nothing reads it after that. Its
layout is unknown. It starts with the u16 values 0x38 and 0xa, then
0xffff, then bytes that look like widths.

## Intro animations (.CDP)

`BALTAZAR/DATA/IGN1.CDP`, `IGN2.CDP` and `IGN3_0.CDP` to `IGN3_3.CDP`,
played in that order after the language page. `src/cdp.c`;
`choose_language` in `src/menu/pages.c` loads them.

| Offset | Size | Field |
|---|---|---|
| 0x000 | 4 | `"CDP\0"` |
| 0x004 | u16 | 100, the version |
| 0x006 | u16 | number of frames |
| 0x008 | u16 | 1 to loop; 0 on the disc |
| 0x00a | u16 | width, 320 |
| 0x00c | u16 | height, 200 |
| 0x00e | u16 | 0x100, unknown |
| 0x010 | 0x300 | palette |
| 0x310 | | frames |

Each frame is a byte code that draws over the last frame
(`decode` [0x499abc]):

| Byte | Meaning |
|---|---|
| 0 to 245 | a pixel of that colour |
| 246 | the next byte is a pixel |
| 247 to 250 | skip 2 to 5 pixels |
| 251 | skip the next byte's count of pixels |
| 252 | skip the next u16's count of pixels |
| 253 | a run: a u8 count (0 is 256), then the colour |
| 254 | a run: a u16 count, then the colour |
| 255 | the end of the frame |

`IGN1.CDP` and `IGN2.CDP` hold two frames fewer than their headers
give. The original reads on past the file; the port ends the animation
at the end of the data.

## Menu background (TEST.PFM, DEFAULT.PSQ)

The front end's background is a film of the track and cars drawn as
lists of screen space faces. `src/menu/bg.c` plays it; `menucar_load`
[0x40ca60] loads the files.

`BALTAZAR/DATA/TEST.PFM` is a list of chunks: a u32 tag, an s32 size,
then that many bytes. `menubg_parse` [0x40ffe0] reads them.

| Tag | Content |
|---|---|
| `li01` | a frame's vertices: s16 x, y in pixels, 12.4 fixed point. It comes before its frame's `li00`. |
| `li00` | a frame's drawing script: records of u8 type, u8 count, then count commands, up to a type of 0xff |
| `li02` | 16 bytes a textured face: u16 u0, v0, u1, v1, u2, v2 in 8.8 texels, s32 page |
| `li03` | 16 bytes a sprite: s16 hot spot x, y; u16 u0, v0, u1, v1 in texels; u16 page; u16 table |
| `lixx` | the end, size 0 |

`TEST.PFM` has 1124 frames, one `li02` of 1597 records and one `li03` of
59. The commands, by type (`build_frame` [0x410440]):

| Type | Bytes | Fields | Draws |
|---|---|---|---|
| 7 | 8 | s16 sprite, u16 x scale, u16 y scale (8.8), s16 vertex | a sprite through its table |
| 11 | 5 | unknown | a line; `TEST.PFM` has none |
| 13 | 7 | unknown | skipped; `TEST.PFM` has none |
| 15 | 7 | 3 s16 vertices, u8 colour | a flat face of colour `MENU.TAB[colour << 8]` |
| 17 | 8 | 3 s16 vertices, s16 `li02` record | a textured face |
| 18 | 9 | as 17, then u8 table | a textured face through a table |
| 19 | 8 | 3 s16 vertices, u8 colour, u8 table | a flat face through a table |

The pages are the eight files of `BALTAZAR/DATA/TEXTURES/` (`CANADA`,
`CARS`, `DIVSPR`, `LIGHT`, `SMOKE`, `DARKSMOK`, `BOOM`, `EXSMOKE`), read
one after another into 30 pages at most (`menubg_load_pages`
[0x40cd60]). Table 0 is `LEVELS/CANADA/CANADA.SHD`; table 1 leaves
colour 0 out (`menubg_load_shades` [0x40ce00]).

`BALTAZAR/DATA/DEFAULT.PSQ` says in which order the frames play: 12
bytes an entry, s32 first frame, s32 number of frames, s32 0 (unknown).
`menu_background_render` [0x40c740] plays the entries in turn and then
starts again. `DEFAULT.PSQ` has 24 entries.

`TEST2.PFM` (209 frames) and `DEFAULT2.PSQ` (11 entries, which fit it)
are a shorter film of the same format. They are not read.

## Menu panels (MENUBKG.DAT)

0x9358 bytes of tiles for the rounded boxes behind the menus' text.
`menu_load_background` [0x401000] reads it; `panel_draw` [0x4011c0]
draws from it. A tile is 19 rows, stored bottom up; colour 0 is clear.

| Offset | Size | Content |
|---|---|---|
| 0x0000 | 0x130 | top left corner, 16 x 19 |
| 0x0130 | 0x130 | top right corner, 16 x 19 |
| 0x0260 | 0x130 | bottom left corner, 16 x 19 |
| 0x0390 | 0x130 | bottom right corner, 16 x 19 |
| 0x04c0 | 27 x 0x260 | left joins to the box above, 32 x 19, for an offset of -13 to 13 |
| 0x44e0 | 31 x 0x260 | right joins to the box above, 32 x 19, for an offset of -15 to 15 |
| 0x8e80 | 4 x 0x130 | four joins of 16 x 19, for the larger offsets |
| 0x9340 | 0x18 | the colour of each of the 24 rows of a box's inside |

`menu_load_background` also sets up `g_menu_remap`, which moves eight
colours for highlighted boxes.

## Meshes (.MSH)

`LEVELS/<T>/<T>.MSH`, `CARS/CARS.MSH` and `BALTAZAR/DATA/MENUCAR.MSH`.
A `.MSH` is meshes one after another, one for each placement of the
`.PLC` of the same name, in its order. A placement names its mesh by
its offset in dwords from the start of the file.

| Offset | Size | Field |
|---|---|---|
| 0x00 | s32 | vertex count, `nv` |
| 0x04 | s32 | face count, `nf` |
| 0x08 | nv x 12 | vertices: s32 x, y, z, from the placement's origin; y points down |
| 0x08 + 12 nv | nf x 0x2c | faces |

Every face in the files is 0x2c bytes:

| Offset | Size | Field |
|---|---|---|
| 0x00 | u8 | face type, see below |
| 0x01 | u8 | 0; unknown |
| 0x02 | u16 | surface (material); see "Surface types" |
| 0x04 | 3 x s32 | vertex indices |
| 0x10 | 6 x s32 | u0, v0, u1, v1, u2, v2: 8.8 texels in a 256 x 256 page |
| 0x28 | u16 | 0; unknown |
| 0x2a | u16 | the page in the object's texture slot |

The type picks the face's handler (the table at 0x49c8e0). The files use
17, 18, 19, 21 and 22. The game builds meshes of its own with the other
types, of other sizes (shadows, ghosts, weather, effects, the flare):

| Type | Bytes | Face |
|---|---|---|
| 1, 2 | 0xc | a point: vertex, colour; 2 is twice the size of 1 |
| 7 | 0x24 | a sprite: vertex, u0, v0, u1, v1 (8.8 texels), the page in the high half of the next dword, x and y scale; through the `.SHD`, scaled by distance |
| 8 | 0x24 | as 7, through the `.PAN`, at a fixed size on whole pixels |
| 11, 12 | 0x10 | lines; not drawn by the port |
| 13 | 0x18 | a line a pixel wide: two vertices, colours from and to (8.8), and whether it goes through the `.SHD` |
| 15, 16 | 0x14 | a flat face: three vertices, colour |
| 17, 21 | 0x2c | a textured face |
| 18, 22 | 0x2c | a textured face, colour 0 left out |
| 19, 23 | 0x2c | a textured face through the `.SHD` |
| 20 | 0x14 | a flat face through the `.SHD` (shadows, ghosts) |

[NOTES.md](../NOTES.md) says the original draws 17, 18 and 22 affine
and 20 and 21 perspective correct. A type that is not in the table
ends the mesh.

`mesh_build` and `collect` in `src/gl3d.c` turn the faces into
triangles; `face_size` gives the sizes. `race_track_order` [0x416250],
`srf.c`, `ai.c` and `respawn.c` step through faces 11 dwords at a time,
which holds for every mesh on the disc.

## Placements (.PLC)

`LEVELS/<T>/<T>.PLC`, `CARS/CARS.PLC` and `BALTAZAR/DATA/MENUCAR.PLC`.
`load_plc` [0x419a90] reads the race's; `src/race/race.h` has
`struct plc_entry`.

| Offset | Size | Field |
|---|---|---|
| 0x00 | s32 | count, `n` |
| 0x04 | n x 0x14 | placements |

| Offset | Size | Field |
|---|---|---|
| 0x00 | s32 | the mesh's offset in dwords into the `.MSH` |
| 0x04 | u32 | type, and three fields above it |
| 0x08 | s32 | x |
| 0x0c | s32 | y, up |
| 0x10 | s32 | z |

`race_unpack_types` [0x41b360] splits the type word into
`g_plc_extra`:

| Bits | Field | Use |
|---|---|---|
| 0 to 11 | type | see below |
| 12 to 15 | `f0`, group | which objects are drawn: with a group set, the view draws only group 0 and the group of the piece the camera's car is on (`group_at`, `src/race/view.c`) |
| 16 to 23 | `f1`, slope | the road piece's slope for the chase camera: 1 to 10 down, 11 to 20 up (`race_chase_camera` [0x43c910]) |
| 24 to 31 | `f2`, sign | the road sign the HUD shows on this piece, 0 for none (`signs`, `src/race/hud.c`) |

A level's objects stand at (x + 0x6400, y, z + 0x6400), in a world of
0xc800 x 0xc800 units (`race_make_world` [0x41b470]).

A level's placement types:

| Type | Object |
|---|---|
| 0 | the road piece at the start line |
| 1 | the road piece after the start |
| 2 | road; the code also takes 9 as road, which the disc does not use |
| 3 | a fork: the road splits |
| 4 | the first branch of a fork |
| 5 | the second branch |
| 6 | where the branches meet |
| 7 | a road piece that pairs with the nearest other 7 |
| 8 | a road piece not counted in the track's length; its role is unknown |
| 50 | unknown; centred like a road piece, not counted |
| below 100 | other scenery, made first |
| 150 to 199 | a collision object that the cars knock (150, 152, 153, 154; `make_collision_objects` [0x41bb1f]) |
| 200 | an object of class 200, some with a path in the `.POS`; its role is unknown |
| 250 | an animated texture's key frames, see "Animated textures" in engine.md |
| 300 to 349 | a moving object or trigger zone (`make_movers` [0x41c1aa]); 300 to 305 trigger events in `race_contact` [0x424570] |
| 350 to 359 | a lens flare or light (`make_flares` [0x41bf2b]); 354 a flare sprite |
| 400 to 498 | a sound source: sample type - 400 of the track's bank (`make_sound_sources` [0x41be37]) |

`CARS/CARS.PLC` has 55 placements: type 10 k is car model k's body and
10 k + 1 to 10 k + 4 its wheels, for k from 0 to 10 (`load_models`,
`src/race/cars.c`). `BALTAZAR/DATA/MENUCAR.PLC` has 11: the type is the
car's number, and x, y, z are not used (`menucar_load` [0x40ca60] stands
every car at (1000, 1000, 1000)).

## Textures (.TEX)

Pages of 256 x 256 texels, 0x10000 bytes each, from the start of the
file. There is no header. Some files end in a part page or zeros:

| File | Size | Pages |
|---|---|---|
| `LEVELS/<T>/<T>.TEX` | 0xfc000 to 0x104080 | 16; `JAPAN` 15.75; `ICELAND` 16 and 0x4080 more bytes; `AUSTRIA` and `CARIB` 16 and 0xc0 or 0x80 zero bytes |
| `CARS/CARS.TEX` | 0x60040 | 6, then 0x40 zero bytes |
| `GENERAL/DIVSPR.TEX`, `LIGHT`, `SMOKE`, `BOOM`, `EXSMOKE` | 0x10000 | 1 |
| `GENERAL/DARKSMOK.TEX` | 0x8000 | half a page |
| `BALTAZAR/DATA/MENUCAR.TEX` | 0x60040 | the same file as `CARS/CARS.TEX` |
| `BALTAZAR/DATA/TEXTURES/*.TEX` | | the menu background's; `CANADA`, `CARS` and `DIVSPR` differ from the race's copies |

`load_textures` [0x419d10] reads the race's textures into one block,
each file at a 64 KB boundary, rounded up to whole pages. The block's
slots:

| Slot | File |
|---|---|
| 0 | the track's `.TEX` |
| 1 | `CARS/CARS.TEX` |
| 2 | `GENERAL/DIVSPR.TEX` |
| 3 | `GENERAL/LIGHT.TEX` |
| 4 | `GENERAL/SMOKE.TEX` |
| 5 | `GENERAL/DARKSMOK.TEX` |
| 6 | `GENERAL/BOOM.TEX` |
| 7 | `GENERAL/EXSMOKE.TEX` |
| 8 | a blank page; the original draws network players' names into it |

An object names its slot, and a face its page within the slot. The
effects name their sprite sheets by slot.

## Moving objects (.POS)

`LEVELS/<T>/<T>.POS`, read by `load_level_files` [0x418dd0]. It is an
array of s32, starting with one for each placement:

| Offset | Size | Field |
|---|---|---|
| 0x00 | n x s32 | for placement i: -1, or `rel`, so that its path is at dword `rel + i` |

A path, at dword `rel + i`:

| Offset | Size | Field |
|---|---|---|
| 0x00 | s32 | key count |
| 0x04 | s32 | the current key, 0 in the file |
| 0x08 | keys x 0x18 | keys: s32 x, y, z, then s32 angles about x, y and z in tenths of a degree |

The keys' x, y, z are positions. `race_pos_deltas` [0x4356d0] turns them
into the step from each key to the next, the last back to the first.
`race_animate_objects` [0x4357a0] moves the object one key a tick. On
the disc the paths belong to placements of types 200, 300 to 304, 351,
357 and 400 to 408, with 45 to 1201 keys.

## Surfaces (.SRF)

`LEVELS/<T>/<T>.SRF` is a grid over the ground that lists, for each
cell, the faces of the world's objects that lie over it.
`race_srf_load` [0x412670] loads it; `srf_lookup` [0x412fc0] reads it.
`src/race/init.h` has `struct srf`.

| Offset | Size | Field |
|---|---|---|
| 0x00 | s32 | 200; `ext_z`, unknown, not read |
| 0x04 | s32 | 200; `ext_x`, unknown, not read |
| 0x08 | s32 | cell height in z, 0x200 |
| 0x0c | s32 | cell width in x, 0x200 |
| 0x10 | s32 | grid width `gw`, 101 |
| 0x14 | s32 | grid height `gh`, 101 |
| 0x18 | s32 | span count, `ns` |
| 0x1c | s32 | list A's length, `na` |
| 0x20 | s32 | list B's length, `nb` |
| 0x24 | gw x gh x 0xc | cells, rows of z |
| | ns x 0x18 | spans |
| | na x u32 | list A: byte offsets into the spans |
| | nb x u32 | list B: byte offsets into the spans |

A cell:

| Offset | Size | Field |
|---|---|---|
| 0x0 | u32 | byte offset into list B |
| 0x4 | u32 | byte offset into list A |
| 0x8 | u16 | count in list A |
| 0xa | u16 | count in list B |

A span is the stretch of a face over the cell: an x range that moves
along z.

| Offset | Size | Field |
|---|---|---|
| 0x00 | s32 | x where it starts |
| 0x04 | s32 | z where it starts |
| 0x08 | s32 | slope of the left end, x per z, 16.16 |
| 0x0c | s32 | slope of the right end, x per z, 16.16 |
| 0x10 | s16 | its length in z: 0 or more in list B, 0 or less in list A |
| 0x12 | u16 | the face, in dwords into the object's mesh |
| 0x14 | u32 | the object, in bytes into the object pool: index x 0x2a |

The object is an index into the scene's pool, in the order
`race_make_world` [0x41b470] creates the objects: the placements below
type 100 first, then the rest. The file only works with that order.
Every span on the disc names a whole object.

The surface of the ground is the face's surface (bits 16 to 31 of its
first dword). `srf_lookup` takes, of the faces over a point, the one
nearest the height asked for, and prefers any face to off-track ground.

### Surface types

What the code shows of the surface numbers:

| Surface | Meaning |
|---|---|
| 0 to 29 | ground; the grip is `g_surface_grip[surface]` (`race_contact`), which repeats every 10 |
| units digit 4 | dirt: dust behind the car (`race_dust_trail` [0x433f90]) |
| units digit 5 | sand: no skid smoke |
| units digit 6 | water or mud: spray (`race_spray` [0x4333e0]) |
| units digit 9 | grip 0.01; its name is unknown |
| below 20 | on the road; above 19 the car's corners are probed again at half size |
| 40 to 79 | off the track: any other face wins, and a car on it for 5 steps is put back |
| 40 and up | not used to place a car put back on the road (`race_respawn` [0x435910]) |
| 0x50 and up | walls; 0x51, 0x5c, 0x6f are low walls; 0x5a gives sparks |
| 100 | landing on it blows the car up (`race_car_fall` [0x423aa0]); so do 20 to 29 and 110 to 130 while the car's `deck` flag is set, whose meaning is unknown |
| 101 | landing on it sinks the car; so do 30 to 39 and 130 to 150 while `deck` is set |

The road pieces' centres use the faces of surface 0 to 4 and 6 of each
ten below 40 (`centre_face`, `src/race/track.c`). The rest of the
numbers are unknown.

## AI track (.TRI)

`LEVELS/<T>/<T>.TRI`, read by `race_load_ai_track` [0x414e40].

| Offset | Size | Field |
|---|---|---|
| 0x00 | s32 | unknown (247 for `CANADA`, 214 for `USA`, 341 for `ICELAND`); not read |
| 0x04 | n x 0x1f4 | one record for each placement |

A record:

| Offset | Size | Field |
|---|---|---|
| 0x000 | u8 | surface for the AI's speed, into `g_surface_grip` |
| 0x001 | s16 | vertex of the piece's mesh at one edge of the road |
| 0x017 | s16 | vertex at the other edge |
| 0x06d | u8 | lanes: 0 one; 1 or 2 split across into two halves, in either order |
| others | | unknown; many bytes from 0x003 to 0x05f are not zero |

Each road piece in the track's order gives one node of the racing line
from its two vertices, or two nodes where the record splits it.
`ICELAND.TRI` has 233 records for 247 placements, so its read comes up
short and the port logs `race_ai: no LEVELS\ICELAND\ICELAND.TRI`. The
records it has are read.

## The tracks (LEVELS)

Each track has a folder `LEVELS/<T>/` with the files `<T>.COL`, `.PAN`,
`.PIC`, `.SHD`, `.TAB`, `.PLC`, `.MSH`, `.TEX`, `.POS`, `.TRI`, `.SRF`
and a `SOUND/` folder. `load_level_files` [0x418dd0] reads them in that
order, except `.TRI` (`race_load_ai_track` [0x414e40]) and `.SRF`
(`race_load_world` [0x418c40]).

| Index | Folder | Name | Ghost file | CD track | Weather |
|---|---|---|---|---|---|
| 0 | `CANADA` | Moosejaw Falls | `MOOSEJAW.GST` | 2 | rain |
| 1 | `USA` | Gold Rush | `GOLDRUSH.GST` | 3 | a sound only, the track's sample 2 |
| 2 | `CARIB` | Snake Island | `SNAKEISL.GST` | 4 | none |
| 3 | `BRAZIL` | Lost Ruins | `LOSTRUIN.GST` | 5 | none |
| 4 | `AUSTRIA` | Yodel Peaks | `YODELPEA.GST` | 6 | snow |
| 5 | `ICELAND` | Cape Thor | `CAPETHOR.GST` | 6 | none |
| 6 | `JAPAN` | Tokyo Bullet | `TOKYOBUL.GST` | 7 | none |

The names, folders and CD tracks are tables in `src/race/load.c`
(`s_tracks`, `s_track_titles`, `s_track_cd`). The championship's order
of tracks depends on the difficulty (`s_championship`).

## The cars (CARS)

| File | Content |
|---|---|
| `CARS/CARS.PLC`, `CARS.MSH` | the bodies and wheels of the 11 models |
| `CARS/CARS.TEX` | their textures, slot 1 |
| `CARS/<CAR>/SOUND/00_A.*`, `01_A.*` | the engine's two loops, `.WAV` or `.PAT` |
| `CARS/<CAR>/SOUND/ENGINE.INF` | the engine's pitch and loudness by revs |
| `CARS/TEST.AIS` | text: nine AI settings for "car 1" (`acc`, `carebed`, `caresight`, `careother`, `maintain`, `aggr`, `sight`, `curveinflu`, `slopeinflu`); the port does not read it |

The models, in their order (`g_car_dirs`, `src/race/data.c`): `COOPER`,
`PORSCHE`, `JEEP`, `COP`, `MUSTANG`, `SCHOOL`, `VAN`, `VW`, `TRUCK`,
`DODGE`, `NASCAR`. `race_load_cars` [0x41d190] moves each body so its
top is at y 0 and keeps each wheel's offset from the body.

`ENGINE.INF` is 0x320 bytes: four tables of 200 s8, indexed by revs
(`race_load_sounds` [0x41f9b0]).

| Offset | Size | Field |
|---|---|---|
| 0x000 | 200 x s8 | the first loop's pitch |
| 0x0c8 | 200 x s8 | the first loop's loudness |
| 0x190 | 200 x s8 | the second loop's pitch |
| 0x258 | 200 x s8 | the second loop's loudness |

The car's physics, the AI's settings and the curves are tables in the
executable, not files (`src/race/data.c`, made by
`tools/gen_race_data.py`).

## Effects (GENERAL)

`GENERAL/*.TEX` are the effects' sprite sheets, texture slots 2 to 7
(see "Textures"). `GENERAL/SOUND/` holds the general sound bank.

## Sounds (.WAV, .PAT, .XI, .RW8)

A sound bank is a folder of group folders, and a group folder holds
samples. A sample's number is the number its file name starts with
(`00_BOOST.WAV` is sample 0); a group folder's number is the number its
name starts with. `sound_load_bank` [0x459140] and `sound_load_group`
[0x458e00] in `src/sound.c` read them, by the file's extension. The
first file that fails ends its group.

| Bank, group | Folder | Loaded by |
|---|---|---|
| menu 0, 0 | `BALTAZAR/DATA/SOUND/0_GRP0/`: `0_SEL`, `1_FLIP`, `2_NO`, `3_INTRO` | `menu_sound_init` [0x403bc0] |
| race 0, 0 | `GENERAL/SOUND/ROLL/`: rolling tyres; by their names on asphalt, gravel, mud, sand and snow | `race_load_sounds` [0x41f9b0] |
| race 0, 1 | `GENERAL/SOUND/SKID/`: skids on the same five | |
| race 0, 2 | `GENERAL/SOUND/COLL/`: crashes | |
| race 0, 3 | `GENERAL/SOUND/BOOST/`: the boost (sample 0) and a sound for each car | |
| race 0, 4 | `GENERAL/SOUND/DIV/`: the start lights, gears, the ghost and others | |
| race 1, 0 | `LEVELS/<T>/SOUND/`: the track's sounds | |
| race 2, i | `CARS/<car of driver i>/SOUND/` | |

`.WAV` (`load_wav` [0x4587d0]): RIFF WAVE, PCM, mono. On the disc all are
8 bit, at 11025, 14385 or 22050 Hz. The first loop of a `smpl` chunk is
the loop; without one the whole sample loops. Two files have a `smpl`
chunk.

`.PAT` (`load_pat` [0x4582b0]): a Gravis GF1 patch with one wave. The
loader reads the 0x14f byte header and the wave after it:

| Offset | Size | Field |
|---|---|---|
| 0x000 | 12 | `"GF1PATCH110\0"` |
| 0x0f7 | u32 | wave length in bytes |
| 0x0fb | u32 | loop start |
| 0x0ff | u32 | loop end |
| 0x103 | u16 | sample rate |
| 0x126 | u8 | modes: bit 0 16 bit, bit 1 unsigned |
| 0x14f | | the wave |

The other fields are the GF1 format's; the loader does not read them.
On the disc the patches are 8 bit unsigned, at 11025, 22050 or
22100 Hz.

`.XI` (`load_xi` [0x4584d0]): a FastTracker 2 instrument, its first
sample. The sample header is at 0x12a: u32 length, u32 loop start, u32
loop length, and at 0x138 the type byte, whose bit 4 means 16 bit. The
data follows at 0x152, delta coded. The rate is always 22050 Hz.

`.RW8` (`load_rw8` [0x4586a0]): signed 8 bit samples at 22050 Hz, the
whole file. The disc has none.

## Ghosts (.GST)

A time trial's best lap, written by the game when the player beats it
(`race_save_results` [0x4222b0]) and read by `race_load_ghost`
[0x420240]. The disc's `GHOSTS/` holds only a `README.TXT`. RESET GHOST
CAR deletes the seven files (`menu_cb_404790` [0x404790]). A file is
0x49d4c bytes:

| Offset | Size | Field |
|---|---|---|
| 0x00 | s16 | car model |
| 0x02 | s16 | track index; a ghost of another track is not used |
| 0x04 | s16 | the time in hundredths of a second |
| 0x06 | 4 | the driver's name: the writer puts the whole name here, then the recording over it from 0x0a, so only its first 4 bytes are kept, without an end when the name is longer |
| 0x0a | 21600 x 0xe | the recording: 7 s16 for each half tick of the physics (`race_ghost_record`, `src/race/phys.c`) |

A step: x - 0x6400, y (plus 5000 while the car is out), z - 0x6400,
the three angles in radians x 1024, and the car's progress along the
track. 21600 half ticks are 5 minutes. When the file is saved, the rest
of the recording repeats its last step, all but the progress.

## Settings (ign_win.btz)

The game's settings, 0x597 bytes, read and written whole. It is not on
the disc: the game writes it into the data folder. The front end reads
it only when its first word is 0x1e (`settings_load` in
`src/frontend/frontend.c`, from `menu_setup` [0x404a60]) and writes it
before a race or on quitting (`step_timers`, from `menu_frame`
[0x402c00]). The race reads its own copy into `g_race_cfg`
(`read_settings` [0x419260]) and writes it back after the race
(`race_save_settings` [0x4225d0], `race_end` [0x420870]).

The code reads fields by offset, some not aligned: `settings_get` and
`settings_set` in `src/settings.h`, `race_cfg` and `cfg_set` in the race.
`settings_defaults` [0x40c810] gives the defaults. All fields are s32
unless the table says otherwise.

| Offset | Field |
|---|---|
| 0x000 | version, 0x1e |
| 0x004 | 1 for two players (split screen) |
| 0x008 | race mode: 0 championship, 1 single race, 2 time trial, 3 pursue mode |
| 0x00c | difficulty, 0 to 3; 3 also mirrors the tracks (`g_mirror`) |
| 0x010 | 1: the ghost car in a time trial |
| 0x014 | 0 leaves out the moving objects, the collision objects and some flares; a championship sets it to 1 (`r_553084`) |
| 0x018 | unknown |
| 0x01c | 8 x s32: each driver's car model |
| 0x03c | unknown |
| 0x040, 0x044 | the car each player has chosen on the car pages |
| 0x048 | 9 x s32: each driver's gearbox, 1 automatic and 0 manual (`race_driver.f0c`); the race reads one for each driver |
| 0x06c | 8 x 15 bytes: the drivers' names; player 1's first |
| 0x0e4 | 15 bytes: player 2's name |
| 0x0f3 | the championship's race (0 to 6), or the track chosen |
| 0x0f7 | CD track: 0 the track's own, -1 random, else that track |
| 0x0fb | CD music volume, 0 to 10; 0 is off |
| 0x0ff | sound volume, 0 to 10 |
| 0x103 | SCREEN SIZE as the menu shows it |
| 0x107 | the view's size in the race, 3 - 0x103 |
| 0x10b | RESOLUTION: 0 320 x 200, 1 640 x 480, 2 800 x 600, 3 NATIVE (the port's) |
| 0x10f | the scene's perspective switch (`r_525e68`) |
| 0x113, 0x117 | unknown (`r_552e58`, `r_563be0`) |
| 0x11b, 0x11f | unknown |
| 0x123, 0x127 | each player's controller: 0 the keyboard, above 0 a joystick; what 1, 2 and 3 each mean is unknown |
| 0x12b, 0x12f | each player's `race_driver.f18`; unknown |
| 0x133 | 8 x s32: each driver's place on the grid |
| 0x153 | number of drivers |
| 0x157 | unknown |
| 0x15b | blurred texture levels on (`r_527f78`); the port does not make them |
| 0x15f | 8 x s32: who drives: 0 a player, 1 the computer, 2 the network |
| 0x17f | 8 bytes a player: player 1's keys, then player 2's at 0x187, as DirectInput scan codes: left, right, accelerate, brake, gear up, gear down, the action (boost) and look back |
| 0x18f, 0x193 | unknown |
| 0x197 | network mode; 2 is no network |
| 0x19b to 0x1ac | bytes; unknown (the defaults put `G`, `1` and `1`, `2` there) |
| 0x1bf | 7 x 0x78: the best times of each track, see below |
| 0x507 | 25 x s32: the item selected on each menu page |
| 0x56b | 0x14 bytes; unknown |
| 0x57f | the track shown on the best times page |
| 0x583 | the best times page: 0 race times, 1 laps |
| 0x587 | the highest difficulty won, 0 to 3; it unlocks cars and difficulty levels |
| 0x58b | 1 once the last championship is won at difficulty 3 |
| 0x58f | unknown; 0 |
| 0x593 | language, 0 to 5 |

Each track's best times, 0x78 bytes from 0x1bf + 0x78 x track:

| Offset | Size | Field |
|---|---|---|
| 0x00 | 5 x 4 | the initials of the five best race times |
| 0x14 | 5 x double | the five race times, in seconds |
| 0x3c | 5 x 4 | the initials of the five best laps |
| 0x50 | 5 x double | the five laps |

The race reads the track record at 0x1bf + 0x78 x track + 0x14 and the
lap record at + 0x50 (`lap_done`, `race_clear_laps` [0x41f8f0]).

## The disc image and CD music

The game plays its music from the disc's audio tracks. The port reads
them from a BIN/CUE disc image (`src/cdimage.c`, `src/cd.c`). It also
reads the data track's ISO 9660 file system, so the packaged builds can
copy the game files out of the image at the first start.

- The `.cue` sheet names the `.bin` files (`FILE ... BINARY`), the
  tracks and their sector layouts (`TRACK nn MODE1/2352` or `AUDIO`),
  and each track's start (`INDEX 01 mm:ss:ff`, 75 frames a second).
- Track 1 is the data track, 2352 byte sectors with the 2048 bytes of
  user data at 16. Tracks 2 to 8 are audio: 16 bit stereo samples at
  44.1 kHz.
- The image can be one `.bin` for all tracks or one for each track.
  Some images have no audio tracks; the game then plays no music.

`cd_read_tracks` [0x457da0] gives each track's start and length in
frames, as MCI did, and moves track 1's start to track 2's, so only
audio plays. `cd_play_track` [0x457ed0] plays a whole track through the
mixer. The race plays the track's CD track (see "The tracks"), or the
one setting 0x0f7 names; the front end plays setting 0x0f7's track, 7
for 0. `make extract` also writes each audio track to
`disc/audio/trackNN.wav`; the game does not read those.

## Files the port does not use

| File | What it is |
|---|---|
| `FONTS/IGNITION.FNT` | loaded, never used; layout unknown |
| `LEVELS/<T>/<T>.TAB` | loaded, never used by the port |
| `BALTAZAR/DATA/TEST2.PFM`, `DEFAULT2.PSQ` | a shorter menu film |
| `BALTAZAR/DATA/CONSPR.PIC`, `DEMOCAR.PIC`, `DEMOTRK.PIC`, `MARK.PIC` | pictures |
| `LEVELS/USA/USA0.PIC` to `USA2.PIC` | three other 320 x 200 pictures with `USA`'s palette |
| `FLAGGOR.PIC`, `SMALL.LFT` in the root | other versions of the files in `BALTAZAR/DATA/` and `FONTS/` |
| `FONTS/RESULT.LFT` | a font |
| `CARS/TEST.AIS` | AI settings as text |
| `INSTALL.PIC`, `INSTALL.TAB`, `VIELOGO.TGA` | the installer's; `tools/vita_sce_sys.py` reads `INSTALL.PIC` |
