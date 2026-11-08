# DDJ Groovebox

A groovebox for the Mac played from a Pioneer DDJ-400. The window draws the controller, and every control on it shows
what it does and its value right now: the controller is the screen.

- **TRIGDECK** (the default) is a groovebox designed around the DDJ-400: 8 tracks in two decks, synthesised drum
  machines, a synth, FM and a sampler, an Elektron-style sequencer (trigs, parameter locks, conditions, micro timing,
  retrigs), scenes morphed by the crossfader, decks you scratch with the jogs while the pattern runs on underneath,
  beat FX for performance, resampling, Euclidean and generative tools, and an AUTOPILOT that arranges the pattern in
  8-bar phrases. It opens on a house / techno pattern, with breakdown, dub techno, acid and electro patterns next to
  it. See [docs/TRIGDECK.md](docs/TRIGDECK.md) for the guide and
  [docs/VISION.md](docs/VISION.md) for the design.
- **Cartridges** are M-VAVE FM-1 firmwares running unchanged on the Mac, played through an FM-1 panel mapping. The
  first is **SLOOP** by isod89 (Instrument > SLOOP, Cmd-2); see [docs/MAPPING.md](docs/MAPPING.md).

Without a DDJ-400, click the drawing or play the Mac keyboard (laid out like the controller, see the guide).

## Build and run

Needs macOS 14 or later and Xcode or the Command Line Tools (Swift 5.10+). The SLOOP cartridge also needs Python 3 with
Pillow (`pip3 install Pillow`) for SLOOP's header generators.

```sh
scripts/gen_headers.sh          # once: SLOOP's generated headers (vendor/sloop/build/gen)
swift run -c release            # run from the terminal
scripts/build_app.sh --open     # or build "build/DDJ Groovebox.app" and open it
```

Plug in the DDJ-400 before or after launching: it is found by name and reconnected when plugged in again.

## How it is built

| Part | What |
| --- | --- |
| `Sources/TrigCore` | the TRIGDECK engine (C, real time): machines, filters, envelopes, LFOs, the sequencer, scenes, decks with scratch buffers, performance and master FX, projects |
| `Sources/SloopCore` | SLOOP's firmware (`vendor/sloop`, unmodified) compiled for the Mac against host versions of the FM-1's hardware, as upstream's own host tests do; a text tap reads what its screen says |
| `Sources/DDJMapping` | pure Swift, unit tested: the DDJ-400's MIDI (codes from Mixxx's DDJ-400 mapping, checked on hardware), the TRIGDECK mapping and its control faces, the FM-1 panel mapping, the LED writer |
| `Sources/DDJGroovebox` | the app: the drawn controller (faces, VoiceOver, tooltips), the strip above it, audio (restarts on device changes; a fallback clock keeps instruments running), CoreMIDI (the DDJ-400, other inputs, MIDI out stamped with the audio's clock), instruments |

Projects are kept in `~/Library/Application Support/DDJGroovebox/` (`trigdeck.project`, `sloop-flash.bin`).

## Tests

```sh
swift test                      # mapping (87), SLOOP core and end-to-end (16), TRIGDECK engine (13)
sh Tests/c/run_core_smoke.sh    # SLOOP through its C API, with screenshots
cc -O2 -ffast-math -fno-finite-math-only -ISources/TrigCore/include -ISources/TrigCore Sources/TrigCore/td_*.c \
   Tests/c/trigcore_test.c -lpthread -lm -o .build-c/trigcore_test && .build-c/trigcore_test   # 128 engine checks
```

`Tests/c/core_engine_test.c` covers the review's engine fixes (clock timing, saves, damaged files, MIDI bursts), and
`Tests/c/trigcore_tsan.c` runs the engine under ThreadSanitizer. With a DDJ-400 attached,
`DDJ_PROBE=1 DDJ_PROBE_SECONDS=30 swift run DDJGroovebox` flashes its LEDs and prints every control you touch.
`DDJ_SNAPSHOT=out.png` plays a short script and saves the window. [docs/REVIEW.md](docs/REVIEW.md) is the critique the
current version answers.

## Credits and licence

- [SLOOP](https://github.com/isod89/sloop-fm1) by isod89, based on [Felucca](https://github.com/hugelton/Felucca) by
  Leo Kuroshita / Hügelton Instruments, GPL-3.0, vendored unchanged in `vendor/sloop`.
- Ideas from the FM-1 community's firmwares ([the hub](https://github.com/aroum/fm1-custom-fw)), DUB-1 by sbegueria
  above all; see docs/VISION.md.
- DDJ-400 MIDI codes from [Mixxx](https://github.com/mixxxdj/mixxx)'s Pioneer DDJ-400 mapping.
- Terminus font (SIL OFL), as SLOOP uses it.
- This project is GPL-3.0 ([LICENSE](LICENSE)).
