# Experiment 7: exact indexed DYN geometry

## Status

The runtime scope correction is RED then GREEN. The feature is opt-in,
active only on the framebuffer-fetch path, and the final line and branch
evidence is regenerated below from the corrected source.

## Flag and scope

- Build flag: `IGN_GLES_INDEXED_DYN=1`.
- Active only when `IGN_GLES`, `__ANDROID__` and the exact flag value `1` are
  all present. Undefined, `0`, non-Android, non-GLES and feature-off builds
  retain the existing array upload path.
- Even in an enabled build, an indexed upload is attempted only while
  `rnd_dyn_framebuffer_fetch()` reports the fetch path active at runtime.
  `draw_batch()` also serves the non-fetch dependency-layer path when
  `rnd_batch_copies() == 1`; that path keeps its original array stream and
  draw ordering, including one copy before each layer.
- Scope is the DYN framebuffer-fetch path in `src/gl3d.c`. Mesh, raster,
  Canvas, soft3d, other programs, metadata packing and coordinate precision
  are unchanged.

## Behaviour

- Each existing DYN draw run is built from its original vertex sequence.
- Complete 9-float vertex records are deduplicated only when all 36 bytes are
  identical. Comparisons use `memcmp`, so signed zero and NaN payload bits are
  not normalized.
- The first occurrence defines the reusable vertex and the original six
  indices per quad (or three per triangle) are retained in their original
  order. Reconstructed triangles therefore preserve provoking attributes.
- A run whose exact indexed payload is not smaller falls back to the existing
  array upload. Allocation failure, the 16-bit representation bound, or a
  rejected backend upload also falls back without changing draw semantics.
- A successful indexed run is uploaded once into the DYN program's existing
  generic GL buffer: unique vertex records first, then `uint16_t` indices at
  the float-aligned `vertex_count * 36` byte offset. The draw uses
  `GL_ELEMENT_ARRAY_BUFFER` pointing at that same buffer and unbinds the
  element association after the draw.
- Texture/table grouping, far-to-near order, depth, polygon offset, sampling,
  admission caps, profile draw counts and all other draw state stay on the
  original path.

## Memory and profiling

- CPU staging is reused and bounded by the largest run seen. No allocation is
  made per face. The local open-addressed table maps exact records only; there
  is no global sorting or hash framework.
- `PROF_UPLOAD_BYTES` now counts the actual combined indexed byte payload
  when the indexed path succeeds. It remains the exact vertex-byte count on
  the array fallback; its scope therefore includes index bytes for this path.

## Evidence

- Baseline commit: `ac4d1f993fa67d018fbb6fa7e081fbc14702c456`.
- Runtime scope RED: `.experiment-evidence/final/scope-red-build.log` and
  `.experiment-evidence/final/scope-red-run.log`. With fetch off and batch
  copies on, two overlapping table faces must stream two six-vertex arrays
  with copies before draws 1 and 2. The unguarded code instead emitted two
  indexed uploads/draws; twelve checks failed.
- Scope fix: the indexed block in `draw_batch()` is guarded by the active
  `rnd_dyn_framebuffer_fetch()` result, not only by the compile flag.
- The UINT16/index-block path was not redesigned and vertex attributes are
  still packed exactly as before.
- Existing cases preserve non-fetch with `rnd_batch_copies()==0`, fetch with
  the indexed feature on and off, and array fallback. The new runtime RED
  case covers non-fetch with `rnd_batch_copies()==1`.

## Coverage

Final builds were recompiled from the corrected source; the old root and
`.experiment-evidence/cov-*` gcov files are not used for these counts.

- Configurations: gl3d off/on/prof and rnd_gl off/fetch-on/indexed-on/
  indexed-prof. The build and gcov commands are in
  `.experiment-evidence/final/commands.txt`.
- Production changes relative to `ac4d1f9` are `src/gl3d.c`, `src/rnd_gl.c`
  and `src/rnd.h`; tests are excluded.
- Changed lines: 175 added in total. 88 are instrumented executable lines and
  all 88 were executed. `src/rnd.h`'s 18 added lines are preprocessor
  directives and declarations and have no executable outcomes.
- Combined unique branch outcomes on changed executable lines: 50/50.
  `src/gl3d.c` has 64/64 lines and 32/32 outcomes; `src/rnd_gl.c` has 24/24
  lines and 18/18 outcomes.
- The changed-line map is `.experiment-evidence/final/changed-lines.tsv`;
  the summary is `.experiment-evidence/final/coverage-summary.txt`.
- Every generated `.gcov` source section was compared line-for-line with the
  current actual source. The final report is
  `.experiment-evidence/final/gcov-source-exact.txt` (23 gcov files, 16770
  source lines checked, zero mismatched files). The checker is
  `.experiment-evidence/final/verify-gcov-source.ps1`.
- Final production objects compiled with `-Wall -Wextra -Werror` for all
  listed configurations are in `.experiment-evidence/final/obj/`.

Final source hashes:

- `src/gl3d.c`: `5E5EDB2A9AA6406B66B360912CC875B92DE76D3692284CDCF48B3A7B58264035`
- `src/rnd_gl.c`: `E8CD76CD126283ACFC4AF317DA95905A784645C7533FE03D5D229F69FC27AFC9`
- `src/rnd.h`: `762777551E3343B2FB9B9C7312FD7546DA74012F2395C6F52744DC1294AA68DB`

The final test matrix is host GCC/gcov only. No Android build, device, TV,
ADB or GPU validation was performed; that remains with the parent after the
common black-view fix. Version is `0.3.2`.
