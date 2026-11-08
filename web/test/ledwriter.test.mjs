// SPDX-License-Identifier: GPL-3.0-only
// DDJ400LEDWriter (after Tests/DDJMappingTests/DDJ400LEDWriterTests.swift, which drives it with the SLOOP cartridge's
// GrooveboxMapper; here a small fake LED source), and DDJPadFunction. Run: node --test web/test/
import { test } from "node:test";
import assert from "node:assert/strict";
import { DDJ400LEDWriter, DDJPadFunction, FM1Keyboard } from "../src/mapping/index.js";
import { C, DECKS, PAD_MODES, OFF, DIM, ON } from "../src/vocab.js";

/** 8 modes x 2 decks x 8 pads x (unshifted + shifted), 2 decks x 9 buttons x 2 notes, FX ON/OFF x 2 */
const ledCount = 8 * 2 * 8 * 2 + 2 * 9 * 2 + 2;

class FakeLEDs {
    buttons = new Map();                         // control -> light
    pads = {};                                   // mode -> [[8], [8]]
    modes = ["hotCue", "hotCue"];
    lights() {
        const out = new Map(this.buttons);
        for (const d of DECKS) this.padLights(this.modes[d])[d].forEach((l, i) => out.set(C.pad(d, i), l));
        return out;
    }
    padLights(mode) { return this.pads[mode] ?? [new Array(8).fill(OFF), new Array(8).fill(OFF)]; }
    padMode(deck) { return this.modes[deck]; }
    setPad(mode, deck, pad, light) {
        const p = this.padLights(mode).map((row) => row.slice());
        p[deck][pad] = light;
        this.pads[mode] = p;
    }
}

const unlimited = () => { const w = new DDJ400LEDWriter(); w.maxPerCall = Infinity; return w; };
const keyOf = (x) => x.join(",");
const asSet = (list) => new Set(list.map(keyOf));
const BUTTON_CONTROLS = [C.play, C.cue, C.sync, C.loopIn, C.loopOut, C.reloop, C.callLeft, C.callRight, C.headphoneCue];

test("full send then diff", () => {
    const src = new FakeLEDs();
    const w = unlimited();
    const first = w.messages(src, [0, 0]);
    assert.equal(first.length, ledCount + 2);
    assert.ok(asSet(first).has("176,2,0") && asSet(first).has("177,2,0"));   // the meters, B0 / B1 02 00
    assert.deepStrictEqual(w.messages(src, [0, 0]), []);

    src.setPad("hotCue", 0, 0, ON);
    src.setPad("beatLoop", 0, 4, ON);
    src.setPad("beatLoop", 0, 6, DIM);                           // dim is sent as off: nothing changes
    src.buttons.set(C.cue(0), ON);
    src.buttons.set(C.cue(1), ON);
    src.buttons.set(C.play(0), DIM);
    const diff = asSet(w.messages(src, [0, 0]));
    const expected = asSet([
        [0x97, 0x00, 0x7F], [0x98, 0x00, 0x7F],                  // HOT CUE left pad 1, unshifted and shifted
        [0x97, 0x64, 0x7F], [0x98, 0x64, 0x7F],                  // BEAT LOOP left pad 5
        [0x90, 0x0C, 0x7F], [0x90, 0x48, 0x7F],                  // CUE both decks, unshifted 0x0C and shifted 0x48
        [0x91, 0x0C, 0x7F], [0x91, 0x48, 0x7F],
    ]);
    assert.deepStrictEqual(diff, expected);

    w.invalidate();
    assert.equal(w.messages(src, [0, 0]).length, ledCount + 2);
});

test("button notes", () => {
    const src = new FakeLEDs();
    for (const d of DECKS) for (const b of BUTTON_CONTROLS) src.buttons.set(b(d), ON);
    src.buttons.set(C.fxOnOff, ON);
    const all = asSet(unlimited().messages(src, [0, 0]));
    for (const status of [0x90, 0x91]) {
        for (const note of [0x0B, 0x0C, 0x58, 0x10, 0x11, 0x4D, 0x51, 0x53, 0x54, 0x47, 0x48, 0x60, 0x4C, 0x4E, 0x50,
                            0x3E, 0x3D, 0x68]) {
            assert.ok(all.has(keyOf([status, note, 0x7F])), `${status} ${note}`);
        }
    }
    assert.ok(all.has(keyOf([0x94, 0x47, 0x7F])));
    assert.ok(all.has(keyOf([0x94, 0x43, 0x7F])));
});

test("sampler pads: lit on, dim off, shifted alike", () => {
    const src = new FakeLEDs();
    src.setPad("sampler", 1, 6, ON);
    src.setPad("sampler", 1, 5, DIM);
    const all = asSet(unlimited().messages(src, [0, 0]));
    assert.ok(all.has(keyOf([0x99, 0x36, 0x7F])));
    assert.ok(all.has(keyOf([0x99, 0x35, 0x00])));
    assert.ok(all.has(keyOf([0x9A, 0x36, 0x7F])));
});

const amp = (db) => Math.pow(10, db / 20);

test("meter scale in dB", () => {
    assert.equal(DDJ400LEDWriter.meterValue(0, -48), 127);
    assert.equal(DDJ400LEDWriter.meterValue(-24, -48), 64);       // 63.5
    assert.equal(DDJ400LEDWriter.meterValue(-12, -48), 95);       // 95.25
    assert.equal(DDJ400LEDWriter.meterValue(-48, -48), 0);
    assert.equal(DDJ400LEDWriter.meterValue(-60, -48), 0);
    assert.equal(DDJ400LEDWriter.meterValue(-Infinity, -48), 0);
});

test("meters rise at once and decay", () => {
    const src = new FakeLEDs();
    const w = unlimited();
    let t = 10;
    w.now = () => t;
    w.messages(src, [0, 0]);
    assert.deepStrictEqual(w.messages(src, [1, amp(-12)]), [[0xB0, 0x02, 127], [0xB1, 0x02, 95]], "a peak shows at once");
    t += 0.4;   // silence: falls 25 dB/s, 0 -> -10 dB, -12 -> -22 dB
    assert.deepStrictEqual(w.messages(src, [0, 0]), [[0xB0, 0x02, 101], [0xB1, 0x02, 69]]);
    t += 0.01;  // -0.25 dB: less than 2 steps, nothing sent
    assert.deepStrictEqual(w.messages(src, [0, 0]), []);
    t += 0.04;  // -11.25 dB -> 97 (4 down from the 101 sent)
    assert.deepStrictEqual(w.messages(src, [0, 0])[0], [0xB0, 0x02, 97]);
    t += 2;     // below the floor: 0 is always sent
    assert.deepStrictEqual(w.messages(src, [0, 0]), [[0xB0, 0x02, 0], [0xB1, 0x02, 0]]);
    assert.deepStrictEqual(w.messages(src, [0, 0]), []);
    // a level above what is shown wins over the decay
    t += 0.1;
    assert.deepStrictEqual(w.messages(src, [amp(-12), 0]), [[0xB0, 0x02, 95]]);
});

test("all off", () => {
    const src = new FakeLEDs();
    for (const mode of PAD_MODES) for (const d of DECKS) for (let p = 0; p < 8; p++) src.setPad(mode, d, p, ON);
    for (const d of DECKS) for (const b of BUTTON_CONTROLS) src.buttons.set(b(d), ON);
    src.buttons.set(C.fxOnOff, ON);
    const w = unlimited();
    w.messages(src, [1, 1]);
    const off = w.allOff();
    assert.equal(off.length, ledCount + 2);
    assert.ok(off.every((x) => x[2] === 0));
    assert.equal(new Set(off.map((x) => (x[0] << 8) | x[1])).size, ledCount + 2);
    assert.ok(asSet(off).has(keyOf([0xB0, 0x02, 0])));
    assert.ok(asSet(off).has(keyOf([0xB1, 0x02, 0])));
    assert.ok(asSet(off).has(keyOf([0x9A, 0x77, 0])));
    // the sent state is off: a dark source needs nothing, one lit pad needs its two notes, a lit source more
    const dark = new FakeLEDs();
    assert.deepStrictEqual(w.messages(dark, [0, 0]), []);
    dark.setPad("sampler", 1, 4, ON);
    assert.deepStrictEqual(asSet(w.messages(dark, [0, 0])), asSet([[0x99, 0x34, 0x7F], [0x9A, 0x34, 0x7F]]));
    assert.ok(w.messages(src, [0, 0]).length > 0);
});

test("budget and order with a fake source", () => {
    const src = new FakeLEDs();
    src.modes = ["sampler", "hotCue"];
    for (const mode of PAD_MODES) for (const d of DECKS) for (let p = 0; p < 8; p++) src.setPad(mode, d, p, ON);
    const w = new DDJ400LEDWriter();
    assert.equal(w.maxPerCall, 24);
    let all = [];
    for (let i = 0; i < 20; i++) {
        const out = w.messages(src, [0, 0]).filter((x) => x[0] !== 0xB0 && x[0] !== 0xB1);
        assert.ok(out.length <= 24);
        all = all.concat(out);
    }
    assert.equal(all.length, ledCount, "everything sent in the end, once");
    assert.ok(all.slice(0, 38).every((x) => x[0] < 0x97), "buttons and FX first");
    // then the pads of the current modes (left SAMPLER 0x30, right HOT CUE 0x00, on any deck's status), then the rest
    const isCurrent = (x) => (x[1] & 0xF8) === 0x30 || (x[1] & 0xF8) === 0x00;
    const pads = all.slice(38);
    const firstOther = pads.findIndex((x) => !isCurrent(x));
    assert.equal(firstOther, 2 * 2 * 8 * 2, "both current modes' pads (2 modes x 2 decks x 8 x 2 notes) first");
    assert.ok(pads.slice(firstOther).every((x) => !isCurrent(x)));
});

test("DDJPadFunction", () => {
    assert.deepStrictEqual(DDJPadFunction.of("hotCue", 0, 0), { type: "key", key: 0 });
    assert.deepStrictEqual(DDJPadFunction.of("hotCue", 1, 7), { type: "key", key: FM1Keyboard.whiteKeys[15] });
    assert.deepStrictEqual(DDJPadFunction.of("hotCue", 0, 0, true), { type: "punch", key: 0 });
    assert.deepStrictEqual(DDJPadFunction.of("padFX1", 0, 1), { type: "punch", key: 2 });
    assert.deepStrictEqual(DDJPadFunction.of("beatLoop", 0, 4), { type: "key", key: 0 });          // F3
    assert.deepStrictEqual(DDJPadFunction.of("beatLoop", 0, 0), { type: "key", key: 1 });          // F#3
    assert.deepStrictEqual(DDJPadFunction.of("beatLoop", 0, 3), { type: "none" });                 // above B: none
    assert.deepStrictEqual(DDJPadFunction.of("sampler", 0, 2), { type: "key", key: FM1Keyboard.blackKeys[2] });
    assert.deepStrictEqual(DDJPadFunction.of("sampler", 1, 5), { type: "selectTrack", track: 1 });
    assert.deepStrictEqual(DDJPadFunction.of("padFX2", 1, 5), { type: "none" });
    assert.deepStrictEqual(DDJPadFunction.of("sampler", 1, 3), { type: "none" });
    assert.deepStrictEqual(DDJPadFunction.of("hotCue", 0, 8), { type: "none" });
    assert.equal(DDJPadFunction.key({ type: "punch", key: 5 }), 5);
    assert.equal(DDJPadFunction.key({ type: "selectTrack", track: 1 }), null);
    assert.equal(FM1Keyboard.whiteKeys.length, 16);
    assert.equal(FM1Keyboard.blackKeys.length, 11);
});
