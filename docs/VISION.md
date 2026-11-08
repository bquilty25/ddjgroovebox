# TRIGDECK: a groovebox built for the DDJ-400

SLOOP showed that a DJ controller can drive a groovebox. TRIGDECK starts from the controller instead: an instrument
designed around the DDJ-400's own controls, with Elektron's sequencing depth, Teenage Engineering's clarity and the
gestures DJs already have in their hands.

## Principles

1. **The controller is the screen.** There is no separate device screen to decode. The window draws the DDJ-400,
   and every control shows on itself what it does and its value now: the pads are the 16 steps (trigs, locks,
   conditions, the playhead), the knobs show their parameter's name, value and arc, the jogs show their deck's
   tracks and sound, the loop buttons show the pages. A thin header carries only global state.
2. **Colour is meaning** (after Teenage Engineering). Each knob row has a fixed colour on both channels: HI blue,
   MID green, LOW white, FILTER orange. Whatever a knob does on a page appears on screen in its colour. Each of the
   8 tracks has its own colour, used on its pads, steps and jog segment.
3. **Elektron depth.** Trigs and parameter locks, trig conditions, micro timing, retrigs, per-track length, fills,
   mutes, 16 patterns that switch on the bar, two scenes.
4. **DJ gestures become groovebox gestures.** The crossfader morphs between scenes; the channel faders are the
   two decks' buses; touching a jog grabs that deck's sound to scratch it while the sequence keeps running
   underneath (slip), and letting go drops back in time; the beat FX section throws performance effects.

## The instrument

- **8 tracks in two decks.** Deck A holds tracks 1 to 4, deck B tracks 5 to 8. Each deck is a bus with its channel
  fader (level) and TRIM (drive).
- **Machines** (any track): KICK, SNARE, HAT, CLAP, TOM (synthesised, Syntakt-style), SYNTH (two oscillators,
  subtractive), FM (two operators), SAMPLE (built-in sounds, or a WAV dropped onto the track).
- **Pages**, 8 parameters each (A to H on the 8 knobs): TRIG (note, velocity, length, retrigs, condition), SRC
  (the machine), FLTR (multimode filter and its envelope), AMP (envelope, pan, level, drive), FX (sends, crush),
  LFO. A global MASTER page: delay, reverb, compressor, master filter.
- **Sequencer**: 64 steps per track (16 per page), per-track length, swing; per step: note, velocity, micro timing,
  retrigs, a condition (probability, A:B, FILL, NOT FILL, FIRST), any number of parameter locks.
- **Scenes**: scene A and scene B per pattern, each a set of parameter values. Crossfader centre plays the pattern
  as written; left morphs towards A, right towards B.
- **Performance FX** (beat FX section): REPEAT, STUTTER, TAPE STOP, DELAY THROW, REVERB THROW, FILTER SWEEP, CRUSH,
  on deck A, deck B or the master (the beat FX channel switch), at a beat division, held on ON/OFF.

## The controller

| DDJ-400 | TRIGDECK |
| --- | --- |
| Pads, HOT CUE mode | the active track's steps: left deck 1 to 8, right deck 9 to 16. Tap: trig on / off. Hold + knob: parameter lock. Hold + jog side: micro timing. Hold + BROWSE: condition. |
| Pads, BEAT LOOP mode | play the active track live (16 notes of its scale; drums: the deck's 4 tracks twice); recorded when REC is on |
| Pads, BEAT JUMP mode | that deck's 4 tracks: top row selects, bottom row mutes |
| Pads, SAMPLER mode | patterns 1 to 8 (left) and 9 to 16 (right): tap to queue, switches at the end of the bar |
| HI, MID, LOW, FILTER (both channels) | the 8 data-entry knobs A to D (channel 1) and E to H (channel 2) of the page shown |
| Loop buttons IN, OUT, RELOOP | pages: left deck TRIG, SRC, FLTR; right deck AMP, FX, LFO. Right BEAT SYNC: MASTER |
| CUE/LOOP CALL ◀ ▶ | left deck: previous / next track; right deck: step page (steps 17 to 64) |
| LOAD | previous / next pattern |
| BROWSE | the machine or sample (SRC page), a step's condition (step held); press: audition the track |
| PLAY | play / stop (SHIFT: back to the start) |
| CUE | left: record live; right: tap tempo |
| Left BEAT SYNC | FILL while held; SHIFT: AUTOPILOT on / off |
| LOAD with a step held | left: slide, right: accent (acid) |
| SHIFT + touch a jog | resample: the deck's last bar into a sample |
| TRACKS mode, a track's pad held | BROWSE: Euclidean pulses; jog: rotate; BROWSE press: dice |
| Jog, top touched | scratch that deck's sound (slip: the sequence runs on underneath) |
| Jog, side | the active track around the deck's ring of four; micro timing of a held step; SHIFT: nudge the tempo |
| TEMPO sliders | left: tempo; right: swing; towards you (the "+" end) = faster (soft takeover) |
| Channel faders, TRIM | deck A and deck B bus level and drive |
| Crossfader | scene A ← pattern → scene B |
| Headphone CUE (held) | edit scene A / B: knobs turned while held go into the scene (SHIFT: clear it) |
| Beat FX: FX SELECT, BEAT ◀ ▶, channel switch, LEVEL/DEPTH, ON/OFF | performance effect, division, target (deck A, deck B, master), amount, hold to play |

## Learned from the FM-1 community

The FM-1 hub's firmwares (github.com/aroum/fm1-custom-fw) explore ideas TRIGDECK takes up on the DDJ-400:

| Idea | From | In TRIGDECK |
| --- | --- | --- |
| Power on and it plays: a generative arrangement in 8-bar phrases (kick drops, breakdowns, builds, fills), chord-memory stabs into a tape delay, polymetric loops, DUB HOLD | DUB-1 (sbegueria), AMB-1 | AUTOPILOT (SHIFT + FILL), CHORD on the synth and FM machines, a tape delay, DUB HOLD in the beat FX, a dub techno pattern (P03) with polymetric track lengths |
| Create, modify, resample, repeat | Veffbox, PIGEON, April OS, Hortator | SHIFT + touch a jog: the deck's last bar becomes a sample; SLICES chops it across the pads |
| Algorithmic sequencing | FM-1 T1 (Torso T-1 style), Tessera, DUB-1 | TRACKS mode, hold a track: BROWSE sets Euclidean pulses, the jog rotates the pattern, BROWSE press rolls the dice |
| Acid lines | X0X, X0X-RMX (ReBirth style) | slide and accent per trig: hold a step, LOAD left / right |
| Conditional cross-track modulation | Elixir (LXR-02) | DUCK: any track ducked by the kick |
| Two drum machines from different eras, stacked | Big Beat Studio BB1 | deck A and deck B |
| Four colour-coded knobs named on screen | Gnomad, LCD-4, Teenage Engineering | the knob rows' fixed colours |

## Cartridges: every FM-1 firmware on the DDJ-400

The FM-1 community has written more than 70 firmwares (the hub: github.com/aroum/fm1-custom-fw): SLOOP, X0X,
ChoralRoot, PIGEON, Melodee, Mini-Tone, a Torso-style generator, a Plaits port, trackers. Nearly all are GPL forks of
Felucca and share its hardware layer. TRIGDECK runs them as **cartridges**: each firmware's own source, unmodified,
compiled against a host version of Felucca's hardware layer (keys, buttons, encoders, LEDs, screen, audio, flash) into
a plug-in the app loads. A cartridge browser switches between them; the DDJ-400 plays every cartridge through one
FM-1 panel mapping (16 pads = the white keys, the loop buttons = the FM-1's buttons, the knobs = its encoders, the
cartridge's screen shown in the mixer). SLOOP is the first cartridge (SloopCore today); the generic host layer comes
next, then X0X, ChoralRoot and Melodee.

## Build

`Sources/TrigCore` (C, real-time): machines, per-track filter / envelopes / LFO, sequencer, scenes, buses, scratch,
performance FX, master FX, patterns, project save. `Sources/DDJMapping`: the DDJ-400 decoder, LED writer and the
TRIGDECK mapping (pure Swift, tested). `Sources/DDJGroovebox`: the app, whose main view is the augmented controller.
SLOOP (vendor/sloop) stays in the repository as the reference it was; its host build (SloopCore) remains available.
