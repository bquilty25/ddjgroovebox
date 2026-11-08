# Critique and improvement plan (2026-11-08)

Four independent reviews (the engine and audio, the DDJ-400 mapping, the app's UX and code, and the user's own
critique), plus runtime measurements. Findings marked CONFIRMED were reproduced or measured; PLAUSIBLE ones were
found by reading the code.

## The critique

### The one that matters most: the screen and the controller do not talk to each other

The user: *"it's not clear what buttons correspond to what's on the screen at all."* The window showed SLOOP's screen
and the DDJ-400 side by side with nothing linking them. The DDJ's EQ knobs said "K1".."K4" while SLOOP's screen
called the same knobs "SWING", "LEVEL", "STEPS", "PAN"; holding SEQ filled the screen with 16 numbered tiles while
the pads still said "F3".."G5"; a held layer showed as a 1.5 px ring; the four knob colours on SLOOP's screen
(blue, green, yellow, orange) appeared nowhere on the controller.

### Engine and audio

| # | Severity | Finding |
| --- | --- | --- |
| E1 | critical, CONFIRMED | SLOOP's HARDWARE CALIBRATION menu item hangs the app: its loop waits for buttons or 30 s of `fm1_ms`, which only advances in the audio render it runs inside. Reachable from the DDJ. Force-quit, work lost. |
| E2 | high | An output-device change (switching output, unplugging the DDJ-400 as audio interface) stops AVAudioEngine and nothing restarts it. SLOOP's clock and UI run in the render, so the whole groovebox freezes, not only the sound. |
| E3 | medium, CONFIRMED | MIDI clock, start / stop and SysEx from other inputs are dropped (only UMP type 2 is read): SYNC to a DAW cannot work. |
| E4 | medium, CONFIRMED | Clock out is drained by the 60 Hz UI timer, unstamped: pulses 16.7 to 33.3 ms apart instead of 20.8 at 120 BPM; a 1.3 s UI stall overflows the 64-slot queue and loses note-offs. |
| E5 | medium-low, CONFIRMED | Concurrent flash saves race (static buffer, fixed tmp name; 50 of 400 overlapping pairs failed); the dirty flag is cleared before the write; no fsync. |
| E6 | low-medium | A damaged flash file boots blank and the quit save overwrites it; a failed `sloop_init` would save zeros. |
| E7 | low | MIDI REMOTE (buttons over MIDI CC) never reaches the panel. |
| E8 | low | No MIDI-in buffering: a burst over 64 events, or input while audio is stopped, is dropped. |
| E9 | measured | Real-time cost is small (render of 256 frames: 51 µs mean, 129 µs max of a 5.8 ms budget), but the screen copy converts 57,600 pixels under the audio lock 60 times a second, changed or not. |

### Performance (measured)

| # | Finding |
| --- | --- |
| P1 | 20 to 25 % CPU while a pattern played: the whole DDJ drawing (about 80 controls of text and paths) redrew 60 times a second because the level meters changed every frame, and SLOOP's screen image was replaced every UI frame even when unchanged. Fixed first: 3 to 6 %. |
| P2 | 300 to 430 MB of memory, almost all graphics: drawing a new scaled CGImage of SLOOP's screen into the view each frame (with the screen view hidden: 20 to 50 MB). |

### Mapping

| # | Severity | Finding |
| --- | --- | --- |
| M1 | medium, CONFIRMED | Reconnecting the DDJ makes knobs and levels jump (stale positions kept). |
| M2 | medium, CONFIRMED | The tempo slider never takes over when BPM is outside 60 to 180 (SLOOP allows 40 to 240). |
| M3 | medium, CONFIRMED | The crossfader overwrites a master filter set from SLOOP (FX + KNOB 1). |
| M4 | low-medium, CONFIRMED | Two pads on one key lose strikes; a punch on a held key does nothing. |
| M5 | low, CONFIRMED | One pad mode for both decks, while the DDJ has one per deck: one deck is drawn and captioned wrongly. |
| M6 | low | Track pads stepped ALGORITHM detents: a quick second press could land on the wrong track. |
| M7 | low, CONFIRMED | Jog accumulators never decay: light brushes add up to a track change. |
| M8 | low | 0x5C (BEAT SYNC long press) ignored: SEQ / FX could stay held. VU meters linear, pinned at the top. |
| M9 | feel | EQ knobs as K1..K4 overshoot (64 detents a turn, multiplied by SLOOP's acceleration); the right jog changes sound at a brush; the black keys need a pad-mode change in the step layout. |

### UX and app

| # | Finding |
| --- | --- |
| U1 | Without a DDJ-400 SLOOP's core gesture (hold a button, tap pads) is impossible with one mouse pointer, and there is no computer-keyboard play, yet the FAQ says the controller is optional. |
| U2 | Captions are context-blind and hard to read (8 px pixel font scaled to 5.5 to 7 pt; deck numbers at 1.6:1 contrast; captions read as belonging to the neighbouring control: "TRIM K1", "FILTER HOME"). |
| U3 | On-screen faders do not show SLOOP's values (BPM 90 drawn at the 120 position) and soft takeover is invisible. |
| U4 | Missing macOS basics: Edit and Window menus (Copy in the FAQ, Cmd-W, Cmd-M), window size and position not remembered, a default window taller than a 13" screen, no icon, no About credits. |
| U5 | A pad or SEQ held with the mouse stays held when the window loses focus; pressed outlines stay after unplugging. |
| U6 | Audio failure shows as one dim line while everything else silently stops (see E2). |
| U7 | The demo and snapshot modes played into the user's real project and saved it. Fixed. |
| U8 | Docs claims that were wrong: "every other control is mapped" (right TRIM, LEVEL/DEPTH, headphone LEVEL, jog touch do nothing), LOAD "selects tracks directly" (it steps), the crossfader "drags up or down", the Gatekeeper advice for macOS 15. |

## The plan

1. **Integrate the screen and the controller** (the user's critique).
   - SloopCore taps SLOOP's own text drawing (no change to SLOOP): every label on the screen and where it is.
     ScreenReader turns it into the four knobs' names, values and colours, a held layer's 16 tiles and its title.
   - One device: the screen strip becomes the top of the controller, three columns continuing the left deck, the
     mixer and the right deck. SLOOP's screen sits above the mixer; the left column reads out the left deck's
     controls (track, BPM, transport, the held layer), the right column the right deck's (sound, swing, what the
     pads do, help for the control under the pointer).
   - Live captions: pads say what the screen says (step numbers in SEQ, effect names in FX, drum names on the drum
     track); K1..K4 say "SWING 0", "ATK 1.0 MS" and take the dial's colour; the held layer's button is filled and
     tagged HELD; touching a knob or a pad lights its dial or tile on SLOOP's screen.
2. **Engine robustness**: E1 (a clock that moves when the firmware waits), E2 (restart audio on device changes; a
   silent fallback clock keeps SLOOP running while audio is down), E3 to E5, E7, E8, E9.
3. **Mapping fixes** M1 to M9 (per-deck pad modes, direct track select, takeover fixes, SHIFT + pad = the black
   key above in HOT CUE, 32 detents a turn, safer jogs, tap tempo on SHIFT + left BEAT SYNC, dB meters).
4. **App**: computer-keyboard play, standard menus, remembered window frame sized to the screen, release on focus
   loss, clearer audio errors, P2 (SLOOP's screen in its own Core Animation layer).
5. **Docs**: correct every claim above; MAPPING.md and the FAQ follow the new behaviour.

Work split: the mapping (3) and the DDJ drawing's part of (1) and (4) went to two subagents with written briefs;
the core, the screen strip, the app and the docs are done in the main session. Each part lands with tests.

## Direction change (the user, during the work)

The user asked to move beyond SLOOP ("inspiration, but this is clearly something very different"), towards an
instrument in the spirit of Elektron (Digitakt, Syntakt) and Teenage Engineering (OP-1, OP-Z), with ideas from the
FM-1 community's firmwares (DUB-1 especially). The plan became docs/VISION.md: TRIGDECK, a native groovebox designed
for the DDJ-400, with SLOOP and the other FM-1 firmwares as cartridges. Items above specific to SLOOP's captions
(the live text tap's use for SLOOP, SHIFT + pad black keys, SLOOP tap tempo) were dropped; everything generic stayed.

## Status

| Item | State |
| --- | --- |
| P1 CPU (20-25 % to 3-6 %), P2 memory | P1 done (only changed controls redraw, unchanged screen frames skipped); P2: the cause found (the strip's scaled screen image), the fix comes with the TRIGDECK strip |
| E1 calibration hang | fixed (a firmware clock that moves while the firmware waits), reproduced before and after |
| E2 audio device change | fixed: restart on configuration change, a fallback clock keeps the instrument running while audio is down, problems shown |
| E3 clock / start / stop from other inputs | fixed (UMP type 1 read) |
| E4 clock-out jitter | fixed: MIDI stamped with the audio's sample clock, sent every 2 ms with its time; tested: every pulse within one 32-frame block of ideal |
| E5 concurrent saves, E6 damaged flash | fixed and tested (80 concurrent saves; the damaged file kept as .damaged) |
| E7 MIDI REMOTE, E8 MIDI-in burst | fixed; E8 tested (300 messages, none lost) |
| E9 screen copy under the audio lock | fixed (raw copy under the lock, converted outside, only when a new frame exists) |
| M1-M8 | fixed and tested (63 then 86 mapping tests) |
| U1 no keyboard play | done: the Mac keyboard laid out like the DDJ-400 |
| U4 macOS basics | done: Edit, View (full screen), Window menus, Cmd-H, Cmd-W, Cmd-M, the window frame remembered and sized to the screen |
| U5 focus loss | done: mouse- and keyboard-held controls released |
| U7 demo mode wrote the real project | fixed |
| The screen and the controller not linked (the user's critique) | done: TRIGDECK's controls show their own faces (job, value, colour, light, badges, playhead); the strip and the drawing are one device; the jogs show their decks; the knob rows keep fixed colours |
| TRIGDECK | built: engine (128 C checks, 13 Swift tests, ThreadSanitizer clean, ~3 % of a core), mapping (TrigMapper, tested), the augmented drawing, the 8-track strip, the guide; the default instrument |
| Performance with TRIGDECK | 86 % of a core while playing at first; measured and fixed down to ~26 %: a 2 ms dispatch timer (worker thread spinning) replaced by one thread, the audio latency no longer asked 60 times a second, the static background split from the moving parts, levels at 10-20 Hz, the drawing at 20 Hz. Further gains need a layer per control (follow-up) |

## Follow-ups

- The drawing's CPU while playing (~26 %): a layer per control instead of redrawing into shared surfaces.
- More cartridges (X0X, ChoralRoot, Melodee...): their sources need downloading and building; a generic host for
  Felucca's hardware layer (designed in docs/VISION.md) would take most hub firmwares.
- TRIGDECK: external MIDI in (notes, clock), recording knob moves as locks, samples dropped onto the window.
