// SPDX-License-Identifier: GPL-3.0-only
// DDJ400Decoder (a port of Tests/DDJMappingTests/DDJ400DecoderTests.swift). Run: node --test web/test/
import { test } from "node:test";
import assert from "node:assert/strict";
import { DDJ400Decoder } from "../src/mapping/index.js";
import { C, PAD_MODES, PAD_MODE_OFFSET, ev, press, absolute, relative } from "../src/vocab.js";

const decode = (s, d1, d2) => new DDJ400Decoder().decode([s, d1, d2]);

test("deck buttons and shifted variants", () => {
    const cases = [
        [0x0B, C.play(0), false], [0x47, C.play(0), true],
        [0x0C, C.cue(0), false], [0x48, C.cue(0), true],
        [0x58, C.sync(0), false], [0x60, C.sync(0), true],
        [0x10, C.loopIn(0), false], [0x4C, C.loopIn(0), true],
        [0x11, C.loopOut(0), false], [0x4E, C.loopOut(0), true],
        [0x4D, C.reloop(0), false], [0x50, C.reloop(0), true],
        [0x51, C.callLeft(0), false], [0x3E, C.callLeft(0), true],
        [0x53, C.callRight(0), false], [0x3D, C.callRight(0), true],
        [0x54, C.headphoneCue(0), false], [0x68, C.headphoneCue(0), true],
        [0x36, C.jogTouch(0), false], [0x67, C.jogTouch(0), true],
        [0x3F, C.shift(0), false],
    ];
    for (const [note, control, shifted] of cases) {
        assert.deepEqual(decode(0x90, note, 0x7F), ev(control, press(true), { shifted }), `note ${note}`);
    }
    assert.deepEqual(decode(0x91, 0x0B, 0x7F), ev(C.play(1), press(true)));
    assert.deepEqual(decode(0x91, 0x54, 0x7F), ev(C.headphoneCue(1), press(true)));
});

test("release by velocity zero and note off", () => {
    assert.deepEqual(decode(0x90, 0x0B, 0x00), ev(C.play(0), press(false)));
    assert.deepEqual(decode(0x81, 0x0B, 0x40), ev(C.play(1), press(false)));
    assert.deepEqual(decode(0x87, 0x05, 0x00), ev(C.pad(0, 5), press(false), { padMode: "hotCue" }));
});

test("pad mode buttons", () => {
    assert.deepEqual(decode(0x90, 0x1B, 0x7F), ev(C.padModeButton(0, "hotCue"), press(true)));
    assert.deepEqual(decode(0x90, 0x6D, 0x7F), ev(C.padModeButton(0, "beatLoop"), press(true)));
    assert.deepEqual(decode(0x91, 0x20, 0x7F), ev(C.padModeButton(1, "beatJump"), press(true)));
    assert.deepEqual(decode(0x91, 0x22, 0x00), ev(C.padModeButton(1, "sampler"), press(false)));
    assert.deepEqual(decode(0x90, 0x69, 0x7F), ev(C.padModeButton(0, "keyboard"), press(true), { shifted: true }));
    assert.deepEqual(decode(0x90, 0x1E, 0x7F), ev(C.padModeButton(0, "padFX1"), press(true), { shifted: true }));
    assert.deepEqual(decode(0x90, 0x6B, 0x7F), ev(C.padModeButton(0, "padFX2"), press(true), { shifted: true }));
    assert.deepEqual(decode(0x90, 0x6F, 0x7F), ev(C.padModeButton(0, "keyShift"), press(true), { shifted: true }));
});

test("sync long press is sync", () => {
    assert.deepEqual(decode(0x90, 0x5C, 0x7F), ev(C.sync(0), press(true)));
    assert.deepEqual(decode(0x91, 0x5C, 0x00), ev(C.sync(1), press(false)));
    assert.deepEqual(decode(0x81, 0x5C, 0x40), ev(C.sync(1), press(false)));
});

test("reconnect forgets halves", () => {
    const d = new DDJ400Decoder();
    d.decode([0xB0, 0x13, 0x40]);
    d.decode([0xB0, 0x33, 0x7F]);
    // a new decoder (what the app makes on a connect) and reset() start from nothing
    const fresh = new DDJ400Decoder();
    assert.deepEqual(fresh.decode([0xB0, 0x13, 0x20]), ev(C.channelFader(0), absolute((0x20 << 7) / 16383)),
                     "nothing of the old decoder");
    assert.equal(fresh.decode([0xB1, 0x33, 0x10]), null, "an LSB before its MSB");
    assert.equal(fresh.decode([0xB1, 0x13, 0x20]), null, "then the MSB waits for its LSB");
    assert.deepEqual(fresh.decode([0xB1, 0x33, 0x11]), ev(C.channelFader(1), absolute(((0x20 << 7) | 0x11) / 16383)));
    // reset(): the MSB is not held back by an LSB from before the reconnect
    d.reset();
    assert.deepEqual(d.decode([0xB0, 0x13, 0x20]), ev(C.channelFader(0), absolute((0x20 << 7) / 16383)), "no stale LSB");
    d.reset();
    assert.equal(d.decode([0xB0, 0x33, 0x01]), null, "no stale MSB");
});

test("beat FX section", () => {
    assert.deepEqual(decode(0x94, 0x4A, 0x7F), ev(C.beatLeft, press(true)));
    assert.deepEqual(decode(0x94, 0x4B, 0x7F), ev(C.beatRight, press(true)));
    assert.deepEqual(decode(0x94, 0x63, 0x7F), ev(C.fxSelect, press(true)));
    assert.deepEqual(decode(0x94, 0x64, 0x7F), ev(C.fxSelect, press(true), { shifted: true }));
    assert.deepEqual(decode(0x94, 0x47, 0x7F), ev(C.fxOnOff, press(true)));
    assert.deepEqual(decode(0x94, 0x43, 0x7F), ev(C.fxOnOff, press(true), { shifted: true }));
    // the channel switch: a note-on at each position (CH1, CH2, MASTER); its note-off says nothing
    assert.deepEqual(decode(0x94, 0x10, 0x7F), ev(C.fxChannel(0), press(true)));
    assert.deepEqual(decode(0x94, 0x11, 0x7F), ev(C.fxChannel(1), press(true)));
    assert.deepEqual(decode(0x94, 0x14, 0x7F), ev(C.fxChannel(2), press(true)));
    assert.equal(decode(0x94, 0x14, 0x00), null);
});

test("browse and load", () => {
    assert.deepEqual(decode(0x96, 0x41, 0x7F), ev(C.browsePress, press(true)));
    assert.deepEqual(decode(0x96, 0x42, 0x7F), ev(C.browsePress, press(true), { shifted: true }));
    assert.deepEqual(decode(0x96, 0x46, 0x7F), ev(C.load(0), press(true)));
    assert.deepEqual(decode(0x96, 0x47, 0x7F), ev(C.load(1), press(true)));
    assert.deepEqual(decode(0xB6, 0x40, 0x01), ev(C.browse, relative(1)));
    assert.deepEqual(decode(0xB6, 0x40, 0x03), ev(C.browse, relative(3)));
    assert.deepEqual(decode(0xB6, 0x40, 0x7F), ev(C.browse, relative(-1)));
    assert.deepEqual(decode(0xB6, 0x40, 0x7D), ev(C.browse, relative(-3)));
});

test("pads", () => {
    for (const mode of PAD_MODES) {
        for (let pad = 0; pad < 8; pad++) {
            const note = PAD_MODE_OFFSET[mode] + pad;
            assert.deepEqual(decode(0x97, note, 0x7F), ev(C.pad(0, pad), press(true), { padMode: mode }));
            assert.deepEqual(decode(0x98, note, 0x7F), ev(C.pad(0, pad), press(true), { padMode: mode, shifted: true }));
            assert.deepEqual(decode(0x99, note, 0x7F), ev(C.pad(1, pad), press(true), { padMode: mode }));
            assert.deepEqual(decode(0x9A, note, 0x7F), ev(C.pad(1, pad), press(true), { padMode: mode, shifted: true }));
        }
        assert.equal(decode(0x97, PAD_MODE_OFFSET[mode] + 8, 0x7F), null);
    }
});

test("jogs", () => {
    assert.deepEqual(decode(0xB0, 0x22, 65), ev(C.jog(0), relative(1)));
    assert.deepEqual(decode(0xB0, 0x23, 60), ev(C.jog(0), relative(-4)));
    assert.deepEqual(decode(0xB1, 0x21, 66), ev(C.jog(1), relative(2)));
    assert.deepEqual(decode(0xB1, 0x29, 63), ev(C.jog(1), relative(-1), { shifted: true }));
});

test("fourteen-bit pairing", () => {
    const d = new DDJ400Decoder();
    // MSB first, no LSB yet: msb << 7
    assert.deepEqual(d.decode([0xB0, 0x13, 0x40]), ev(C.channelFader(0), absolute((0x40 << 7) / 16383)));
    assert.deepEqual(d.decode([0xB0, 0x33, 0x10]), ev(C.channelFader(0), absolute(((0x40 << 7) | 0x10) / 16383)));
    // once the control has sent an LSB, an MSB waits for its LSB (no jump back with a stale LSB), and the pair gives
    // the value
    assert.equal(d.decode([0xB0, 0x13, 0x41]), null);
    assert.deepEqual(d.decode([0xB0, 0x33, 0x02]), ev(C.channelFader(0), absolute(((0x41 << 7) | 0x02) / 16383)));
    // a fader moving one way never reads backwards (MSB then LSB, the way the DDJ-400 sends them)
    let last = -1;
    for (let v = 0x41 << 7; v <= 0x48 << 7; v += 37) {
        d.decode([0xB0, 0x13, v >> 7]);
        const e = d.decode([0xB0, 0x33, v & 0x7F]);
        assert.ok(e && "absolute" in e.value, "the LSB completes the pair");
        assert.ok(e.value.absolute > last);
        last = e.value.absolute;
    }
    assert.equal(d.decode([0xB0, 0x13, 0x7F]), null);
    assert.equal(d.decode([0xB0, 0x33, 0x10]).value.absolute, ((0x7F << 7) | 0x10) / 16383);
    // an LSB without an MSB yet: nothing
    assert.equal(d.decode([0xB1, 0x33, 0x10]), null);
    // full scale
    d.decode([0xB6, 0x1F, 0x7F]);
    assert.deepEqual(d.decode([0xB6, 0x3F, 0x7F]), ev(C.crossfader, absolute(1)));
});

test("absolute control families", () => {
    const cases = [
        [0xB0, 0x13, C.channelFader(0)], [0xB1, 0x13, C.channelFader(1)],
        [0xB0, 0x00, C.tempo(0)], [0xB1, 0x00, C.tempo(1)],
        [0xB0, 0x04, C.trim(0)], [0xB1, 0x04, C.trim(1)],
        [0xB0, 0x07, C.eqHi(0)], [0xB1, 0x0B, C.eqMid(1)], [0xB0, 0x0F, C.eqLow(0)],
        [0xB6, 0x1F, C.crossfader], [0xB6, 0x17, C.cfx(0)], [0xB6, 0x18, C.cfx(1)],
        [0xB6, 0x0D, C.headphoneLevel], [0xB4, 0x02, C.fxLevel],
    ];
    for (const [s, cc, control] of cases) {
        const d = new DDJ400Decoder();
        assert.deepEqual(d.decode([s, cc, 0]), ev(control, absolute(0)));
        assert.equal(d.decode([s, cc + 0x20, 0x7F])?.control, control);
    }
});

test("unknown messages", () => {
    assert.equal(decode(0x90, 0x01, 0x7F), null);
    assert.equal(decode(0x92, 0x0B, 0x7F), null);
    assert.equal(decode(0xB0, 0x02, 0x10), null);
    assert.equal(decode(0xB6, 0x40, 0x00), null);
    assert.equal(decode(0xE0, 0x00, 0x40), null);
    assert.equal(decode(0x96, 0x10, 0x7F), null);
    assert.equal(decode(0xB5, 0x13, 0x10), null);
});

test("a Web MIDI message (Uint8Array) decodes too", () => {
    assert.deepEqual(new DDJ400Decoder().decode(new Uint8Array([0x90, 0x0B, 0x7F])), ev(C.play(0), press(true)));
});
