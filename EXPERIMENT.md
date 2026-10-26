# Experiment 6: discard the final 3D view's depth

## Flag and scope

`IGN_GLES_DISCARD_DEPTH=1`, opt-in and default off. The feature is compiled
in only for `IGN_GLES` builds that also define `__ANDROID__`; every other
host, every other backend, and a build without the flag keep the baseline
behavior (the depth buffer survives until the view's next clear).

Production files touched: `src/gl3d.h`, `src/gl3d.c`, `src/rnd.h`,
`src/rnd_gl.c`, `src/race/world.c`. Tests/evidence: `tests/test_discard_depth.c`,
`tests/test_rnd_discard.c`, `tests/fake_gles/GLES3/gl3.h`, `.experiment-evidence/`.

## Behavior

`race_draw_view` is the final renderer-lifetime caller. It already receives
`first`; the frame's split state is `g_two_players == 1 && r_552f58 == 0`
(the same condition `race/view.c` uses for `split`). Therefore:

* single view (`first=1`, no split): final;
* split screen, first half (`first=1`, split): NOT final, depth is kept for
  the resume;
* split screen, second half (`first=0`): final;
* two players but the screen returned to one view (`r_552f58=1`): final.

`gl3d_view_end(final)` is still called at the end of each `race_draw_view`.
On a final view, while the view FBO is bound and after the last 3D draw, it
calls `rnd_discard_depth(view_target)`. `rnd_gl.c` emits
`glInvalidateFramebuffer(GL_FRAMEBUFFER, 1, {GL_DEPTH_ATTACHMENT})` only when
the target is nonzero and is the currently bound FBO. The 2D HUD then binds
away. Nothing is invalidated after the first split half, on the resume, for
the screen/default FBO, or for a target without depth.

## Invariants

* Colour attachment is not in the invalidate list; the view texture stays
  readable for the present/layer path.
* Only `GL_DEPTH_ATTACHMENT` is invalidated, one attachment per final view.
* The view target is still around RG8 colour + depth (no depth16, no format
  or sampling change); clears, scissor, cull, offsets, tables, admission
  caps and draw order are untouched.
* Feature off / non-Android / non-GLES: no new GL call, baseline behavior.
* No gameplay/physics/clock change; `race/view.c` itself is not modified.

## Evidence

* RED (baseline, corrected fixture, runtime through `race_draw_view`):
  `.experiment-evidence/runtime-red.log` - `events=3 draws=1 discards=0`,
  failures are the missing invalidate. The earlier `rnd_bind(900)` fixture
  error and the MinGW link diagnosis are not product RED.
* GREEN: `.experiment-evidence/runtime-final_on.log` - `ok`.
* Variant runs, all link 0 / run 0 / `ok`:
  `runtime-final_off_flag.log` (flag 0),
  `runtime-final_non_android.log` (GLES, flag 1, no `__ANDROID__`),
  `runtime-final_non_gles.log` (no IGN_GLES),
  `runtime-final_prof_on.log` (`IGN_PROF=1`).
* Real `rnd_gl.c` backend oracle: `.experiment-evidence/runtime-rnd-discard.log`
  - only the bound nonzero target is invalidated, with exactly
  `GL_FRAMEBUFFER` / one attachment / `GL_DEPTH_ATTACHMENT` (0x8d00).

## Coverage (changed production lines only)

`.experiment-evidence/coverage-map.txt` has the annotated gcov excerpts.
Measured with GCC 16.2, `-O0 -g --coverage -ffunction-sections
-fdata-sections`, linked with `-Wl,--gc-sections`; host-only `.pdata`/`.xdata`
stripped copies used for the MinGW link, originals untouched, no NDK/build
flags changed. Tests are excluded from the percentages.

* `src/race/world.c:568` `final = !(g_two_players == 1 && r_552f58 == 0)
  || !first;` - 6/6 branch outcomes taken (3/3, 2/1, 1/1); line 570
  `gl3d_view_end(final)` executed 6 times (feature on) and 6 (off).
* `src/gl3d.c:630-636` - `gl3d_view_end` executed 7 times; line 635 has
  4/4 branch outcomes (`final` true/false, target nonzero/zero); line 636
  executed 5 times. Feature-off preprocessing leaves only an empty function
  (line 639 is non-executable), so the old host behavior is unchanged.
* `src/rnd_gl.c:855-863` - `rnd_discard_depth` executed 4 times; line 860
  has 4/4 branch outcomes (target 0, non-bound, bound+call); the call to
  `glInvalidateFramebuffer` executed once with the expected arguments.
* `src/gl3d.h`, `src/rnd.h` add declarations/macros only; no executable
  lines. No other production line changed against HEAD.

These gcov runs exercise only the view path, so whole-file percentages are
low and are not an engine-wide coverage claim. The changed lines and branches
are all covered in both feature-on and feature-off builds; the PROF build
executes the same changed code.

## Build recipe (host test only)

```
gcc -std=c11 -O0 -ffunction-sections -fdata-sections -Isrc -Itests/fake_gles \
  -DIGN_GLES=1 -D__ANDROID__=1 -DIGN_GLES_DISCARD_DEPTH=1 -DEXPECT_DISCARDS=1 \
  -c tests/test_discard_depth.c -o test.o
... world.c, gl3d.c the same way ...
objcopy --remove-section=.pdata* --remove-section=.xdata* test.o test.nounwind.o
... same for world.o, gl3d.o (copies only) ...
gcc -Wl,--gc-sections test.nounwind.o world.nounwind.o gl3d.nounwind.o -lm -o test.exe
```

`tests/test_rnd_discard.c` includes the real `src/rnd_gl.c` and records
`glInvalidateFramebuffer`. LLD execution was permission-denied and was not
retried or bypassed; `objcopy` on host object copies is what makes the MinGW
link work. No production testing `#ifdef`s or stubs were added.

## Final hashes (SHA-256)

Full list: `.experiment-evidence/source-hashes-final.txt`.

```
8e660a656772d87360189181361f87d483b30906f3ef01ff31bbde925c72d9f4  src/gl3d.c
99618fadc343388af620e72c5021ed180572a42a0ec0f2fa6153af02b9877399  src/gl3d.h
84bcde5c298fa60453a87991336f64a0fbaf621ffeab8b3587b426c59f77f8c3  src/rnd.h
a2ed2455c9c68f411092f540eb8d23b85c993399450a351a16e44e6dc3d627f3  src/rnd_gl.c
038de4dc761d085cb7d1ce9e4c215146ecba7254157ec4baf80442ea023bb350  src/race/world.c
79e2ac8b060dbef374daec29087e7cb5af9bf1a6dcfb76990ca8b43205cbe74e  tests/test_discard_depth.c
5324d96836d6402d1f7816016f938a89223b4b0e52085c1a646730f3bb7c200b  tests/test_rnd_discard.c
0b2e71ff7d8675069bc322af70aaeceffbcefa9c44dfb616dba3d5fcf1d26719  tests/fake_gles/GLES3/gl3.h
511a100eedfb9ea66441e5d506392dba2d8aa290f98c4c544c70ae75ec3c3396  VERSION
```

## Deferred

Real-device GLES behavior, pixel equality, frame-time gain and TV checks
belong to the parent. `race_debug_view` during split screen is treated as a
non-final view and simply keeps the baseline depth; this is a missed
optimization, not a behavior change. `race/view.c` is not modified, so its
orchestration is not linked into the host test; the final signal is derived
from the same split condition it uses and both branches are driven through
`race_draw_view`.
