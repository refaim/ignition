# Reverse engineering notes

Facts read from `IGN_WIN.EXE`, the Windows 95 build of Ignition (UDS,
1997). Addresses are virtual addresses in that file. Port status per
routine is in [docs/PROGRESS.md](docs/PROGRESS.md).

## The binary

- `IGN_WIN.EXE`, 915968 bytes, MD5 `527bc475783319ecdd8adae1f97f6759`.
  The same file ships on all three discs in `data/` (Europe, Europe
  Rerelease, USA Rerelease).
- Win32 GUI, Intel 386, MSVC runtime (statically linked). The PDB path
  is `d:\projects\ignition\Ign_win.pdb`; the PDB is not on the disc.
- Built on the UDS "Lisa 2 Development System, Compilation 0.91.0"
  (`0x45b4f0`). Debug messages are in Swedish (`FEL VID LI_MOVEOBJECT`,
  "error in LI_MOVEOBJECT").
- The game logs `Ignition version %.2f initiated` with 6.53 (the double
  at `0x494978`).
- Imports DirectDraw, DirectInput, DirectSound, WINMM (MCI CD audio,
  joystick) and the IPX/TCP network game code.
- 1221 functions: about 490 are runtime library and imports that IDA
  names from signatures, 730 are game and Lisa code.
- `.text` ends below `0x47a000`; `.rdata` and `.data` follow.

## Tools

IDA Free 9.1 has only the cloud decompiler, and `decompile` fails with
`hx:GenPseudo` on this 32 bit binary. All work is from the disassembly.

- `tools/ida_dump.py` writes every function's disassembly to
  `.ida-mcp/asm/` through the ida-mcp HTTP endpoint.
- `tools/ida_index.py` builds `.ida-mcp/index.json` (callers, callees,
  imports and strings per function) and prints a summary of the game
  routines.
- `tools/sync_ida_names.py` pushes the C names back into IDA from the
  address comments in `src/`.
- `tools/progress.py` writes `docs/PROGRESS.md`. It leaves out the
  runtime library listed in `tools/library_funcs.txt` (IDA's signature
  matches, less the two false matches below).

IDA's signatures misname Lisa routines: `__cfltcvt_init` and
`__cfltcvt_init_0` fill the video driver tables, and the twelve
`operator delete` matches at `0x456bb0`..`0x456e50` are one line
wrappers that call through those tables (`video_set_palette`,
`video_flip` and others). All are renamed in the database and left out
of `tools/library_funcs.txt`.

## Start up and frame loop

| Address | Name | Role |
|---|---|---|
| `0x4120a0` | `_WinMain@16` | registers class "Ignition" (`CS_DBLCLKS`, `app_wndproc`), starts the clock, `lisa_init`, `app_init_devices`, message loop |
| `0x4122d0` | `app_wndproc` | `WM_ACTIVATEAPP` sets `g_app_active`; `WM_SETCURSOR` hides the cursor in full screen; `WM_KEYDOWN` is eaten; Alt+Enter rebuilds video and input for the other mode; `WM_DESTROY` shuts them down and quits |
| `0x412500` | `app_init_devices` | `video_select_driver(0)`, `video_create_window`, `input_init`, sets `g_app_ready` |
| `0x412230` | `app_tick` | state machine on `g_app_state`: 0 runs `game_init` once, 1 runs `game_frame`, 2 runs `game_shutdown` and `lisa_shutdown` and returns 0 |
| `0x412460` | `app_time_ms` | game clock in ms, from `QueryPerformanceCounter` or `timeGetTime`; advances only while the app is active |
| `0x412530` | | posts `WM_CLOSE` when `app_tick` returns 0 |

While the app is active the loop peeks for messages and runs `app_tick`
when none is waiting. While inactive it blocks in `GetMessageA`, so the
game freezes and the clock stops.

`game_init` (`0x417270`) sets `g_game_mode` (`0x639394`) to 2 (check
the CD), clears the dispatcher's state words, then runs
`game_video_init` (`0x417ea0`) and `game_cd_init` (`0x4184c0`).
`game_cd_init` reads the disc's track list and then sets the CD ready
and `g_game_mode` to 1 whatever it found, so the CD prompt
(`game_check_cd`, `0x4182d0`) does not show at start up.

`game_frame` (`0x4172b0`) dispatches on those words, in this order:

| Word | Address | Runs |
|---|---|---|
| `g_game_mode` 2 | `0x639394` | `game_check_cd`: CD prompt, Return retries, Escape quits |
| `g_game_mode` 1 | | `cd_stop` (`0x459e10`), `menu_init` (`0x4029a0`) |
| `g_menu_active` | `0x563c3c` | `menu_frame` (`0x402c00`); a result above 1 leaves the menu |
| `g_menu_leaving` | `0x563d9c` | `menu_exit` (`0x4038f0`) until done; then result 2 starts a race, 3 quits |
| `g_race_load_stage` 3, 2, 1 | `0x553290` | `0x418c40`, `0x418be0`, `0x418b70`: one load step a frame, 1 first |
| `g_race_running` | `0x525e5c` | the race, inline from `0x4173fb` |
| `g_race_ending` | `0x563bf4` | `race_end` (`0x420870`); then the race again (`0x527f70`) or the menu |

On frames where the menu is not running but `menu_exit` is, the
original reads the menu's result from an uninitialised local. The port
keeps the last result.

The text language is the word at `0x639933` (not aligned): 0 English,
1 German, 2 Italian, 3 and 4 English text. Localised strings are kept
100 bytes apart per language.

### Input

`input_poll` (`0x455ef0`) reads DirectInput's buffered keyboard events
into four tables by scan code: down (`0x50de60`), trigger with repeat
(`0x50df68`), pressed until cleared (`0x50e068`, which the menus read
through `0x4c50e0`) and held (`0x50e168`). It also keeps the last 32
typed characters at `0x50de18`.

`app_init_devices` and the Alt+Enter path call `input_init`
(`0x455ac0`) without its two arguments (repeat delay and period), so
the delay is the return address `0x41217a` and the period is the
address of `LoadIconA` left in `ebp`. Key repeat never fires in
practice.

`game_video_init` (`0x417ea0`):

1. Fills a 64 KB table at the first 64 KB boundary after `0x619330`
   with `table[row * 256 + col] = row`, and stores its address in
   `0x525e64` and `0x63f2d8`.
2. Makes a memory pool, `0x5530e8`.
3. Sets video mode 640 x 480 x 8, then 320 x 200 x 8, through the video
   function table (`0x456be0` jumps through `0x50eb70`). Either failure
   exits with "Cannot set graphics mode."
4. Opens the log to `Default.fil`, stdout off, and logs the version.
5. Loads `N_SYSGFX.PIC`, `N_SYSG_2.PIC`, `SYS.COL` and eight `.LFT`
   fonts (`game_load_system_gfx`, `0x418130`), makes five sprite
   objects (`sprite_create`, `0x456d40`), reads `MenuBkg.dat`
   (`menu_load_background`, `0x401000`).
6. Builds an 800 x 405 table at `0x553080` (`0x4200e0`), sets the menu
   mode (`game_set_menu_mode`), clears ten words at `0x54f920`.
7. Joystick caps (`0x4596a0`), opens CD audio (`0x458040`) at full
   volume (`0x457cf0`).

It also clears and then passes the static buffer `0x553470` to
`operator delete`, and deletes `SYS.COL`'s pool block 8 bytes in. The
port does neither.

The four sprites cut from `N_SYSGFX.PIC` use the rectangle at
`0x497bb8` (x 236, 20 x 20, one under the other); the fifth (61 x 58) is
at row 190, column 153.

## Lisa 2 library

| Area | Addresses | Notes |
|---|---|---|
| log | `0x45b4f0`..`0x45b5c0` | `Default.fil`; flags at `0x4bac88` (enabled, stdout, file) |
| memory pools | `0x45ad50`..`0x45b135` | 256 pools at `0x63c6a0`; see `src/lisa/mem.c` |
| files | `0x457420`..`0x4576e0` | error codes `0x7d0`..`0x802`, last error at `0x4bab34` |
| exit handlers | `0x45b1b0`..`0x45b410` | 200 slots at `0x5116e0`; run in two passes by priority (`0x10000`, then `0x20000`) at shut down; see `src/lisa/exit.c` |
| 2D target | `0x456cc0`, `0x456f60`, `0x457020` | buffer, pitch, size and clip; `src/lisa/gfx.c` |
| textures | `0x4611d0`..`0x461a60` | `obj_create` (`0x461360`) and `tex_get_info` (`0x4612e0`); see "Textures and sprites" |
| sprites | `0x45c7f0`..`0x45c8fe`, `0x456d20`..`0x457416`, `0x465bb5` | 2000 records at `0x514bd8`; `src/lisa/sprite.c` |
| video | `0x45b740`, `0x45bd70` | `DirectDrawCreate`, window, back buffers; function table at `0x50eb6c` |
| input | `0x455ac0`..`0x455e20` | `DirectInputCreateA`, keyboard device; key names `DIK 00`.. at `0x4931bc` |
| CD audio | `0x457b10`..`0x4581b0` | MCI: track starts and lengths in frames of 1/75 s, track 1 (data) skipped, volume only on or off; `src/cd.c` plays from the disc image |
| sound | `0x4577a0`..`0x459580` | banks of numbered group folders of numbered samples (`.WAV`, `.PAT` GF1PATCH110, `.RW8`, raw); voices of 0x24 bytes at `0x63caa4`, handles slot << 16 + sequence; the loudest five play (`0x467240`); `src/sound.c` |
| renderer | `0x45e610`.. | init copies a 16 byte default state to seven render slots |
| fonts | `0x456180`..`0x456a31` | 30 fonts of 0x640 bytes at `0x63f2e0`; `.LFT`, see "Data"; `src/lisa/font.c` |
| 3D file import | `0x461a90`, `0x461ea0` | reads `geo`, `tmp` and `3ds` text formats (tool code) |

## Video drivers

`video_select_driver` (`0x456af0`), with driver 0, runs two
registrations:

- `gfx_register_generic` (`0x456e60`) fills the table at `0x50eba0`:
  `0x50ebb0` set the draw target (`gfx_set_target`, `0x456f60`: buffer,
  pitch, width, height, bits), `0x50ebb4` set the clip rectangle
  (`0x457020`), `0x50ebbc` draw text into the target (`0x4571b0`),
  `0x50ebc0` create a sprite (`gfx_sprite_create`, `0x45c830`), and the
  object calls at `0x50ebd4`..`0x50ebe4`.
- `ddraw_register` (`0x45b690`) fills the table at `0x50eb68`:
  `0x50eb6c` create the window and DirectDraw (`0x45b740`), `0x50eb70`
  set a mode (`0x45bd70`), `0x50eb74` release (`0x45c060`), blits and
  flips at `0x50eb7c`..`0x50eb9c`.

The game asks for a mode through `g_video_mode` (`0x4ba6e0`: width,
height, bits, flag) and `video_set_mode` (`0x456be0`). Mode table for
the menu at `0x47dc30`: 320 x 200, 640 x 480, 800 x 600, indexed by
`0x4be730`, which the front end sets to 0. `game_set_menu_mode`
(`0x418500`) picks one of the three from `0x552fc0`.

A sprite is not a bitmap blit: `gfx_sprite_create` takes a descriptor
(width, height, pixels, stride and two more words) and makes a Lisa
object through `obj_create` (`0x461360`), the same object system the
race uses (`LI_PLACEOBJECT`, `LI_MOVEOBJECT`, `LI_HIDEOBJECT`). Text, in
contrast, is drawn by the CPU straight into the target buffer
(`0x456660` and `0x4564d0` through `0x50ebbc`).

### Textures and sprites

`obj_create` (`0x461360`) takes a 0x40 byte descriptor (name, width,
height, flags, pixels, stride, hot spot x and y in 24.8) and an id: id 0
makes a texture, a live id redefines it, and a NULL descriptor frees
it. It copies the pixels into 256 x 256 pages aligned to 64 KB
(`0x461530`), so a texel's address is page | v << 8 | u, the form the
rasteriser wants. The table of descriptors (`0x520380`: next free id,
capacity, slots) grows by 1000; ids start at 1.

A sprite (`gfx_sprite_create`, `0x45c830`) is a record of 12 bytes: a
texture id and the hot spot. Drawing (`sprite_draw`, `0x456d20` →
`0x4571b0` → `0x457370`) puts the hot spot at a 24.8 position.
Unrotated, `0x465bb5` clips to the target and writes
`table[src << 8 | dst]` for each pixel, through the 64 KB colour table
at `0x63f2d8`. `game_video_init` points that table at an identity table
(`table[s << 8 | d] = s`, a plain copy); the front end loads
`MENU.TAB` into it. With a scale and angle, `0x4571b0` builds the 16.16
matrix { c, s, s, c } and `0x468c70` draws rotated (not ported yet).

Text (`text_draw`, `0x456660`) draws each glyph as a sprite. Fixed
fonts advance by cell width plus spacing and centre narrow glyphs in the
cell; proportional fonts advance by glyph width plus spacing, and a
space is 0.35 of the cell width. Alignment: 0 left, 1 centred on x, 2
right aligned to x.

### The port's renderer

The port does not port the software rasteriser. Everything draws on
the GPU, keeping the 8 bit colour indices and the game's colour tables:

- `canvas.c` makes the game's 8 bit screen buffers (the menu's screen
  and background) GPU render targets at an internal resolution: the
  RESOLUTION option (setting 0x10b) gives 320 x 200, 640 x 480,
  800 x 600, or NATIVE, the window's size, which the port adds. Their
  logical size stays the original's. Sprites, text, panel blends, the
  intro, the background and the menu cars draw into them. A colour
  table is classed row by row (identity, one colour, or a blend); only
  blending rows read the pixel under them, from a copy of the area.
- `gl3d.c` draws meshes with the original's projection, a depth buffer
  instead of the depth buckets, and the screen space command lists of
  the menu background in order.
- The race's 3D draws at the internal resolution, as wide as the
  window's shape; the HUD's 320 x 200 layer shows over it where it is
  not colour 0.
- `video.c` and `gfx.c` show a surface through the palette, centred
  at 4:3, or the race's layers across the window.
- `--software` or Alt+R switches the 2D back to the original's CPU
  drawing into the 8 bit buffers, with the 3D read back into them.

## Game

| Area | Addresses | Notes |
|---|---|---|
| front end | `0x4029a0`..`0x40c520` | `menu.col`, `menu.tab`, `ign_win.btz`, car and track select, key setup, language select |
| menu car demo | `0x40ca60`..`0x410330` | `menucar.msh/.plc/.tex`, `default.psq`, "Script Player" |
| track list | `0x4198d0` | folder per track: ICELAND, CANADA, USA, CARIB, AUSTRIA, JAPAN, BRAZIL |
| level load | `0x418dd0`, `0x419a90`..`0x41af70` | `LEVELS\<t>\<t>.COL/.PAN/.PIC/.PLC/.MSH/.TEX/.SRF/.POS/.TAB/.SHD/.TRI` |
| world build | `0x41b470`, `0x41cd70`..`0x41d190` | objects, weather, cameras (at most 2), cars and wheels |
| sound per track | `0x41f9b0` | `SOUND\ROLL`, `SKID`, `COLL`, `BOOST`, `DIV`, `ENGINE.INF` |
| ghost car | `0x420240`, `0x4222b0` | `GHOSTS\<track>.GST` |
| AI | `0x4134e0`, `0x414e40` | `.TRI` track data, `CARS\TEST.AIS`, `ailists.txt` dump |
| race objects | `0x429a40`..`0x4357a0`, `0x43d540` | the `LI_MOVEOBJECT` handlers: collision, skid marks, sprites, flying parts, lightning, splashes, weather, animations, car, wheel, shadow |
| race HUD | `0x43ef30`, `0x440840` | lap times, positions, "WARNING, YOU'RE LAST!" |
| race end | `0x438a60`, `0x43c3c0` | results, scores, championship text |
| pause menu | `0x4383b0`, `0x438880` | continue, restart, quit, CD track |
| screenshot | `0x446040` | `IGN%d.TGA` |
| debug overlay | `0x43e9c0`, `0x43ec70` | FPS, speed, position |

## 3D engine

`0x4466d0`..`0x4516f0`. `src/scene.c` holds the scene, objects and
grid; `src/gl3d.c` draws them with OpenGL instead of the pipeline
stages and the rasteriser.

- **Scene record** (`0x63c5f0`, 0xa8 bytes, `struct scene`): camera
  position and angles as doubles, zoom (scales X only), stage switches
  at 0x38..0x54, target at 0x70, focal lengths 253 and 207 at 0x80,
  the eye's distance behind the screen (44) at 0x88, the centre at 0x9c.
  `0x4471e0` scales the focal lengths for another screen size.
- **Objects** (0x2a bytes) come from a pool made by `raster_set_grid`
  (`0x446ca0`) and are kept in lists per 256 unit cell of the ground.
  `0x446d90` creates, `0x446eb0` links, `0x446f30` moves, `0x447150`
  unlinks, `0x447070` changes the mesh.
- **Frame** (`0x4466d0`): look at the target (`0x448990`), gather the
  objects in the cells around the camera and cull them by sphere
  (`0x448e70`), transform and project (`0x449e70`), emit the faces as
  rasteriser commands into 6000 depth buckets (`0x44c1f0`), collect them
  back to front. There is no lighting and no near clip.
- **Projection**: `sx = cx - focal_x * zoom * Xc / (Zc + near)`,
  `sy = cy - focal_y * Yc / (Zc + near)`. The camera's rotation is
  Rz(-roll) Rx(-pitch) Ry(-yaw); an object's is Ry(-ay) Rx(ax) Rz(-az)
  with the mesh's Y flipped.
- **Faces**: the low byte of a face's flags picks its handler
  (`0x49c8e0`); the high word is a material id. 17 and 21 are textured,
  18 and 22 textured with colour 0 left out, 19 and 23 textured through
  the level's .SHD, 15 and 16 flat, 20 flat through the .SHD, 7 and 8
  sprites, 1 and 2 points, 11 to 13 lines. A handler keeps going while
  the next face has its type.
- **Rasteriser** (`0x44f0e9`): takes a list of commands, all triangles
  but sprites (split into two triangles of type 18). Screen x, y are
  24.8; u, v are 8.8 texels in a 256 x 256 page, u the column. 17, 18
  and 22 are affine, 20 and 21 perspective correct; 15 and 16 look up
  `TAB[colour << 8 | shade]`; 18, 19 and 21 look up `T[a << 8 | dst]`
  with the pixel under them. No z buffer.

## Front end

The menus are data driven. `0x404a60` builds the tables (C++ static
initialisers, guarded by bits of `0x4bfb18`..`0x4bfb2c`; run by
`tools/x86_stores.py`), loads the settings, fills the menu context at
`0x4c50d0` and starts the engine (`0x410930`). The engine runs from
`0x4109d0` (frame), `0x410d80` (navigation), `0x411640` (enter or leave
a page), `0x411960` (draw a visual), `0x411b40` (make an item's sprite).

There are six copies of the tables, one per language, 0x2f68 bytes
apart from `0x480990`. `0x4beb20`..`0x4beb34` point at each language's
page list (30 slots, unused ones 0). The table at `0x4be768` holds 25
pages of language 0, for copying each page's selection into the
settings.

| Record | Size | Fields |
|---|---|---|
| page | 0x18 | handler A (`0x409fb0` for all), handler B (`0x409610`), page data (`0x47dd70` for all), item count, items, selected item |
| item | 0x1c, packed | value pointer (into the settings), min, max, visuals, +0x10, +0x14 (s16), choice (+0x16, pointer), kind (+0x1a, s16; 1 everywhere) |
| visual | 0x18, packed | pixels, x, y, width, height, stride (s16 each), text, text x, text y, font index (s16 each) |
| choice | 0x18 | key bindings: +4 points at a key code byte in the settings, +0x14 the kind (2 a text field, 3 a key) |

An item has 2 + 2 x (max - min) visuals; `0x411b40` picks one by the
value and whether the item is selected, with a second set at +0x30.
Visual pixels are NULL in the data; the menu loader (`0x403e80`) points
them into `BILAR.PIC` and the other menu pictures. Positions are
relative to the screen centre: the engine adds half the screen width
(`0x4c50fc` is 1). With `0x4c5100` set, positions are in a 3200 x 2400
space scaled to the screen.

The navigation tree (`0x4909e0`, 25 entries of 0x40 bytes) is a list
of paths: (item index, code) pairs by depth, ended by -1. A code of 0
or more is a page index into the language's page list; a negative code
c calls `callbacks[-c - 1]` from `0x47df70` (`0x4045b0`, `0x4045e0`,
`0x404610`, `0x404740`, `0x404790`). The page stack is at `0x4c5144`,
the selection stack at `0x4c4e88`, the depth at `0x4c529c`.

The settings, saved as `ign_win.btz`, are 0x597 bytes at `0x4bf580`;
the file is read only when its first word is 0x1e. `settings_defaults`
(`0x40c810`) is checked byte for byte against the original routine run
by `tools/x86_stores.py` (`make test`).

### Frames and pages

`menu_frame` (`0x402c00`) steps the front end's clocks in ticks of
1 / 0.036 ms: joystick moves become key presses, the intro plays its
six `.CDP` animations (`ign1.cdp`, `ign2.cdp`, `ign3_0..3.cdp`) from 28.8
to 1278 ticks with palette fades, and timers count at 27 a tick. After
the intro, and after about 1080 idle ticks, the idle clock (`0x4be598`)
runs the attract mode: the credits, a title and up to three names a
step of 168 ticks, from the table at `0x47e4e0`. Any key leaves it and
is taken.

Handler A (`0x409fb0`) draws: the 3D background (`0x40c740`, a script
player over `menucar.msh`), the logo, the idle panels, the chosen car
and track pictures, the language flags, the 3D car carousel
(`0x40c5a0`), and the page layer blended through `MENU.TAB`. The page
layer (`0x40b820`) is a panel round every item's text, made of
`MenuBkg.dat` tiles (`0x4011c0`), then the item texts (`0x410af0`), and
the dialogs.

Handler B (`0x409610`) takes each key first: it skips the intro, plays
menu sounds, switches the language's tables on the language page and
builds the list of cars won so far, refuses keys bound twice, and
checks seven cheat codes typed anywhere: BANARNE, SURMULE (all
difficulty levels), SLASKTRATT (all eleven cars), FILMJOLK, SKUNK,
SVINPOLE, STRINGS. The language page skips its second language (no
flag; settings value 1).

## Data

`data/<image>/disc/` holds the files extracted from each image by
`make extract`, and `disc/audio/trackNN.wav` the CD audio (tracks 2 to
8 on the two multi-track discs; the Europe Rerelease has none).

### .PIC

`src/pic.h`. A 0x4e byte header (u32 file size, u16 `0x9500`, u16
width, u16 height, then a position and 8 for the bits per pixel at
0x12), a 768 byte palette of 8 bit RGB at 0x4e, and width x height
pixels from 0x34e. Every `.PIC` on the disc matches its header; the
game reads pixels from 0x34e.

### .COL

776 bytes: u32 `0x308` (the file size), u32 `0xb123`, then 256 RGB
entries of 8 bits. The menu reads the palette with
`file_read_at(.., 0x300, 8)`.

### .LFT

`src/lisa/font.h`. `"LFT\0"`, u16 version 100, u16 glyph count, u16
cell width, u16 height, then 224 s32 offsets for characters 32..255
(-1 for none) at 0xc, a 768 byte palette at 0x38c, 224 s16 widths at
0x68c, and glyph pixels from 0x84c. The shipped fonts stop at `j`
(106).

### .CDP

`src/cdp.h`. The intro animations. `"CDP\0"`, u16 version 100, u16
frames, u16 loop, u16 width, u16 height, a 768 byte palette at 0x10,
and frames from 0x310. Each frame is a byte code drawn over the last
(`0x499abc`): bytes up to 245 are pixels, 246 takes the next byte as
a pixel, 247 to 250 skip 2 to 5 pixels, 251 and 252 skip a u8 or u16
count, 253 is a run with a u8 count (0 is 256), 254 a run with a u16
count, and 255 ends the frame. IGN1 and IGN2 hold two frames fewer
than their headers give.

### Sound samples

`src/sound.c`. A bank folder holds numbered group folders, and a group
folder numbered samples: `.WAV` (PCM mono), `.PAT` (a Gravis GF1
patch, its first wave), `.XI` (a FastTracker 2 instrument, its first
sample, delta coded, at 22050 Hz) and `.RW8` (signed 8 bit at
22050 Hz). The race loads the level `SOUND` folders and
`GENERAL\SOUND\ROLL`, `SKID` and `COLL` as groups.

### Menu car files

- `MENUCAR.PLC`: s32 count (11), then count entries of 5 s32: the mesh's
  offset in dwords into the `.MSH`, the car id, and x, y, z.
- `.MSH`: meshes, each s32 vertex count, s32 face count, the vertices
  as 3 s32, then 44 byte faces: u32 flags (`0x00110011` on the menu
  cars), 3 s32 vertex indices, 3 pairs of s32 u, v in 8.8 texels, and
  a s32 whose high word is the texture page.
- `.TEX`: 256 x 256 pages of 8 bit texels from the start, no header;
  some files end with zero padding or a part page. Loaded at a 64 KB
  boundary. See docs/formats.md.

### Menu background files

- `DEFAULT.PSQ`: 12 byte entries `{ s32 first frame, s32 frames, 0 }`,
  played in turn.
- `TEST.PFM`: chunks `{ u32 tag, s32 size, data }`. `li01` a frame's
  vertices (s16 x, y, 12.4 pixels), then `li00` its script (`{ u8 type,
  u8 count, commands }` to a type of 0xff); one `li02` of textured face
  records (6 u16 8.8 u, v and s32 page) and one `li03` of sprites (s16
  hot spot, 4 u16 texel rectangle, u16 page, u16 table); `lixx` ends.
  Commands: 7 sprite, 11 line, 13 skipped, 15 flat, 17 textured, 18
  textured through a table, 19 flat through a table.
- The pages are `TEXTURES\*.TEX` read one after another; table 0 is
  `levels\canada\canada.SHD`, table 1 leaves colour 0 out.

### Track files

`LEVELS\<t>\<t>.*`, read by `0x418dd0` and `0x419a90`..`0x419d10`:

- `.COL` palette; `.PAN` the HUD's blend table; `.SHD` the
  translucency table; `.TAB` colour x shade (columns 64 up give the
  colour back); `.PIC` the loading picture.
- `.PLC`: s32 count, then `{ s32 mesh offset in dwords, u32 type, x, y,
  z }`, x and z placed at +0x6400. Types below 100 are scenery,
  150..199 collision objects, 300..349 moving, 350..359 lens flares.
- `.MSH`: as the menu car's; one mesh per placement.
- `.POS`: s32 per placement (-1 for none, else a dword offset from the
  entry) to `{ s32 frames, s32 current, frames x { x, y, z, rx, ry, rz } }`.
- `.TRI`: s32, then 500 bytes per placement: the AI's road sections.
- `.SRF`: the collision grid: a 0x24 byte header (extents, cell size
  512, 101 x 101 cells), cells `{ u32 offset, u32 offset, u16 count,
  u16 count }`, 24 byte spans `{ s32 x, z, s32 slopes x2 (16.16), s16
  dz, s16 face, u32 object }` and two lists of span offsets.
- Texture slots: 0 the track, 1 `CARS`, 2..7 `DIVSPR`, `LIGHT`,
  `SMOKE`, `DARKSMOK`, `BOOM`, `EXSMOKE`, 8 a blank page.

### Other files

File types: `.PIC` pictures, `.COL` palettes, `.TAB` colour tables,
`.LFT` and `.FNT` fonts, `.MSH` meshes, `.PLC` placements, `.TEX`
textures, `.SRF` surfaces, `.TRI` AI track lines, `.POS`, `.SHD`, `.PAN`,
`.GST` ghost laps, `.CDP` (`IGN1.CDP`..`IGN3_3.CDP`, loaded by the front
end), `.PSQ` and `.PFM` (menu car script).
