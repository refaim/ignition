# Experiment: dynamic GLES arena whole-FETCH uploads

`IGN_GLES_DYN_ARENA=1` enables the experiment. The default is `0`; scope is Android GLES only.

When enabled, one whole FETCH DYN pass is uploaded. Consecutive texture and table runs retain their existing offsets and remain unchanged. Ordering and physics are intended invariants. Other and non-FETCH paths remain unchanged. If staging allocation fails, the code falls back to existing behavior.

Initial real-entry runtime RED baseline: 3 legacy uploads versus 1 pass and wrong offsets. Final strengthened byte-payload, corrupt/missing-payload negative probes, and table A/B/A constant-texture cases pass.

Independent certification: 75/75 new-production executable lines and 30/30 branch outcomes, with gl3d 64/64 lines and 28/28 branches; rnd_gl 11/11 lines and 2/2 branches with PROF. This is not whole-engine coverage. TV image checks and actual millisecond measurement remain deferred, so no FPS claim is made.
