# Experiment 4: split dynamic GLES3 shaders

`IGN_GLES_SPLIT_DYN=1` enables split dynamic shader programs; the default is `0`, and the path is Android GLES3 only. When enabled, ordinary dynamic draws keep reusing the existing ordinary GLSL source, while table-only draws use a coherent fetch shader. Draws keep the same VBO, VAO, and buffer storage as before, so any result is isolated from the separate streaming experiment. Each program owns independent uniform locations and relies on `use_table` values `0`, `1`, and `0`.

Plain compile or link failure falls back to the existing all-fetch program. Fetch-program failure keeps the pre-existing copy fallback. Palette, depth, precision, game logic, and physics behavior are unchanged.

The initial real entry point was RED: ordinary draws received the fetch shader before implementation. Final tests cover compile/link failure cleanup and reinit, missing uniforms, and a corrected per-owned-resource lifetime and double-delete oracle with recorded RED and GREEN runs. Independent review certified 55/55 changed production executable lines and 32/32 branch outcomes in `src/rnd_gl.c`, not the whole engine.

The parent's official glslang 16.6 compiled and linked the generated ordinary ES shader; the coherent fetch variant could not be validated there because the extension is unsupported. That is neither a product failure nor a pass. TV images, GPU behavior, and actual millisecond measurement are deferred, with no FPS claim.
