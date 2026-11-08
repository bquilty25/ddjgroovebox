# FAQ

## Getting started

**What is this?**
A groovebox played from a Pioneer DDJ-400. TRIGDECK, the instrument it opens with, was designed for the controller:
8 tracks in two decks, a step sequencer with parameter locks, scenes on the crossfader, scratchable decks and beat FX.
Instrument > SLOOP (Cmd-2) plays a cartridge instead: an M-VAVE FM-1 firmware running unchanged.

**How do I read the screen?**
The window draws the DDJ-400 and every control shows what it does and its value right now. The knob rows always have
the same colour (HI blue, MID green, LOW white, FILTER orange), and the name beside a knob is what it changes on the
page shown. Pads show the active track's steps; the jogs show their deck's four tracks; the strip on top shows all 8
tracks at once. The guide ([TRIGDECK.md](TRIGDECK.md), Help menu) explains the rest.

**My first beat?**
Press PLAY: the deep house pattern starts. Move the crossfader (scenes), touch a jog and move it (scratch), hold
SHIFT and press the left BEAT SYNC (AUTOPILOT). To write: HOT CUE mode, tap pads to set steps; hold a pad and turn a
knob to lock that value on that step.

**What other patterns are there?**
Sixteen, four of each: house (P01-04), techno (P05-08), garage (P09-12) and drum & bass (P13-16), each with its own
tempo. In SAMPLER mode the left pads switch to house and techno, the right pads to garage and drum & bass, at the end
of the pattern (LOAD ◀ ▶ also steps through them). Their kits are also sound packs: on the MASTER page, BROWSE chooses
one, push loads it.

**Do I need the DDJ-400?**
No. Click and drag the drawing, scroll over a jog or BROWSE, or play the Mac keyboard: 1-8 and Q-I are the pads, space
is PLAY, A S D F / J K L ; the loop buttons and BEAT SYNC of each deck, shift is SHIFT (the full table is in the
guide). Click a SHIFT button once to latch it.

**How do I connect the DDJ-400?**
Plug it in by USB, before or after opening the app. It is found by name and reconnected when plugged in again; the
small dot at the top of the mixer turns green.

## Sound

**I hear nothing.**
Press PLAY. Check the Mac's output (System Settings > Sound) and the channel faders (deck A and deck B levels). The
DDJ-400 is also an audio interface: choose it as the output to hear the app through its master and headphone outputs.
If the audio system cannot start, the strip says so in red; the app keeps running and retries.

**There is a delay between a pad and the sound.**
The app asks the output device for 256-sample buffers (about 5 ms). Bluetooth and AirPlay outputs add their own large
delay: use wired headphones or the DDJ-400's outputs.

**How do I stop everything?**
Groovebox > Panic (Cmd-.) stops the transport and lets every note and effect go.

## Playing

**A fader or tempo slider does nothing.**
Faders and the tempo sliders use soft takeover: after a value changes elsewhere, the fader takes over once you move it
past the current value (a mark on the drawing shows where).

**The pads on the hardware and on screen differ.**
The DDJ-400's pads are on or off; faint glows (a lock-only step, an inactive track) show only on screen. Each deck has
its own pad mode, as on the hardware.

**What do the pad badges mean?**
A dot: parameter locks. "1:2", "50%", "FILL": a trig condition. "x3": retrigs. "~": slide. "!": accent. A white
outline: the playhead or the step you are holding.

**How do I make a sample from what is playing?**
Hold SHIFT and touch a jog: the deck's last bar becomes a sample. With a SAMPLE track active it plays it at once; SLICES
on its SRC page chops it across the pads (KEYS mode).

**What does AUTOPILOT do?**
It arranges the pattern in 8-bar phrases (kick drops, breakdowns, builds, fills, slow filter drifts, a chord
progression) without changing what you wrote. Anything you do wins (a track you mute stays muted); turning it off puts
everything back. It was inspired by DUB-1, an FM-1 firmware by sbegueria.

## Projects and data

**Is my work saved?**
Yes, every few seconds after a change and when you quit: TRIGDECK in `trigdeck.project`, the SLOOP cartridge in
`sloop-flash.bin`, both in `~/Library/Application Support/DDJGroovebox/`. A file that cannot be read is kept aside as
`.damaged`, never overwritten.

**How do I start again?**
Quit and delete `trigdeck.project`: TRIGDECK opens on its default patterns again.

## MIDI and other software

**Can it drive a DAW or another synth?**
TRIGDECK sends MIDI clock and start / stop to a MIDI source named "DDJ Groovebox", timed with the audio (each message
leaves stamped with the moment it is heard). The SLOOP cartridge sends what its own menus turn on (GLO > MIDI).

**Can it follow a DAW, or be played from a MIDI keyboard?**
The SLOOP cartridge: yes, other MIDI inputs (notes, controllers, clock, start / stop) reach it. TRIGDECK does not read
external MIDI yet.

**Can I run rekordbox or Mixxx at the same time?**
Both would receive the controller and set its LEDs. Quit the other DJ software first.

## The project

**What are cartridges?**
The FM-1 community has written more than 70 firmwares, nearly all forks of Felucca sharing one hardware layer.
Cartridges run them unchanged, played through an FM-1 panel mapping. SLOOP is the first; more need their sources
downloaded and built, which is a separate, deliberate step.

**Is this GrooveOS or SLOOP?**
Neither. GrooveOS is closed source and was only a reference for the experience. SLOOP is included as a cartridge,
unmodified (GPL-3.0). TRIGDECK is this project's own.

**macOS will not open the app on another Mac.**
The app is signed ad hoc (not by a registered developer). On another Mac, try to open it once, then allow it in System
Settings > Privacy & Security ("Open Anyway"); or build it there with `scripts/build_app.sh`.

**What does it need?**
macOS 14 or later. It has been built and tested on Apple silicon; Intel Macs should work but are untested.
