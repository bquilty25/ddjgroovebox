# TRIGDECK on the web (first version)

The same engine as the Mac app (`Sources/TrigCore`, C, unchanged but for a no-op lock) compiled to WebAssembly and
run in an AudioWorklet; a DDJ-400 or DDJ-FLX4 over Web MIDI; the tracks and patterns on a canvas. It runs in Chrome
and Edge on macOS, Windows, Linux and ChromeOS (Safari has no Web MIDI; Firefox needs a site-permission add-on).

## Run it

    brew install llvm lld wasi-libc wasi-runtimes   # once: the WebAssembly toolchain
    sh web/build.sh                                  # web/trigcore.wasm (~120 KB)
    sh web/serve.sh                                  # then open http://localhost:8000/web/ in Chrome or Edge

Press START (browsers start audio only after a click). Allow MIDI (with SysEx) when asked.

## What it does so far

- The engine: all 16 patterns with their tempos, the sounds, beat FX, scenes.
- The controller: PLAY; pads in HOT CUE (the active track's steps), BEAT JUMP (top row: tracks; bottom row: mutes) and
  SAMPLER (the 16 patterns); LOAD (previous / next pattern); channel faders, crossfader, left TEMPO (soft takeover);
  the jogs' sides (tracks); left CUE/LOOP CALL (track); right CUE/LOOP CALL (step page; SHIFT: a bar shorter /
  longer); left BEAT SYNC (FILL); BEAT FX (select, division, channel, level, on / off). LEDs for PLAY, the pads and
  BEAT FX, paced (24 a frame, one message per send). A DDJ-FLX4 gets its keep-alive every 200 ms.
- The screen: the 8 tracks over their whole length (the page on the pads framed), the playheads, levels, the 16
  patterns. Mouse: click a step, a track, a pattern. Keys: space (play), arrows (pattern, track).

## Not yet

The drawn controller, the knob pages (TRIG, SRC, FLTR, AMP, FX, LFO, MASTER), parameter locks, scenes from the
controller, sound packs, saving a project (browser storage). The Mac app's mapping (`Sources/DDJMapping`, Swift) is the
model for porting these.

`node web/test_node.mjs` checks the WebAssembly engine without a browser.
