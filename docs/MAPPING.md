# DDJ-400 to FM-1 mapping (cartridges: SLOOP)

This is how the DDJ-400 plays a cartridge (Instrument > SLOOP). TRIGDECK, the app's own instrument, has its own
mapping, described in [TRIGDECK.md](TRIGDECK.md) and shown on every control in the window.

SLOOP was written for the M-VAVE FM-1: 27 keys (F3 to G5: 16 white, 11 black), 14 buttons and 7 encoders. The app
runs SLOOP unchanged and turns the DDJ-400 into that panel. Each deck keeps its own pad mode, as on the hardware; the
faders, tempo sliders and crossfader take over only once moved past SLOOP's current value (a mark shows where). Every SLOOP gesture works: tap a button to open its
pages, hold it and touch pads for its layer (SEQ held + pads writes steps, FX held + pads punches effects, and so on).
See [SLOOP's manual](../vendor/sloop/SLOOP.md) for what each FM-1 control does.

## Pads

The 16 pads (8 per deck) follow the pad mode chosen on the DDJ. Pads are numbered 1 to 8, top row 1 to 4, bottom row 5 to 8.

| DDJ pad mode | What the pads play |
| --- | --- |
| HOT CUE | **White keys**: left pads 1 to 8 are white keys 1 to 8 (F3 to F4), right pads 1 to 8 are white keys 9 to 16 (G4 to G5). These are SLOOP's 16 steps, 16 drum sounds and 16 layer tiles. |
| BEAT LOOP | **Piano, low**: the two decks form one 2 x 8 grid. Bottom row: white keys 1 to 8. Top row: the black key just above each white key (none above B and E). |
| BEAT JUMP | **Piano, high**: the same with white keys 9 to 16 (G4 to G5). |
| SAMPLER | **Black keys**: left pads 1 to 8 are black keys 1 to 8, right pads 1 to 3 are black keys 9 to 11. Right pads 5 to 8 **select track 1 to 4** directly (4 is the drums). |
| KEYBOARD (SHIFT + HOT CUE) | as BEAT LOOP |
| PAD FX1 (SHIFT + BEAT LOOP) | **Punch-in effects on the white keys**: as HOT CUE, with FX held for you while a pad is down. |
| PAD FX2 (SHIFT + BEAT JUMP) | **Punch-in effects on the black keys**: as SAMPLER's black keys, with FX held. |
| KEY SHIFT (SHIFT + SAMPLER) | as BEAT JUMP |

SHIFT + a pad in any mode plays that pad's key with FX held (a punch-in effect).

A pad lights when SLOOP lights its key (a step set, a note sounding, a layer's tile).

## Buttons

| DDJ-400 | Left deck | Right deck |
| --- | --- | --- |
| PLAY/PAUSE | PLAY | PLAY |
| CUE | REC | REC |
| BEAT SYNC | SEQ | FX |
| LOOP IN | EDIT | ENV |
| LOOP OUT | ARP | LFO |
| RELOOP/EXIT | GLO | SEL |
| CUE/LOOP CALL left | OCT- | OCT- |
| CUE/LOOP CALL right | OCT+ | OCT+ |
| Headphone CUE (mixer) | HOME | SAVE |
| LOAD | previous track | next track |

| DDJ-400 (centre) | SLOOP |
| --- | --- |
| BROWSE turn | SELECT (pages; tempo on HOME) |
| BROWSE press | HOME |
| BEAT FX ON/OFF | FX (hold it for the FX layer) |
| BEAT left / right | SELECT one step back / forward |
| BEAT FX SELECT | PRESETS one step forward (with SHIFT: back) |

Each DDJ button holds its SLOOP button for as long as it is held. A button lights when SLOOP lights it
(PLAY blinks on the beat, REC while recording, the layer you are in).

## Knobs, faders and jogs

| DDJ-400 | SLOOP |
| --- | --- |
| Left jog | ALGORITHM: the selected track |
| Right jog | PRESETS: the sound |
| EQ HI, MID, LOW and CFX (FILTER), either channel | KNOB 1, 2, 3, 4 (turning acts as the FM-1's encoders) |
| Channel 1 fader | level of the selected track |
| Channel 2 fader | level of the drums |
| Crossfader | master DJ filter: left low-pass, centre off, right high-pass |
| Left TEMPO slider | BPM, 60 to 180 |
| Right TEMPO slider | swing, 0 to 100 |
| Left TRIM | master volume |

Faders and the tempo sliders use soft takeover: after the value changes elsewhere (the FM-1 screen, another track
selected), a fader only takes over once it is moved past the current value.
The channel level meters show the output level.

## On-screen DDJ-400

The window draws the DDJ-400 with each control's SLOOP job printed on it. It lights as the hardware does and moves
as you touch the hardware. Without a controller, click it: pads and buttons are held while the mouse is down, drag
knobs and faders up or down, scroll over a jog or BROWSE.
