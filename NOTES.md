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

IDA's signatures misname two Lisa routines as `__cfltcvt_init` and
`__cfltcvt_init_0`. They fill the video driver tables; see "Video
drivers". They are renamed in the database.

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

`game_init` (`0x417270`) sets `g_game_mode` (`0x639394`) to 2, clears six
unidentified globals, then runs `game_video_init` (`0x417ea0`) and
`game_cd_init` (`0x4184c0`, which sets `g_game_mode` to 1).

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
6. Joystick caps (`0x4596a0`), opens CD audio (`0x458040`).

## Lisa 2 library

| Area | Addresses | Notes |
|---|---|---|
| log | `0x45b4f0`..`0x45b5c0` | `Default.fil`; flags at `0x4bac88` (enabled, stdout, file) |
| memory pools | `0x45ad50`..`0x45b135` | 256 pools at `0x63c6a0`; see `src/lisa/mem.c` |
| files | `0x457420`..`0x4576e0` | error codes `0x7d0`..`0x802`, last error at `0x4bab34` |
| tasks | `0x45b1f0`, `0x45b240` | 200 entry table at `0x5116e0` with callbacks at `0x511a00` |
| video | `0x45b740`, `0x45bd70` | `DirectDrawCreate`, window, back buffers; function table at `0x50eb6c` |
| input | `0x455ac0`..`0x455e20` | `DirectInputCreateA`, keyboard device; key names `DIK 00`.. at `0x4931bc` |
| CD audio | `0x457b10`..`0x4580a0` | MCI strings: `open cdaudio`, `play cdaudio from .. to ..` |
| sound | `0x4582b0`..`0x459140` | loads `.WAV`, `.PAT` (GF1PATCH110) and `.RW8` from a folder |
| renderer | `0x45e610`.. | init copies a 16 byte default state to seven render slots |
| fonts | `0x456180` | `fontExit()`; `.LFT` and `.FNT` files |
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

### The port's renderer

The port does not reimplement the software rasteriser or the low
resolution modes as output. It keeps the Lisa object API and draws the
objects with OpenGL:

- 2D screens that the game draws into a buffer (the menu's 320 x 200
  surface, text) stay 8 bit surfaces, shown through a palette shader,
  centred at their original 4:3 aspect in a wide window (`video.c`,
  `gfx_gl.c`).
- Lisa objects (sprites, meshes, particles) become OpenGL draws, so 3D
  scenes render at the window's full size and aspect.

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

### Other files

File types: `.PIC` pictures, `.COL` palettes, `.TAB` colour tables,
`.LFT` and `.FNT` fonts, `.MSH` meshes, `.PLC` placements, `.TEX`
textures, `.SRF` surfaces, `.TRI` AI track lines, `.POS`, `.SHD`, `.PAN`,
`.GST` ghost laps, `.CDP` (`IGN1.CDP`..`IGN3_3.CDP`, loaded by the front
end), `.PSQ` and `.PFM` (menu car script).
