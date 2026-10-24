# Ignition in the browser

The web build is the same engine in WebAssembly. The host is
`src/platform/web.c`, on Emscripten's own APIs: WebGL 2 on the page's
canvas, the keyboard, gamepad and touch events, and Web Audio.

You import the files of your own copy of the game into the browser's
storage once. The files are never uploaded.

## Build

You need [Emscripten](https://emscripten.org) 3.1.53 or later, with
`emcc` on `PATH`:

```sh
make web          # build-web/dist/
make web-serve    # serves it on http://localhost:8080
make web-clean
```

`build-web/dist/` is a static site:

| File | Purpose |
| --- | --- |
| `index.html` | The import page and the game's canvas |
| `ignition.js`, `ignition.wasm` | The engine |

The build uses no threads, so it needs no special HTTP headers. Any
static host works, GitHub Pages included.

## Import the game

Select **Import game folder…** and pick the root of the game CD, or a
copy of it: the folder that holds `BALTAZAR` and `LEVELS`. A folder that
holds those one level down, as `disc/`, also works.

The page copies the files the game reads into IndexedDB (about 40 MB).
It skips the installers, `DIRECTX/`, `.EXE` and `.DLL` files, and disc
images. **Remove game** deletes the copy. Settings and best times are
saved in the same storage.

## Limits

- No CD music: the music is on the CD's audio tracks, and the page does
  not import the disc image. Sound effects play.
- Browsers start audio only after a key press or a touch on the page.
- The import needs a browser that can pick a folder. Desktop Chrome,
  Edge, Firefox and Safari can; most phone browsers cannot.

## How it works

- **Main loop:** the game's loops run unchanged. The build uses
  Asyncify: after each frame the game waits for the browser's next
  animation frame, and a loop that only polls the keyboard gives the
  browser control every 16 ms.
- **Video:** WebGL 2 is OpenGL ES 3.0, which `src/rnd_gl.c` supports
  (`IGN_GLES`).
- **Audio:** a `ScriptProcessorNode` pulls the game's mixer.
- **Files:** IndexedDB is mounted at `/ignition`; the game runs from
  `/ignition/disc`.
- **Input:** keyboard, gamepads (the browser's standard mapping), and an
  on-screen pad on touch screens.
