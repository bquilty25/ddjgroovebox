// SPDX-License-Identifier: GPL-3.0-only
// The DDJ-400's LEDs: MIDI out, diffed against what was last sent (a port of Sources/DDJMapping/DDJ400LEDWriter.swift).
// LED codes from Mixxx's DDJ-400 mapping. Messages are [status, data1, data2] arrays.
//
// An LED source (TrigMapper, or anything else) gives:
//   lights() -> Map(control -> light): every lit control's light (buttons; the pads of the deck's current mode);
//   padLights(mode) -> [[8 lights], [8 lights]]: the pads' lights in a given mode, [deck][pad];
//   padMode(deck) -> mode: each deck's pad mode as last seen (its LEDs are sent first).

import { C, DECKS, PAD_MODES, PAD_MODE_OFFSET, OFF, ON } from "../vocab.js";

/** the deck buttons with LEDs, and their (unshifted, shifted) notes on 0x90 / 0x91 */
const BUTTONS = [
    [C.play, 0x0B, 0x47], [C.cue, 0x0C, 0x48], [C.sync, 0x58, 0x60], [C.loopIn, 0x10, 0x4C], [C.loopOut, 0x11, 0x4E],
    [C.reloop, 0x4D, 0x50], [C.callLeft, 0x51, 0x3E], [C.callRight, 0x53, 0x3D], [C.headphoneCue, 0x54, 0x68],
];

const byte = (l) => (l === ON ? 0x7F : 0x00);

/** Every LED in a fixed order: pads of every mode (unshifted 0x97 / 0x99 and shifted 0x98 / 0x9A, lit alike),
 *  deck buttons (their unshifted and shifted notes), BEAT FX ON/OFF. [status, note, value] each. */
function leds(padLights, button) {
    const list = [];
    for (const mode of PAD_MODES) {
        const pads = padLights(mode);
        for (const d of DECKS) {
            const status = d === 0 ? 0x97 : 0x99;
            for (let p = 0; p < 8; p++) {
                const v = byte(pads[d][p]);
                list.push([status, PAD_MODE_OFFSET[mode] + p, v]);
                list.push([status + 1, PAD_MODE_OFFSET[mode] + p, v]);
            }
        }
    }
    for (const d of DECKS) {
        const status = d === 0 ? 0x90 : 0x91;
        for (const [control, plain, shifted] of BUTTONS) {
            const v = byte(button(control(d)));
            list.push([status, plain, v]);
            list.push([status, shifted, v]);
        }
    }
    const fx = byte(button(C.fxOnOff));
    list.push([0x94, 0x47, fx]);
    list.push([0x94, 0x43, fx]);
    return list;
}

const defaultClock = () => performance.now() / 1000;

export class DDJ400LEDWriter {
    /** last data2 sent, keyed by status << 8 | data1 */
    #sent = new Map();
    #lastMeter = [null, null];
    /** the level each meter shows, dBFS (falls at meterDecayDBPerSecond) */
    #meterDB = [-Infinity, -Infinity];
    #meterTime = null;

    constructor() {
        /** the bottom of the meters: this level (dBFS) and below is dark, 0 dBFS is full (127) */
        this.meterFloorDB = -48;
        /** how fast a meter falls back after a peak */
        this.meterDecayDBPerSecond = 25;
        /** the clock, in seconds (injectable for tests) */
        this.now = defaultClock;
        /** LED messages sent at most per call (the app calls ~60 times a second). A refresh of every LED is ~300
         *  messages: sent at once, the controller can lose most of them, and the diff would never send them again.
         *  Over budget, the rest waits for the next call (it is still different from what was sent). */
        this.maxPerCall = 24;
    }

    /** Forgets what was sent: the next messages() resends everything. */
    invalidate() {
        this.#sent = new Map();
        this.#lastMeter = [null, null];
    }

    /** The LED messages that changed since the last call. meters: the output peaks [left, right], linear 0...1.
     *  Most visible first: the buttons, the pads of each deck's current mode, then the other modes' pads. */
    messages(source, meters = [0, 0]) {
        const out = [];
        const lights = source.lights();
        const current = new Set(DECKS.map((d) => source.padMode(d)));
        const all = leds((m) => source.padLights(m), (c) => lights.get(c) ?? OFF);
        const ordered = [
            ...all.filter((l) => l[0] < 0x97),                                                   // buttons, FX
            ...all.filter((l) => l[0] >= 0x97 && DDJ400LEDWriter.isMode(l[1], current)),        // pads, current modes
            ...all.filter((l) => l[0] >= 0x97 && !DDJ400LEDWriter.isMode(l[1], current)),       // pads, other modes
        ];
        for (const [s, n, v] of ordered) {
            if (out.length >= this.maxPerCall) break;
            this.#put(s, n, v, out);
        }

        const t = this.now();
        const dt = this.#meterTime === null ? 0 : Math.max(0, t - this.#meterTime);
        this.#meterTime = t;
        for (let d = 0; d < 2; d++) {
            const peak = meters[d];
            const db = peak > 0 ? 20 * Math.log10(Math.min(1, peak)) : -Infinity;
            this.#meterDB[d] = db >= this.#meterDB[d] ? db
                : Math.max(db, this.#meterDB[d] - this.meterDecayDBPerSecond * dt);
            const v = DDJ400LEDWriter.meterValue(this.#meterDB[d], this.meterFloorDB);
            const last = this.#lastMeter[d];
            if (last !== null && (last === v || (Math.abs(v - last) < 2 && v !== 0))) continue;
            this.#lastMeter[d] = v;
            out.push([0xB0 + d, 0x02, v]);
        }
        return out;
    }

    /** does the pad note belong to one of the modes (a Set of mode names)? */
    static isMode(note, modes) {
        for (const m of modes) if ((note & 0xF8) === PAD_MODE_OFFSET[m]) return true;
        return false;
    }

    /** A meter's CC value 0...127 for a level in dBFS: floor and below 0, 0 dBFS 127, linear in dB between. */
    static meterValue(dB, floor) {
        if (!Number.isFinite(dB) || !(dB > floor)) return 0;
        return Math.max(0, Math.min(127, Math.round(((dB - floor) / -floor) * 127)));
    }

    /** Every LED it manages off (on quit), the VU meters to 0. */
    allOff() {
        const out = [];
        const dark = [new Array(8).fill(OFF), new Array(8).fill(OFF)];
        for (const [s, n] of leds(() => dark, () => OFF)) {
            this.#sent.set((s << 8) | n, 0);
            out.push([s, n, 0]);
        }
        for (let d = 0; d < 2; d++) {
            this.#lastMeter[d] = 0;
            this.#meterDB[d] = -Infinity;
            out.push([0xB0 + d, 0x02, 0]);
        }
        return out;
    }

    #put(s, n, v, out) {
        const key = (s << 8) | n;
        if (this.#sent.get(key) === v) return;
        this.#sent.set(key, v);
        out.push([s, n, v]);
    }
}
