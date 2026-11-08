// SPDX-License-Identifier: GPL-3.0-only
// The page's model of the engine (src/engine.js) read from real snapshots: one engine instance plays the audio
// thread's part, another the silent helper. Checked against what the default project holds (td_default.c).
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { instantiate } from "../src/wasi.js";
import { TrigWebEngine } from "../src/engine.js";

const bytes = readFileSync(new URL("../trigcore.wasm", import.meta.url));

function setup() {
    const audio = instantiate(bytes);
    audio.tdw_init();
    const m = new TrigWebEngine();
    m.helper = instantiate(bytes);
    m.helper.tdw_init();
    const snap = (full) => {
        const all = new Int32Array(audio.memory.buffer, audio.tdw_snapshot(full), audio.tdw_snapshot_len(full)).slice();
        m.light = all.slice(0, audio.tdw_snapshot_len(0));
        if (full) m.heavy = all;
    };
    snap(1);
    // apply() posts calls to a worklet: here they run on the audio instance directly
    m.node = { port: { postMessage: (msg) => { for (const [n, ...a] of msg.calls || []) audio[n](...a); snap(1); } } };
    return { audio, m, snap };
}

test("header and tracks: P01 deep house at 122 BPM", () => {
    const { m } = setup();
    assert.equal(m.pattern, 0);
    assert.equal(m.bpm, 122);
    assert.equal(m.playing, false);
    assert.equal(m.queuedPattern, null);
    assert.equal(m.machine(0), 0, "track 1: KICK");
    assert.equal(m.length(4), 64, "the bass runs over 4 bars");
    assert.equal(m.step(0, 0).trig, true, "four on the floor");
    assert.equal(m.step(0, 2).trig, false);
    assert.equal(m.playingStep(0), null, "stopped");
});

test("parameter descriptions and formatting come from the helper", () => {
    const { m } = setup();
    const cut = m.info(4, 16);                                 // FLTR page slot 0
    assert.equal(cut.name, "CUTOFF");
    assert.equal(cut.shortName, "CUT");
    assert.match(m.format(cut, m.liveValue(4, 16)), /Hz/);
    assert.equal(m.info(0, 8).name, "TUNE", "a KICK's SRC page starts with TUNE");
    assert.equal(m.globalInfo(0).name, "DLY TIME");
    assert.equal(m.condName(1), "FILL");
    assert.equal(m.machineName(5), "SYNTH");
    assert.ok(m.packNames === undefined || Array.isArray(m.packNames));
});

test("locks and scenes: P01's chord stabs lock their chord type; scene A filters the bass", () => {
    const { m } = setup();
    assert.equal(m.lock(5, 2, 8 + 6), 3, "bar 1's stab: MIN9");
    assert.equal(m.lock(5, 34, 8 + 6), 2, "bar 3's: MAJ7");
    assert.equal(m.scene(0, 4, 16), 26, "scene A: the bass cutoff down");
    assert.equal(m.scene(1, 7, 16), null);
});

test("actions reach the engine and the snapshot follows", () => {
    const { m } = setup();
    m.apply([{ type: "stepSet", track: 0, step: 2, on: true }, { type: "tempo", bpm: 128 }, { type: "togglePlay" }]);
    assert.equal(m.step(0, 2).trig, true);
    assert.equal(m.bpm, 128);
    assert.equal(m.playing, true);
    m.apply([{ type: "length", track: 3, steps: 32 }, { type: "queuePattern", pattern: 12 }]);
    assert.equal(m.length(3), 32);
    assert.equal(m.queuedPattern, 12);
});
