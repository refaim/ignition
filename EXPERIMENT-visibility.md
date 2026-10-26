# Experiment 2: conservative GPU visibility rejection

This is an isolated A/B experiment, not an integration branch.

## Flag

`src/gl3d.c` compiles the rejection only when all of these are true:

```
IGN_GLES
__ANDROID__
IGN_GLES_VISIBILITY_CULL == 1
```

The default is off. Feature-off builds compile the old paths unchanged.
No other renderer, shader, scene, game, physics or transparent-ordering
code is changed.

## Behaviour

After the existing `collect()` admission has finished, `draw_dyns()`
compacts its admitted `MAX_DYN` set in place. A face is rejected only
when every vertex is strictly beyond one of the four side planes in
homogeneous clip space. Touching or crossing a side, crossing or lying
behind the eye, a non-positive `w`, NaN/Inf, and margins that overflow
all keep the face. Admission still happens first, so culling cannot make
room for a face that the original `MAX_DYN` limit had truncated.
Compaction preserves the original relative order, including equal-depth
ties.

Cached static meshes get an enclosing box of the `FLOATS8` vertex
positions actually uploaded by `mesh_get()`. `gl3d_mesh_changed()`
recomputes it after the matching in-place upload. `gl3d_draw()` tests all
eight box corners with the float matrix passed to the shader and the same
ambiguity-keep rules. Only the static mesh draw is skipped; deferred/table
faces are collected before this check and keep their admission order.
Dynamic and generated billboard geometry never use cached bounds.

The static-box test evaluates rows in double precision. It keeps the
box when any matrix or position input is non-finite, or when a nonzero
subnormal input could be flushed before a large position amplifies the
loss. The full overflow guard is `S + E <= FLT_MAX`, where `S` is the
sum of the absolute products and `E` is the relative error allowance
plus `8 * FLT_MIN`; merely checking `S <= FLT_MAX` is insufficient
because rounded intermediates can still overflow. The relative rounding
allowance covers four products and three additions, with room for an FMA
or a different association, and is augmented by eight `FLT_MIN` for FTZ
at those seven operations. A corner is used only when its positive `w`
exceeds its own error; a side rejects only when its distance is strictly
greater than the x-or-y error plus the `w` error. Uncertain cases are
kept.

This envelope assumes ordinary IEEE binary32 arithmetic with
round-to-nearest, permits an FMA and optional FTZ, and does not assume
arbitrary precision.

Whole-offscreen geometry already produces no visible fragments. The
expected benefit is avoided CPU sorting, vertex upload and GPU
clip/submission work, not a fill-rate or pixel-output change.

## RED / GREEN

Initial RED, against unchanged production (`gl3d.c` SHA-256
`997F1494081C4DF51C05CB4912A3C3EE179B89283FAEC36268088F046D5E0A75`),
through the real `draw_dyns()` and `gl3d_draw()` entry points:

```
FAIL tests/test_perf_visibility.c:23: s_draw_count == 0
FAIL tests/test_perf_visibility.c:76: s_mesh_draw_count == 0
test_perf_visibility: 2 failures
```

The initial test source (`905D6C260CF638F59ED561127EB97467089B08475355EEEDCDA17E663F058AB5`)
and production source are preserved under `.experiment-evidence/red/`,
with the run log in `.experiment-evidence/red_visibility.log`.

The widened GREEN suite covers four DYN clip sides, touching/straddling/
behind-eye/near-plane/NaN/Inf/overflow ambiguity, stable ties,
cold/warm mesh caches, mesh-change bound updates, mirrored/scaled
matrices, degenerate and empty bounds, deferred table faces under a
culled static body, copy fallback, and preservation of the original
`MAX_DYN` admission boundary. The same test is also compiled as the
feature-off control.

Phase 3 added two concrete RED cases before the production edit: finite
exact x `1e35` with `w = 1` whose separate float products overflow to
`+Inf`/`-Inf` and then NaN, and a subnormal coefficient whose exact x
`5.88e-31` is outside a tiny `w` but becomes zero after input FTZ. Against
the previous `src/gl3d.c` the new checks produced:

```
FAIL tests/test_perf_visibility.c:526: error == INFINITY
FAIL tests/test_perf_visibility.c:531: mesh_visible(&e, m) == 1
FAIL tests/test_perf_visibility.c:545: mesh_visible(&e, m) == 1
FAIL tests/test_perf_visibility.c:550: mesh_visible(&e, m) == 1
FAIL tests/test_perf_visibility.c:563: error == 8.0f * FLT_MIN
FAIL tests/test_perf_visibility.c:571: error >= 8.0f * FLT_MIN
FAIL tests/test_perf_visibility.c:579: error == INFINITY
test_perf_visibility: 7 failures
```

The RED and GREEN runs are
`.experiment-evidence/phase3_red.log` and
`.experiment-evidence/phase3_green.log`.

The final headroom regression (valid float/s32 inputs, `S < FLT_MAX`
but separately rounded products sum to infinity) produced three runtime
RED failures before the full overflow guard and a GREEN pass after it;
`phase4`-prefixed logs are under `.experiment-evidence/`, with the final
`run.log` in `.experiment-evidence/phase4-cov-final/`.

## Coverage

Fresh coverage against the full common baseline tree
`7f86a470396a0ab417e822b6a746565164ef2b09` covers every executable line
and branch outcome added by this experiment to `src/gl3d.c`, not only the
last phase and not the whole engine:

```
New/touched production executable lines: 92/92
New/touched production branch outcomes:  104/104
```

Test code is excluded. The exact line-by-line counts and branch outcomes
are in `.experiment-evidence/phase4-cov-final/gl3d.c.gcov`; the annotated
test `.gcov` files and final `run.log` are in the same directory.

## Current source identity

```
src/gl3d.c                    6A179C05556368C03287966B715E9415BFD65E042321950A691EE10E2FB298BA
tests/test_perf_visibility.c  BCC2981B7D5D07E1FD3860677F798B6D6CA67A2E55422C67C5C47A3C00930FEF
tests/test_gl3d_dyn_fetch.c   D706183B2BFAF41781C98C7E7930D05C1B78C747EADC17B7348E2C63DC01598B
VERSION                       0.3.2 (unchanged)
```

## Coverage method

Host GCC 16.2.0, `-O0 --coverage`, with
`-D__ANDROID__=1 -DIGN_GLES_VISIBILITY_CULL=1`, using
`tests/fake_gles` and the production-file inclusion style:

This is not whole-engine coverage. Test code is excluded from those
counts. The matching `.gcov` data, annotated test files and `run.log` are
under `.experiment-evidence/phase4-cov-final/`.

## Deferred checks

No TV, device, APK, SDK, GPU app, full-engine or NDK build was run. No
FPS or pixel-equivalence claim is made from the fake GL fixture. The
deferred real TV/device checks are:

1. Feature-on and feature-off images match for straddling, touching,
   near-plane and behind-eye geometry.
2. A wholly offscreen DYN or static mesh still causes no visible
   fragments, while frame CPU/GPU submission work drops as expected.
3. Reflections, rotated objects, mesh-change animations and deferred
   table faces keep their output and ordering.
