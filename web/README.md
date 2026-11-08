# TRIGDECK on the web

The whole TRIGDECK instrument in a browser: the same engine as the Mac app (`Sources/TrigCore`, C, compiled to
WebAssembly, running in an AudioWorklet), the same controller logic (ported from `Sources/DDJMapping` with its tests)
and the same drawing (ported from the Mac app's views): the deck, the mixer, the strip, the stage, the pages, the sound
packs. A DDJ-400 or DDJ-FLX4 plays it over Web MIDI; the mouse and the keyboard play it too. It runs in Firefox, Chrome
and Edge on macOS, Windows and Linux (Safari has no Web MIDI: the mouse and keys still work there).

## Run it

    brew install llvm lld wasi-libc wasi-runtimes   # once: the WebAssembly toolchain (macOS)
    sh web/build.sh                                  # web/trigcore.wasm (~180 KB)
    sh web/serve.sh                                  # then open http://localhost:8642/web/
                                                     # (another port: PORT=9000 sh web/serve.sh)

Press START (browsers start audio only after a click), then allow MIDI when asked. **Firefox** asks once to add a small
"site permission" add-on for the page: that is how Firefox grants MIDI. If it never asks (it may not for localhost),
open `about:config`, set `dom.webmidi.gated` to `false` (and check `dom.webmidi.enabled` is `true`), reload and allow.
Chrome and Edge ask for MIDI with SysEx.

The project saves in the browser (every few seconds and when the page closes). The bar above the controller:
NEW PROJECT (the 16 default patterns), EXPORT / IMPORT (the project as a `trigdeck.project` file, the same format as
the Mac app's, so projects move between them), FORGET (the browser's copy).

## How it is built

- `trigcore.wasm`: the engine (`build.sh`); `td_web.c` adds what the page needs: rendering into a buffer, a snapshot
  of the state, parameter descriptions and formatting, the project as bytes.
- `worklet.js`: the engine on the audio thread; it takes engine calls from the page and posts snapshots ~30 times a
  second (the steps, locks and scenes only when the pattern changed).
- `src/engine.js`: the engine as the page sees it: the model the mapper reads (over the snapshots, plus a silent second
  instance for names and formatting), actions to engine calls, the project in `localStorage`.
- `src/mapping/`: the controller logic (decoder, mapper, LED writer), ported from Swift.
- `src/view/`: the drawing (deck, strip, stage, page pictures, keyboard), ported from AppKit to Canvas 2D.
- `src/main.js`: the app: events to the mapper and engine, the drawing ~20 times a second, Web MIDI (the FLX4's
  keep-alive, the not-receiving check), the DDJ's LEDs paced as in the Mac app.

## Tests

    node --test web/test/*.test.mjs                  # the logic (ported from the Swift tests) and the engine model
    node web/test_node.mjs                           # the WebAssembly engine on its own

`view-demo.html` shows the drawing with fake data (`?stage=map|scope|packs|figures`).

## Not in the web version

The SLOOP cartridge (the Mac app's FM-1 firmware), the screen reader support, and MIDI clock out.
