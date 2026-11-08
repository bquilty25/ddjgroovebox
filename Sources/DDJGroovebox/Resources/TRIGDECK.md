# TRIGDECK guide

TRIGDECK is a groovebox built for the Pioneer DDJ-400. The window draws the controller, and every control on it says
what it does right now and shows its value: you never have to translate between a screen and the hardware.

## Your first minute

1. Press **PLAY** (either deck). The house / techno pattern TRIGDECK opens with starts.
2. Touch the **left jog's top** and move it: you are scratching deck A (kick, open hat, clap, closed hats) while the pattern runs
   on underneath. Let go: it drops back in time.
3. Move the **crossfader** left and right: the pattern morphs into scene A and scene B. The middle is the pattern as
   written.
4. Hold **SHIFT** and press the **left BEAT SYNC** (FILL): AUTOPILOT arranges the pattern in 8-bar phrases (kick drops,
   breakdowns, builds). Do it again to take over.
5. Press a **pad** (HOT CUE mode): a step of the active track turns on or off. Hold a pad and turn a coloured knob: that
   step alone gets that value (a parameter lock).

## Patterns to start from

A new project holds five patterns, at 124 BPM in A minor. Switch with the pads in SAMPLER mode (left deck pads 1-5) or
with LOAD ◀ ▶; the switch happens at the end of the bar.

| Pattern | What it is |
|---|---|
| P01 HOUSE / TECHNO | four on the floor, open hats on the off-beats, clap on 2 and 4, closed sixteenths with ghosts, a rolling off-beat bass, a minor 9 stab into the delay, an FM pluck, a vocal blip every fourth bar. Hold the left BEAT SYNC (FILL): kicks and a clap roll at the end of the bar |
| P02 BREAKDOWN | P01 without the kick and the clap, to mix in and out |
| P03 DUB TECHNO | polymetric (16, 12, 10 and 7 steps), a chord stab into the tape delay, after DUB-1 |
| P04 ACID | the four-on-the-floor beat under a resonant 303-style line with slides and accents |
| P05 ELECTRO | broken kicks, a snare on 2 and 4, a square bass, an FM zap |
| P06-P16 | empty, with P01's sounds: write your own |

Each has its own scenes on the crossfader: on P01, scene A is a filtered breakdown and scene B opens and drives the
bass and the stab.

## How to read the controller

- **Colours.** The knob rows always have the same colour: HI blue, MID green, LOW white, FILTER orange. The name
  printed beside a knob, in its colour, is what it changes on the page shown. Each of the 8 tracks has its own colour,
  used for its steps, its pads and its band on the jog.
- **Pads** show the active track's steps: lit = a trig, a dot = parameter locks, "1:2" or "50%" = a condition, "x3" =
  retrigs, "~" = slide, "!" = accent; the white outline is the playhead (or the step you are holding).
- **Jogs** show the deck's four tracks around the platter, with their levels; the active track is bright, a muted one
  hollow.
- **The header** (top of the mixer): pattern (and the next one, if queued), tempo, swing, transport, the active track
  and the page.
- **The strip above** shows all 8 tracks' steps at once: deck A's tracks above deck A, deck B's above deck B, and the
  transport, scenes and autopilot above the mixer.

## The controls

| Control | What it does |
| --- | --- |
| Pads in HOT CUE (**STEPS**) | the active track's 16 steps (left deck 1-8, right deck 9-16). Tap: on / off. Hold + knob: lock. Hold + jog side: micro timing. Hold + BROWSE: condition. Hold + LOAD left / right: slide / accent |
| Pads in BEAT LOOP (**KEYS**) | play the active track live: 16 notes from its TRIG note; on a drum track, the deck's four tracks (top row loud, bottom row soft) |
| Pads in BEAT JUMP (**TRACKS**) | the deck's four tracks: top row selects (hold: BROWSE makes it Euclidean, the jog rotates it, BROWSE press rolls the dice), bottom row mutes |
| Pads in SAMPLER (**PATTERNS**) | patterns 1-8 (left), 9-16 (right): tap to queue (switches at the end of the pattern); SHIFT + tap copies the current pattern there |
| HI, MID, LOW, FILTER | the 8 parameters of the page: channel 1 = A-D, channel 2 = E-H |
| LOOP IN, LOOP OUT, RELOOP | pages: left deck TRIG, SRC, FLTR; right deck AMP, FX, LFO |
| Right BEAT SYNC | the MASTER page (delay, reverb, compressor...); again: back |
| CUE/LOOP CALL ◀ ▶ | left deck: previous / next track; right deck: step page (17-32...) |
| A track's row in the strip on top | click it: that track becomes the active one |
| LOAD ◀ ▶ | previous / next pattern |
| BROWSE | the machine (SRC page) or the track; press: audition the track |
| PLAY | play / stop; SHIFT: back to the start |
| CUE | left: record live (pads in KEYS mode are recorded); right: tap tempo |
| Left BEAT SYNC | FILL while held; SHIFT: AUTOPILOT |
| Jog, top touched | scratch the deck (slip); SHIFT + touch: resample the deck's last bar into a sample |
| Jog, side | a dial around the deck's ring of four tracks (left jog: tracks 1-4, right jog: 5-8): the marker on the drawn jog turns with the platter, a quarter turn a track, and the quadrant it points into is the active track. Turning the other deck's jog a little brings the active track to that deck. With a step held: its micro timing; with a track's pad held: rotate it; SHIFT: nudge the tempo |
| TEMPO sliders | left: tempo, right: swing; towards you (down, the "+" end) = faster / more, on screen as on the hardware (they take over once moved past the current value) |
| Channel faders, TRIM | deck A / deck B level and drive |
| Crossfader | scene A ← pattern → scene B |
| Headphone CUE (held) | edit scene A / B: turn knobs while held; SHIFT + CUE clears the scene |
| BEAT FX | FX SELECT: the effect (REPEAT, STUTTER, TAPE STOP, DELAY THROW, REVERB THROW, FILTER, CRUSH, DUB HOLD); BEAT ◀ ▶: its division; the channel switch: deck A, deck B or master; LEVEL/DEPTH: amount; ON/OFF: press to switch it on, press again to switch it off |

## Without a DDJ-400

Click and drag the drawing (pads and buttons hold while the mouse is down; drag knobs and faders; scroll over a jog or
BROWSE), or play the Mac keyboard, laid out like the controller:

| Keys | Controls |
| --- | --- |
| 1-8, Q-I | the left and right deck's pads |
| A S D F / J K L ; | LOOP IN, LOOP OUT, RELOOP, BEAT SYNC of the left / right deck |
| Z X / N M | CUE/LOOP CALL ◀ ▶ of the left / right deck |
| C / V | CUE left / right |
| space | PLAY |
| , . | LOAD left / right |
| ← → / return | BROWSE turn / press |
| tab | BEAT FX ON/OFF |
| G / H | headphone CUE 1 / 2 (scenes) |
| F1-F4 / F5-F8 | the left / right deck's pad mode |
| shift | SHIFT |
