// SPDX-License-Identifier: GPL-3.0-only
// The DDJ-400's MIDI in, decoded into events (a port of Sources/DDJMapping/DDJ400Decoder.swift). MIDI codes from
// Mixxx's verified mapping (reference/mixxx/Pioneer-DDJ-400.midi.xml and Pioneer-DDJ-400-script.js).
//
// Tempo slider orientation, measured on a DDJ-400: fully towards the user (the "+" end) sends 16383, fully away 0.
// The decoder does not flip anything: absolute is always raw / 16383, so 1 is the "+" end (faster).

import { C, PAD_MODES, PAD_MODE_OFFSET, ev, press, absolute, relative } from "../vocab.js";

/** note on channel 0 / 1 -> [control maker, shifted] */
const DECK_NOTES = new Map([
    [0x0B, [C.play, false]], [0x47, [C.play, true]],
    [0x0C, [C.cue, false]], [0x48, [C.cue, true]],
    [0x58, [C.sync, false]], [0x60, [C.sync, true]],
    [0x5C, [C.sync, false]],          // BEAT SYNC long press: the same button (its release must not be lost)
    [0x10, [C.loopIn, false]], [0x4C, [C.loopIn, true]],
    [0x11, [C.loopOut, false]], [0x4E, [C.loopOut, true]],
    [0x4D, [C.reloop, false]], [0x50, [C.reloop, true]],
    [0x51, [C.callLeft, false]], [0x3E, [C.callLeft, true]],
    [0x53, [C.callRight, false]], [0x3D, [C.callRight, true]],
    [0x54, [C.headphoneCue, false]], [0x68, [C.headphoneCue, true]],   // SHIFT + mixer CUE
    [0x36, [C.jogTouch, false]], [0x67, [C.jogTouch, true]],
    [0x3F, [C.shift, false]],
    // the pad mode buttons (recorded on a DDJ-400; not in Mixxx's mapping): the controller switches its pads itself
    // and says which mode it chose
    [0x1B, [(d) => C.padModeButton(d, "hotCue"), false]],
    [0x6D, [(d) => C.padModeButton(d, "beatLoop"), false]],
    [0x20, [(d) => C.padModeButton(d, "beatJump"), false]],
    [0x22, [(d) => C.padModeButton(d, "sampler"), false]],
    [0x69, [(d) => C.padModeButton(d, "keyboard"), true]],
    [0x1E, [(d) => C.padModeButton(d, "padFX1"), true]],
    [0x6B, [(d) => C.padModeButton(d, "padFX2"), true]],
    [0x6F, [(d) => C.padModeButton(d, "keyShift"), true]],
]);

/** the MSB controller numbers of the 14-bit controls (LSB = MSB + 0x20) */
function absoluteControl(status, cc) {
    switch (status) {
    case 0xB0: case 0xB1: {
        const d = status === 0xB0 ? 0 : 1;
        switch (cc) {
        case 0x13: return C.channelFader(d);
        case 0x00: return C.tempo(d);
        case 0x04: return C.trim(d);
        case 0x07: return C.eqHi(d);
        case 0x0B: return C.eqMid(d);
        case 0x0F: return C.eqLow(d);
        default: return null;
        }
    }
    case 0xB6:
        switch (cc) {
        case 0x1F: return C.crossfader;
        case 0x17: return C.cfx(0);
        case 0x18: return C.cfx(1);
        case 0x0D: return C.headphoneLevel;
        default: return null;
        }
    case 0xB4:
        return cc === 0x02 ? C.fxLevel : null;
    default:
        return null;
    }
}

/** Turns raw DDJ-400 MIDI ([status, data1, data2]) into events. Keeps the MSB / LSB of the 14-bit faders and knobs. */
export class DDJ400Decoder {
    /** last MSB / LSB seen, keyed by status << 8 | MSB controller number */
    #msb = new Map();
    #lsb = new Map();

    /** Forgets the 14-bit halves (on a reconnect; a new decoder does the same). */
    reset() {
        this.#msb = new Map();
        this.#lsb = new Map();
    }

    /** [status, data1, data2] (any array-like, e.g. a Web MIDI message's data) -> an event or null */
    decode(m) {
        const status = m[0] & 0xFF, d1 = m[1] & 0xFF, d2 = m[2] & 0xFF;
        const kind = status & 0xF0, channel = status & 0x0F;
        switch (kind) {
        case 0x80: case 0x90:
            return this.#decodeNote(channel, d1, kind === 0x90 && d2 > 0);
        case 0xB0:
            return this.#decodeCC(status, d1, d2);
        default:
            return null;
        }
    }

    #decodeNote(channel, note, down) {
        const p = press(down);
        switch (channel) {
        case 0: case 1: {
            const entry = DECK_NOTES.get(note);
            if (!entry) return null;
            return ev(entry[0](channel), p, { shifted: entry[1] });
        }
        case 4:
            switch (note) {
            case 0x4A: return ev(C.beatLeft, p);
            case 0x4B: return ev(C.beatRight, p);
            case 0x63: return ev(C.fxSelect, p);
            case 0x64: return ev(C.fxSelect, p, { shifted: true });
            case 0x47: return ev(C.fxOnOff, p);
            case 0x43: return ev(C.fxOnOff, p, { shifted: true });
            // the beat FX channel switch (Mixxx: 0x10 CH1, 0x11 CH2, 0x14 MASTER): a note-on when moved there
            case 0x10: return down ? ev(C.fxChannel(0), p) : null;
            case 0x11: return down ? ev(C.fxChannel(1), p) : null;
            case 0x14: return down ? ev(C.fxChannel(2), p) : null;
            default: return null;
            }
        case 6:
            switch (note) {
            case 0x41: return ev(C.browsePress, p);
            case 0x42: return ev(C.browsePress, p, { shifted: true });
            case 0x46: return ev(C.load(0), p);
            case 0x47: return ev(C.load(1), p);
            default: return null;
            }
        case 7: case 8: case 9: case 10: {
            const deck = channel <= 8 ? 0 : 1;
            const shifted = channel === 8 || channel === 10;
            const mode = PAD_MODES.find((md) => PAD_MODE_OFFSET[md] === (note & 0xF0));
            if (mode === undefined) return null;
            const pad = note & 0x0F;
            if (pad > 7) return null;
            return ev(C.pad(deck, pad), p, { padMode: mode, shifted });
        }
        default:
            return null;
        }
    }

    #decodeCC(status, cc, value) {
        // jogs: centre 64, < 64 backward, > 64 forward
        if (status === 0xB0 || status === 0xB1) {
            const d = status === 0xB0 ? 0 : 1;
            if (cc === 0x21 || cc === 0x22 || cc === 0x23) return ev(C.jog(d), relative(value - 64));
            if (cc === 0x29) return ev(C.jog(d), relative(value - 64), { shifted: true });
        }
        // BROWSE: two's complement, 1...63 clockwise, 65...127 counter-clockwise
        if (status === 0xB6 && cc === 0x40) {
            const ticks = value < 64 ? value : value - 128;
            return ticks === 0 ? null : ev(C.browse, relative(ticks));
        }
        const control = absoluteControl(status, cc);
        if (control !== null) {
            const key = (status << 8) | cc;
            this.#msb.set(key, value);
            // The DDJ-400 sends MSB then LSB. Once a control has shown it sends its LSB, wait for it: the new MSB with
            // the old LSB would jump back for a moment (seen on the hardware), and the knobs turned into encoder steps
            // would wobble.
            if (this.#lsb.has(key)) return null;
            return ev(control, absolute((value << 7) / 16383));
        }
        if (cc >= 0x20) {
            const low = absoluteControl(status, cc - 0x20);
            if (low !== null) {
                const key = (status << 8) | (cc - 0x20);
                this.#lsb.set(key, value);
                if (!this.#msb.has(key)) return null;
                return ev(low, absolute(((this.#msb.get(key) << 7) | value) / 16383));
            }
        }
        return null;
    }
}
