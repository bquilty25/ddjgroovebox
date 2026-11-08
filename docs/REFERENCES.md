# References

Manuals of the instruments TRIGDECK learns from (linked, not copied: they belong to their makers).

## Elektron

- **Digitakt** user manual, OS 1.53: <https://www.elektron.se/wp-content/uploads/2026/09/Digitakt-User-Manual_ENG_OS1.53_260909.pdf>
  (support page with all versions: <https://www.elektron.se/support-downloads/digitakt>; the Digitakt II has its own:
  <https://www.elektron.se/support-downloads/digitakt-ii>)
- **Syntakt** user manual, OS 1.40: <https://www.elektron.se/wp-content/uploads/2026/03/Syntakt-User-Manual_ENG_OS1.40_260304.pdf>
  (support page: <https://www.elektron.se/support-downloads/syntakt>)

What TRIGDECK takes from them: trigs, parameter locks, trig conditions, micro timing, retrigs, per-track lengths,
pattern chaining at the bar, scenes and the crossfader, and a picture of each page's parameters (the envelope, the
filter, the LFO) on screen while they change.

## teenage engineering

- **OP-1 field** user guide (online, with PDF): <https://teenage.engineering/guides/op-1>; PDF for firmware 1.7.0:
  <https://assets.teenage.engineering/_img/69f248938d433104c4dd1846_original.pdf>
- **OP-1** (original) reference: <https://teenage.engineering/guides/op-1/original/reference>; quick guide:
  <https://teenage.engineering/guides/op-1/original/quick-guide>
- All guides: <https://teenage.engineering/guides>

What TRIGDECK takes from them: fixed knob colours (blue, green, white, orange), playful animated graphics that show
what the sound is doing, and few words on screen.

## What the manuals teach (read November 2026: Digitakt OS 1.54, Syntakt OS 1.40, OP-1 field 1.7.0)

**Sounds, kits and packs.**
- A pattern holds a copy of its kit (8 sounds); loading a sound copies it, never links (Digitakt §9, Syntakt §6.2).
  Save, reload and clear each offer the whole pattern, the kit only or the sequence only: keep the beat, swap the kit.
- A browser with preview (through the track's own voice and FX), tags and search; a sound pool per project; a sound
  or sample can be locked on one step (Digitakt §9.2, §10.9.2).
- OP-1: eight instant preset slots, a revert key, a two-column browser (engine, then preset), automatic snapshots.
  Every factory kit puts kick, snare and hats on the same keys, so swapping kits keeps a pattern musical: a pack must
  keep a fixed voice map per track.
- Separate "swap the whole sound" from "swap only the machine".

**Machines (Syntakt Appendix A), for synthesised packs.** A drum is a pitched body (sweep time, sweep depth, hold,
decay) plus a transient (tick, snap, impulse) and noise with its own decay. 808 kick: sine, long decay, short sweep;
909 kick: triangle, snap and tick, some overdrive; 808 snare: two detuned tones and noise; 909 snare: band-passed
noise; hats: six square oscillators at inharmonic ratios; cowbell: two detuned squares; electric piano: two-operator
FM with the modulator's envelope shorter than the amp's; toms: an impulse pinged through a resonant filter; chords and
supersaws: chord shapes and detuned unison. Drum sounds use an AHD envelope, synths ADSR.

**Performance.** Temporary save / reload of a pattern (wreck it, then undo in one press); fill held, for one cycle or
latched; PRE / NEI conditions ("this or that"); knob moves recorded as locks; Control All (one knob, every track);
mutes prepared then dropped together; tempo nudge and a BPM that applies on release; per-page randomise and revert;
choke groups for open and closed hats; trig modifiers (hold a pad to play with a velocity or retrig offset); song mode
rows (pattern, repeats, length, tempo, mutes) as a model for the autopilot. OP-1 tape tricks (loop, break, reverse,
chop, two memos blended) map onto the DDJ's pads and crossfader; its finger sequencer resembles hot cues.

**Screen.** One cell per knob in the knobs' layout, each a small picture of its kind of value; a locked value drawn
inverted; the queued pattern flashing until it plays; lock-only steps in another colour than note steps; popups while a
step is held. OP-1: whatever an encoder controls is drawn in that encoder's colour; each engine is a metaphor whose
shape a knob changes (wobble, tension, size), not a number; motion carries information (tombola's bounce speed is the
velocity); effects share one layout (four values in the corners, mix on a side); a sound-path map warns about stages
at zero.

## DDJ-400

- Mixxx's DDJ-400 mapping (MIDI codes): <https://github.com/mixxxdj/mixxx/blob/main/res/controllers/Pioneer-DDJ-400.midi.xml>
  and script: <https://github.com/mixxxdj/mixxx/blob/main/res/controllers/Pioneer-DDJ-400-script.js>. Not in it, and
  recorded on a DDJ-400 here: the pad mode buttons' notes, and the tempo sliders' direction (towards you sends 16383).
