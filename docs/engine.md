# How the engine works

This document explains how the port runs the game: the main loop, the
front end, how a race loads and runs, how a track is made, and how the
picture and sound come out. It points to the files and functions that
do each part. [formats.md](formats.md) describes the files the engine
reads; [PROGRESS.md](PROGRESS.md) lists every routine of `IGN_WIN.EXE`
and its status; [NOTES.md](../NOTES.md) holds the facts read from the
executable.

Function names are the port's. The address in brackets, such as
[0x4172b0], is the original routine that the function ports, as its
comment in the source gives it. Globals named `r_<address>` and fields
named `f<offset>` are not understood yet.

## The parts

| Part | Files | Role |
|---|---|---|
| host | `src/platform/*.c`, `*.m` | the window, keys, clock, audio device, and the loop that calls `app_tick` |
| shell | `src/app.c`, `src/main.c` | start up, the state machine, the game clock |
| game | `src/game.c`, `src/startup.c` | the phases: CD check, front end, race load, race, race end |
| front end | `src/frontend/`, `src/menu/` | the menus, intro, background film and 3D cars |
| race | `src/race/` | loading a track, the race's step and its picture |
| Lisa 2 | `src/lisa/` | the original's library: memory pools, files, fonts, sprites, textures, the 2D target |
| 3D | `src/scene.c`, `src/gl3d.c`, `src/srf.c`, `src/anim_tex.c` | the scene, objects and grid; drawing meshes; the ground under a point |
| software 3D | `src/soft3d/` | the original's pipeline and rasteriser, under `--soft3d` |
| sound | `src/sound.c`, `src/audio.c`, `src/cd.c`, `src/cdimage.c` | sample banks and voices, the mixer, CD music from the disc image |
| renderer | `src/canvas.c`, `src/gl3d.c`, `src/gfx.c`, `src/video.c`, `src/rnd_gl.c`, `src/rnd_gxm.c` | everything drawn on the GPU in colour indices |

## The main loop

`main` (`src/main.c`) sets the data folder and calls `app_startup`,
which starts the Lisa library and the devices (`app_init_devices`
[0x412500]). The host then calls `app_tick` [0x412230] once a frame
until it returns 0. `app_tick` is a state machine on `g_app_state`:

1. `APP_STATE_INIT` runs `game_init` [0x417270] once.
2. `APP_STATE_RUN` runs `game_frame` [0x4172b0] each frame.
3. `APP_STATE_SHUTDOWN` runs `game_shutdown` [0x417e90] and
   `lisa_shutdown`, and returns 0.

Each tick first sets `g_frame_time`, the game's clock in milliseconds
(`app_time_ms` [0x412460]). The clock stops while the window is not
active. The game counts time in ticks of 1/36 s: milliseconds times
0.036.

`game_init` runs `game_video_init` [0x417ea0] and `game_cd_init`
[0x4184c0]. `game_video_init` builds the identity colour table, makes
the game's memory pool, sets the video mode, opens the log, loads
`N_SYSGFX.PIC`, `N_SYSG_2.PIC`, `SYS.COL` and the eight menu fonts,
cuts the position icons and the bolt, reads `MENUBKG.DAT`, builds the
cone table for the iris, and opens the CD.

`game_frame` dispatches on a set of words, in this order, so that one
frame can pass from one phase to the next:

| Word | Runs |
|---|---|
| `g_game_mode` 2 | `game_check_cd` [0x4182d0]: "PLEASE INSERT THE IGNITION CD"; it does not show at start up |
| `g_game_mode` 1 | `menu_init` [0x4029a0]: set up the front end |
| `g_menu_active` | `menu_frame` [0x402c00]; a result of 2 starts a race, 3 quits |
| `g_menu_leaving` | `menu_exit` [0x4038f0] |
| `g_race_load_stage` 1, 2, 3 | `race_load_1` [0x418b70], `race_load_2` [0x418be0], `race_load_world` [0x418c40]: one stage a frame |
| `g_race_running` | `race_frame` [0x4173fb] |
| `g_race_ending` | `race_end` [0x420870]; then the race again (`g_race_again`), or back to the front end |

## The front end

The menus are data driven. `src/menu/data.c` holds the original's
tables, made from the executable by `tools/gen_menu_data.py`: six
languages, each with its pages. A page has a key map, a list of items
and two handlers. An item points at a value in the settings, with its
range and the visuals (a picture and a line of text) for each value.
`src/menu/menu.h` has the records.

`src/menu/engine.c` runs the pages (`menu_engine_frame` [0x4109d0]). It
keeps a stack of pages and of the item selected on each. Every frame it
takes the keys pressed, moves the selection or changes the selected
item's value, follows the navigation tree when an item is activated
(`menu_change_page` [0x411640]), and runs the page's handlers. A
negative code in the tree calls one of five callbacks: quit
(`menu_cb_4045b0`), start the race (`menu_cb_4045e0`), network game,
restore the settings, and reset the ghosts.

- **Handler A** (`menu_page_handler_a` [0x409fb0], `src/menu/pages.c`)
  draws the page each frame: the background film, the logo, the chosen
  car and track pictures, the flags, the 3D car (`car3d_draw`
  [0x40c5a0], `src/menu/car.c`), and the page's layer. The layer is a
  panel round each item's text (`src/menu/panels.c`, tiles from
  `MENUBKG.DAT`), then the texts, blended over the picture through
  `MENU.TAB`.
- **Handler B** (`menu_page_handler_b` [0x409610]) takes each key first.
  It skips the intro, plays the menu sounds, switches the language,
  refuses a key bound twice, and checks the cheat codes.

`menu_frame` steps the front end's clocks a tick at a time. The intro
plays the six `.CDP` films after the language page, with palette fades.
When the player is idle for a while, an attract mode shows the credits.
The background (`menu_background_render` [0x40c740], `src/menu/bg.c`)
is a film of screen space faces from `TEST.PFM`, played in the order of
`DEFAULT.PSQ`.

Before a race, `menu_apply_settings` [0x404800] turns the menu choices
into the race's setup in the settings: the mode, the cars on the grid,
the drivers' names and who drives them. The front end closes an iris,
writes `ign_win.btz` and returns 2. The race reads its setup
from that file, not from memory.

## Loading a race

A race loads in three stages, one a frame. Each stage moves the loading
bar on the track's loading screen (`race_progress` [0x418a60]).

Everything a race loads comes from one memory pool, `g_race_pool`
(`src/lisa/mem.c`). `race_end` [0x420870] frees the pool whole, then
flushes the GPU's copies of the race's meshes, textures and tables
(`gl3d_flush`, `canvas_forget_tables`), as the next race can reuse
their addresses.

**Stage 1**, `race_load_1` [0x418b70]:

- `read_settings` [0x419260] reads `ign_win.btz` into `g_race_cfg` and
  fans it out: the mode, the track (in a championship, from the
  difficulty's order of tracks), the drivers and their cars, keys and
  controls, the CD track and the volumes.
- `load_level_files` [0x418dd0] reads the level's `.COL`, `.PAN` and
  `.PIC` and shows the loading screen. Then it reads `.SHD`, `.TAB`, the
  level's and the cars' `.PLC` and `.MSH`, the nine texture slots
  (`load_textures` [0x419d10]), the `.POS`, the HUD's fonts and its
  pictures.

**Stage 2**, `race_load_2` [0x418be0]:

- `race_unpack_types` [0x41b360] splits each placement's type word.
- `race_track_order` [0x416250] finds the order of the road pieces.
- `race_make_tables` [0x41b400] builds the colour key table.
- `race_load_cars` [0x41d190] picks the computer's cars, finds each
  model's body and wheels in `CARS.PLC`, and puts each car on the grid.
- `race_init_cameras` [0x41e740] puts a chase camera behind each car.
- `race_load_sounds` [0x41f9b0] loads the sound banks and `ENGINE.INF`,
  and starts each car's engine and tyre loops.
- `race_make_world` [0x41b470] makes the scene and an object for each
  placement.

**Stage 3**, `race_load_world` [0x418c40]:

- `race_pos_deltas` [0x4356d0] turns the moving objects' paths into
  steps.
- `race_load_ai_track` [0x414e40] builds the racing line from the
  `.TRI`.
- `race_make_effects` [0x41cd70] makes 700 objects for particles, 200
  effect slots, and the weather.
- `race_hud_sprites` [0x41e990] cuts the HUD's sprites.
- `race_reset_state` [0x41fe60] clears the race's state.
- `race_animated_textures` [0x420090] sets up the animated textures.
- `race_load_ghost` [0x420240] loads the time trial's ghost.
- `race_srf_load` [0x412670] loads the `.SRF`.
- `race_init_hud` [0x43dea0] sets the race's video mode, the cameras'
  focal lengths and the views.
- The CD starts the race's track, and the lap clocks are cleared.

### Texture slots

The race's textures are read into one block, each file at a 64 KB
page, so a texel is page | v << 8 | u as the original's rasteriser
wants. The scene keeps a list of slots (`g_scene_textures`,
`src/scene.h`); an object names its slot and each face a page in it.

| Slot | Texture | Used by |
|---|---|---|
| 0 | the track's `.TEX` | the track's objects; the animated textures |
| 1 | `CARS.TEX` | cars and wheels |
| 2 to 7 | `DIVSPR`, `LIGHT`, `SMOKE`, `DARKSMOK`, `BOOM`, `EXSMOKE` | the effects and flares |
| 8 | a blank page | network players' names (not ported) |

`gl3d.c` uploads a slot's pages once (`texture_get`, `rnd_pages`). The
front end uses slot 0 for `MENUCAR.TEX`, and the background film has
its own 30 pages (`menubg_load_pages` [0x40cd60]).

## How a track is made

### The world and its grid

A track is a set of placements (`.PLC`): each a mesh of the `.MSH`
put at a place, with a type. The world is 0xc800 units square; a
placement stands at (x + 0x6400, y, z + 0x6400). World y is up, a mesh's
y points down, and angles are in tenths of a degree. The physics
counts a unit as 0.04596 m.

`race_make_world` [0x41b470] makes the scene (`scene_create`
[0x4468d0]) and a grid of 256 unit cells with room for 1500 objects
(`raster_set_grid` [0x446ca0]). It makes an object for each placement:
first those with a type below 100, then the rest. With setting 0x014
off it leaves out the moving objects, the collision objects and some
flares. An object (`struct scene_obj`, `src/scene.h`) holds its mesh,
its texture slot, its class (the placement's type), the road order of
its piece (`f20`) and its group. Linked objects sit in the list of the
cell they stand in (`scene_obj_link`, `scene_obj_move`).

From the placements' types, `race_make_world` also makes:

- the collision objects (150 to 199), which the cars knock
  (`make_collision_objects` [0x41bb1f]);
- the sound sources (400 to 498), each a looping sample of the track's
  bank heard by distance (`make_sound_sources` [0x41be37]);
- the lens flares and lights (350 to 359; `make_flares` [0x41bf2b]);
- the moving objects and trigger zones (300 to 349; `make_movers`
  [0x41c1aa]);
- each car's body, four wheels, shadow and, for car 7, an extra part,
  and the arrows over the players' cars (`make_car_objects`
  [0x41c29f]).

To draw a view, the scene gathers the objects of the cells ahead of the
camera (`scene_gather`). Two things narrow what it takes. The gather
radius limits the cells on some stretches of some tracks
(`gather_radius`, `src/race/view.c`). A placement's group (bits 12 to
15 of its type word) splits a track into parts: while a car is on a
piece of group g, the view draws only the objects of group 0 and of
group g (`group_at`).

### Road pieces and the track's order

The road is made of placements with types below 50: 0 the piece at the
start line, 1 the piece after it, 2 the road, 3 a fork, 4 and 5 the two
branches, 6 where they meet, and 7 pieces that join in pairs.
`race_track_order` [0x416250] moves each piece's origin to the middle
of its road faces. Then it walks from the start to the nearest piece
not yet visited, again and again. At a fork it walks the first branch
in the main list and the second beside it, until the branches meet.

The result is two tables (`src/race/race.h`):

- `g_track_links`: the segments in order, each a placement and its
  heading, and at a fork the second branch's placement and heading.
- `g_road_order`: for each placement, its segment number, negative on a
  second branch, and its heading.

Each object keeps its piece's segment number (`f20`). When the ground
under a car is a road face, its object says which segment the car is
on. The laps, the AI, the camera, the wrong way warning and the
respawn all work from that number (`c->f364`).

### Surfaces

The `.SRF` is a grid of 512 unit cells over the ground. Each cell lists
the spans of the faces over it, so `srf_lookup` [0x412fc0] finds the
faces under a point without searching the meshes. It takes the face
nearest the height asked for, and returns its surface, its triangle,
its normal and its object.

`race_contact` [0x424570] (`src/race/contact.c`) probes the ground
under the car's four corners each step:

- The corners' heights give the car's height, pitch and roll.
- The corners' surfaces give the grip (`g_surface_grip`), the tyres'
  sounds, the dust, spray and skid marks.
- A surface of 0x50 and up is a wall. The car then collides with the
  edges of the ground near it that no other face shares
  (`race_wall_edges` [0x428b90]).
- Off the road, the corners are probed again at half size.

The trigger zones are objects of classes 300 to 349. `race_zone_query`
[0x428730] finds the one under the car among the objects near it.

### The AI's racing line

The `.TRI` gives, for each road piece, two vertices of its mesh: the
road's edges across the piece. `race_load_ai_track` [0x414e40] makes a
node for each segment from them, or two where the record splits the
road into lanes or the track forks (`g_ai_lanes`, `src/race/ai.h`).

`race_ai` [0x4134e0] looks a few segments ahead for the farthest node
whose middle it can see, and steers for it. It sets its speed by the
bends and hills before it and by the surface. At a fork it draws a
chance to take the second branch. Each computer driver's manner comes
from `g_ai_params`, nine values a car, scaled by the difficulty. The
physics runs the drivers every other half tick; each time, one computer
driver plans and the others repeat their last controls (`drive`,
`src/race/phys.c`).

### Laps

A car counts quarters of the lap by its segment (`f378`): it must pass
a quarter, a half and three quarters of the segments in turn. A lap
ends when the car, with three quarters done, comes from the last
segment onto segment 0 (`race_contact`). `lap_done` keeps the lap's
time, checks the lap and track records against the settings, and after
the third lap marks the car finished (`f528`), except in pursue mode.
In pursue mode, `race_knockout` [0x423620] puts out the car that is a
lap behind the others.

`race_wrong_way` [0x43edf0] counts up while the car faces between 120
and 240 degrees off its segment's heading; past 25 the HUD shows WRONG
WAY.

### Putting a car back

A car is put back on the road when it falls below the track (y above
1800), when none of its corners is over the ground, when it stays on
off-track ground (surfaces 40 to 79) for 5 steps, when it lands on
surface 100 or 101, or when it drives into a trigger zone of type 300
or 304. A fall below the track puts it back at once. The other cases
first play an effect: the car blows up (effect 3), sinks in water (7)
or tumbles off the track (10). At the effect's end, `race_respawn`
[0x435910] puts the car two pieces ahead of where it was: at the middle
of the piece's drivable faces, 250 up, facing along the road, stopped
and in first gear.

### Moving objects and trigger zones

A placement with a path in the `.POS` moves one key a tick:
`race_animate_objects` [0x4357a0] adds the key's step to its place and
sets its angles. The paths run on the race's clock, apart from the
cars' step.

Zones of type 300 to 305 act on a car over them (`race_contact`): 300
and 304 put the car back, 301 rolls it, 302 bumps it into the air, 303
squashes it, and 305 rolls it when its moving object is near.

### Weather

`make_weather` [0x41cea0] sets the weather by the track: rain on
`CANADA` (0), snow on `AUSTRIA` (4), and on `USA` (1) only a looping
sound. Rain and snow are meshes of each view's, nine objects that tile
3 by 3 thousand units round the car.

`race_weather_zone` [0x434580] says whether the player's car is in the
stretch of segments where the weather falls. `race_weather` [0x434840]
adds drops (or flakes) while the car is in it, and takes them away
outside it. In rain the palette darkens with the drops
(`race_palette_tint`), and lightning sometimes strikes on the road
ahead. The weather's sound follows the drops. Once the players finish,
the sky clears.

### Animated textures

Water, signs and the like move by changing their faces' texture
coordinates (`src/anim_tex.c`). A placement of type 250 holds an
animation: its mesh's first textured faces are the key frames, each the
corner of a 64 x 64 texel cell of a page. `anim_tex_add` [0x401a50]
lists, under each key, every face of the track's objects that lies in
the key's cell. Each step, `anim_tex_step` [0x401f00] moves each list on
by one key: it adds the difference between the two keys' cells to the
faces' coordinates. `gl3d_mesh_changed` then rebuilds the changed meshes
on the GPU.

## The race's frame

`race_frame` [0x4173fb] runs each frame of the race. It asks
`race_frame_time` [0x420c00] how many ticks have passed. Under one tick,
nothing steps: the race runs at most 36 frames a second, the rate its
logic is made for. With a tick or more, `race_step` runs:

1. The countdown and the lap clocks (`race_countdown` [0x420d10]). The
   cars can drive once 200 ticks have passed since the race showed.
2. `race_physics` [0x422680] steps the cars by however many ticks
   passed, in halves of a tick (1/72 s).
3. For each car: its objects are put where it is (`race_place_car`
   [0x43d540]), its chase camera follows (`race_chase_camera`
   [0x43c910]), and the ground effects and sounds update. A car below
   the track is put back.
4. The ghost plays its next step.
5. Once for each whole tick: the car visuals, the hot keys, the
   players' controls (`race_f_420e60` [0x420e60]), the respawns and the
   effects (`race_effects_tick` [0x434190]).
6. The moving objects step on the race's clock.
7. `race_view` [0x436990] draws the picture.

At more than 10.8 ticks a frame, the step takes 10.8. Paused, the race
does not step: `race_frame` draws the view as it is under the pause
menu (`race_pause_menu` [0x4383b0]) or the Y/N box (`race_ask_box`
[0x438880]). Every 5 seconds the race starts the CD track again if it
has stopped.

## Physics and the car

A car has three records: `struct car` (`tools/structs/car.def`), the
race's view of it in world units; `struct car_phys`
(`tools/structs/car_phys.def`), its rigid body in metres and seconds;
and `struct race_driver`, who drives it. `tools/gen_structs.py` writes
them into `src/structs/` with an assert for each field's offset.

`race_physics` [0x422680] runs in half ticks. Each half tick:

1. Each car moves (`step_car`). It probes the ground (`race_contact`
   [0x424570]), its body steps (`phys_step` [0x40e6b0], `src/phys.c`),
   and it falls or lands (`race_car_fall` [0x423aa0]). In `phys_step`
   the surface's traction and grip limit the drive force and how much
   of the sideways speed is taken away. The yaw rate chases the
   steering's, or in a skid turns the car toward where it is going. In
   the air only the yaw and the speed carry on.
2. The race places are worked out, and in pursue mode the last car may
   be put out.
3. The cars collide with each other (`collide_pair`) and with the walls
   (`collide_walls`).
4. Each car's move is worked out (`update_motion`), and every other
   half tick the drivers run (`drive`).
5. In a time trial, the player's car is recorded for the ghost.

`race_control` (`src/race/control.c`) turns the inputs (0 to 100, and
-100 to 100 for steering) into the engine's force and revs, the brake,
the steering, the gears and the boost. `race_keyboard` [0x421bc0]
(`src/race/input.c`) reads the player's eight keys. The cars' numbers
(the mass, and the curves that `curve_build` [0x411dc0] samples) and
the AI's settings are tables of the executable, in `src/race/data.c`,
made by `tools/gen_race_data.py`.

## The camera

Each car has a chase camera (`struct race_cam`,
`tools/structs/race_cam.def`). `race_chase_camera` [0x43c910] keeps two
points half the camera's distance behind and ahead of the car, the
second for looking back. The camera's heading eases an eighth of the
way to the car's each step. Its height rises with the road's slope
(bits 16 to 23 of the piece's type word) and swings with the angle
between the camera and the road. Easing is slower once the race is on
than in the countdown.

`race_view` [0x436990] picks the camera:

- During the race, the chase camera of the car watched
  (`g_view_car`), or looking back.
- After the players finish, one of the track's three cameras over the
  finish (`g_finish_cams`, 3 a track), or a TV camera that follows the
  best placed car still racing (`tv_pick` [0x438050]). Tab changes it.
- A replay camera and a debug camera, for testing.

The projection is the original's (`src/gl3d.c`):
`sx = cx - focal_x * zoom * Xc / (Zc + near)`. `race_init_hud`
[0x43dea0] sets the focal lengths for the resolution. A zoom of -1
mirrors the picture for difficulty 3. With two players the screen
splits: the first player's view is the right half, the second's the
left.

## Effects

`src/race/fx.c` holds the effects: smoke, fire, sparks, flying parts,
lightning, splashes, skid marks, dust, spray and the boost's flames.
An effect is a request (`struct race_effect`) copied into one of 200
slots (`race_effect_start` [0x434380]). When all are taken, a request
replaces the slot of the lowest priority below its own. Each slot has a
scene object and a mesh that the effect rewrites every step, made of
points, lines, sprites and flat faces. Smoke, sparks and stars start
only near the cars being watched.

Longer sequences are effects too: a car that blows up and comes back
(3), sinks (7) or tumbles off the track (10), and the halo round a car
put back (11).

## HUD

`race_hud` [0x43ef30] (`src/race/hud.c`) draws each player's HUD into
the back buffer at the corners of the player's view: the times, the
place, the speed, the revs on a dial with a needle, the boost,
messages, the road's signs and WRONG WAY. It draws through the sprite
and font code, with the level's `.PAN` as the colour table. There are
three layouts, small, normal and high, each with its pictures and
fonts; `race_hud_layout` [0x41f3a0] picks one by the resolution, the
view's size and the split screen.
The needle and the boost bar are flat triangles that go through
`gl3d_raster`.

## Results and the championship

When a car finishes its third lap, `lap_done` marks it finished and
keeps its place. 120 ticks after the players have finished, the
camera moves to the finish and `race_results` [0x438a60]
(`src/race/results.c`) shows the results page by page. Return moves to
the next page (`race_hot_keys` [0x420eb0] moves the stage
`r_5530a8`):

1. the track's results;
2. the track's score;
3. the total score;
4. in a championship, the end: a statue on the podium (with CD track
   8) when the player is in the first three, or the offer to retry the
   race;
5. with two players, a final page.

At the race's end, `race_save_results` [0x4222b0] saves a ghost that
beat the old one. In a championship it adds the points of each place
(`g_place_points`: 10, 7, 5, 3, 2, 1), moves on to the next race, and
sets the next grid with the worst placed car in front. Then
`race_save_settings` [0x4225d0] writes `ign_win.btz`.

A championship is 5 races at difficulty 0, 6 at difficulty 1 and 7 at
difficulties 2 and 3, in the order `s_championship` gives
(`src/race/load.c`). The player must finish a race in the first three
to go on, and has 3 tries (`g_race_tries`). Winning a championship
raises the highest difficulty won (setting 0x587), which unlocks cars
and the next difficulty.

## Sound and CD music

`src/sound.c` is the original's sample library: banks of group folders
of numbered samples, and 64 voices. A voice is started from a request
(`sound_start` [0x4579b0]) and returns a handle; the game changes the
voice's record through it, and a rate of 0 stops it. `src/audio.c`
mixes on the host's audio thread. It plays the loudest voices, 5 in the
menus and 10 in the race, at their rate, volume and pan, with their
loops.

In the race (`src/race/sound.c`) a sound placed in the world is as loud
as 1 - distance / 1500 from the listener: the watched car, or the
camera over the finish once the players are done; with two players the
nearer of their cars (`race_sound_3d` [0x43e710]). Each car plays two
engine loops and two tyre loops all race. Each step sets their pitch
and loudness from the revs (through `ENGINE.INF`) and from the
surface.

The CD music comes from the disc image. `src/cdimage.c` reads the CUE
sheet and the BIN files, and `src/cd.c` answers the game's MCI calls
from them: the number of tracks, where each starts, and play a track.
`audio.c` mixes the track's samples with the sounds. The front end
plays the track the settings name; the race plays its track's own
(see formats.md, "The tracks").

## The renderer

The original draws everything in software into 8 bit buffers: the 2D
with sprites and colour tables, the 3D with its own rasteriser. The
port keeps the 8 bit picture, its palette and the colour tables, and
draws on the GPU. With `--soft3d` the 3D is drawn by the original's
pipeline and rasteriser, ported in `src/soft3d/`
([software3d.md](software3d.md)). Everything stays
colour indices until the screen is shown through the palette.

The layers, from the game down:

- **The game's 2D** draws through the Lisa library: a target buffer
  with a clip rectangle (`set_target`, `src/lisa/gfx.c`), sprites
  (`sprite_draw`, `src/lisa/sprite.c`) and text (`text_draw`,
  `src/lisa/font.c`). Each pixel becomes `table[src << 8 | dst]`,
  through the colour table in `g_gfx_color_table`.
- **`canvas.c`** makes the game's 8 bit buffers (the menu's screen and
  background, and the race's loading screen in NATIVE) GPU render
  targets at the internal resolution. Their logical size stays the
  original's. Sprites, text, blended rectangles and the 3D draw into
  them on the GPU. A colour table is classed row by row: identity, one
  colour, or a blend. Only the blending rows read the pixel under them,
  from a copy of the target. With `--software` (or Alt+R) the 2D is
  drawn on the CPU into the buffers instead, as the original does.
- **`gl3d.c`** draws the scene's objects. A mesh's textured and flat
  faces become triangles, cached on the GPU by the mesh's address. A
  depth buffer orders them, and faces are clipped at the near plane,
  which the original does not do. Points, lines, sprites and the faces
  that go through a table (types 19, 20, 23, and sprites 7 and 8) are
  drawn after the rest, far to near, each frame anew: they read the
  pixels under them. `gl3d_raster` draws the screen space faces of the
  menu background and the HUD's triangles.
- **`gfx.c` and `video.c`** show the screen. A frame is an R8 texture
  of indices shown through a 256 x 1 palette texture
  (`gfx_draw_screen`), at 4:3 in the middle of the window. In a race,
  the 3D view fills the window (`gfx_draw_layers`), and the back buffer,
  where the HUD and the messages are drawn on the CPU, shows over it
  where it is not colour 0.
- **`rnd.h`** is the set of GPU operations all of the above use:
  textures, render targets, six programs, and draws. `rnd_gl.c` does
  them on OpenGL 3.2 core or OpenGL ES 3.0; `rnd_gxm.c` on the Vita's
  GXM ([vita.md](vita.md) explains how). Render targets hold an index in
  R, and for the 3D in G whether a face covers the pixel.

RESOLUTION sets the internal resolution: 320 x 200, 640 x 480,
800 x 600, or NATIVE, the port's, which follows the window
(`canvas_set_resolution`). The race's 3D is drawn at the internal
resolution's height and as wide as the window's shape, so a wide window
sees more at the sides; in NATIVE that is the window's own size
(`race_draw_view`, `src/race/world.c`).
