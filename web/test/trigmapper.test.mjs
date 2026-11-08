// SPDX-License-Identifier: GPL-3.0-only
// TrigMapper (TRIGDECK on the DDJ-400) against a fake engine model (a port of
// Tests/DDJMappingTests/TrigMapperTests.swift). Run: node --test web/test/
import { test } from "node:test";
import assert from "node:assert/strict";
import { TrigMapper, DDJ400LEDWriter, FM1Keyboard } from "../src/mapping/index.js";
import { C, ev, press, absolute, relative, OFF, DIM, ON, PAGE, MACHINE, COLORS, midiNoteName, PERF_FX, PERF_MENU } from "../src/vocab.js";

const stepView = (o = {}) => ({ trig: false, note: 60, velocity: 100, cond: 0, retrig: 0, micro: 0, locks: 0,
                                slide: false, accent: false, ...o });

class FakeTrig {
    playing = false; recording = false; pattern = 0;
    queuedPattern = null;
    bpm = 120; swing = 50;
    autopilot = false;
    machines = [MACHINE.kick, MACHINE.snare, MACHINE.hat, MACHINE.clap, MACHINE.synth, MACHINE.fm, MACHINE.sample,
                MACHINE.tom];
    mutes = new Set();
    lengths = new Array(8).fill(16);
    playhead = null;
    values = new Map();          // track * 100 + param
    globals = new Map();
    steps = new Map();           // track * 100 + step
    locks = new Map();           // (track * 100 + step) * 100 + param
    scenes = new Map();          // scene * 10000 + track * 100 + param
    packNames = ["808", "909", "KEYS"];
    condCount = 17;

    machine(t) { return this.machines[t]; }
    muted(t) { return this.mutes.has(t); }
    length(t) { return this.lengths[t]; }
    playingStep(t) { return this.playhead; }
    info(track, param) {
        return param === 21 ? { name: "TYPE", shortName: "TYPE", min: 0, max: 2, def: 0, lockable: true }
            : { name: `P${param}`, shortName: `P${param}`, min: 0, max: 127, def: 64, lockable: param !== 9 };
    }
    globalInfo(slot) { return { name: `G${slot}`, shortName: `G${slot}`, min: 0, max: 100, def: 50, lockable: true }; }
    value(track, param) { return this.values.get(track * 100 + param) ?? 64; }
    liveValue(track, param) { return this.value(track, param); }
    globalValue(slot) { return this.globals.get(slot) ?? 50; }
    format(info, v) { return `${v}`; }
    step(track, step) { return this.steps.get(track * 100 + step) ?? stepView(); }
    lock(track, step, param) { return this.locks.get((track * 100 + step) * 100 + param) ?? null; }
    scene(s, track, param) { return this.scenes.get(s * 10000 + track * 100 + param) ?? null; }
    condName(c) { return c === 0 ? "ALWAYS" : `C${c}`; }
    machineName(m) { return ["KICK", "SNARE", "HAT", "CLAP", "TOM", "SYNTH", "FM", "SAMPLE"][m]; }
    trackLevel(t) { return 0; }

    #edit(t, s, f) { const v = { ...this.step(t, s) }; f(v); this.steps.set(t * 100 + s, v); }

    /** apply the actions the way the engine would, so a sequence of events sees its own effects */
    apply(actions) {
        for (const a of actions) {
            switch (a.type) {
            case "stepSet": this.#edit(a.track, a.step, (v) => { v.trig = a.on; }); break;
            case "stepLock":
                this.locks.set((a.track * 100 + a.step) * 100 + a.param, a.value);
                this.#edit(a.track, a.step, (v) => { v.locks += 1; });
                break;
            case "stepMicro": this.#edit(a.track, a.step, (v) => { v.micro = a.micro; }); break;
            case "stepCond": this.#edit(a.track, a.step, (v) => { v.cond = a.cond; }); break;
            case "paramSet": this.values.set(a.track * 100 + a.param, a.value); break;
            case "sceneSet": this.scenes.set(a.scene * 10000 + a.track * 100 + a.param, a.value); break;
            case "mute": if (a.on) this.mutes.add(a.track); else this.mutes.delete(a.track); break;
            case "globalSet": this.globals.set(a.slot, a.value); break;
            case "tempo": this.bpm = a.bpm; break;
            case "swing": this.swing = a.swing; break;
            case "machine": this.machines[a.track] = a.machine; break;
            case "stepSlide": this.#edit(a.track, a.step, (v) => { v.slide = a.on; }); break;
            case "stepAccent": this.#edit(a.track, a.step, (v) => { v.accent = a.on; }); break;
            case "autopilot": this.autopilot = a.on; break;
            default: break;
            }
        }
    }
}

/** a fresh model, mapper and clock for each test */
function setUp() {
    const model = new FakeTrig();
    const m = new TrigMapper(model);
    const clock = { t: 100 };
    m.now = () => clock.t;
    const send = (c, v, { mode = null, shifted = false } = {}) => {
        const a = m.handle(ev(c, v, { padMode: mode, shifted }));
        model.apply(a);
        return a;
    };
    const tap = (c, opts = {}) => [...send(c, press(true), opts), ...send(c, press(false), opts)];
    return { model, m, clock, send, tap };
}

const near = (a, b, eps, msg) => assert.ok(Math.abs(a - b) <= eps, `${msg ?? ""} ${a} != ${b}`);
const stepSet = (track, step, on) => ({ type: "stepSet", track, step, on });

// MARK: steps

test("tap sets then clears a step", () => {
    const { tap } = setUp();
    assert.deepStrictEqual(tap(C.pad(0, 4), { mode: "hotCue" }), [stepSet(0, 4, true)]);
    assert.deepStrictEqual(tap(C.pad(0, 4), { mode: "hotCue" }), [stepSet(0, 4, false)]);
    assert.deepStrictEqual(tap(C.pad(1, 0), { mode: "hotCue" }), [stepSet(0, 8, true)], "right deck: steps 9-16");
});

test("hold step and turn knob locks without clearing", () => {
    const { model, send, tap } = setUp();
    tap(C.pad(0, 2), { mode: "hotCue" });                            // a trig on step 3
    send(C.pad(0, 2), press(true), { mode: "hotCue" });
    send(C.eqHi(0), absolute(0.5));                                   // (reference)
    const a = send(C.eqHi(0), absolute(0.6));                         // +0.1 of 127
    assert.deepStrictEqual(a, [{ type: "stepLock", track: 0, step: 2, param: PAGE.src * 8, value: 64 + 12 }]);
    assert.deepStrictEqual(send(C.pad(0, 2), press(false), { mode: "hotCue" }), [],
                           "an edited step is not cleared on release");
    assert.ok(model.step(0, 2).trig);
});

test("knob without held step sets the base value", () => {
    const { send } = setUp();
    send(C.cfx(1), absolute(0.2));
    assert.deepStrictEqual(send(C.cfx(1), absolute(0)), [{ type: "paramSet", track: 0, param: 8 + 7, value: 64 - 25 }]);
});

test("full turn covers a small range exactly", () => {
    const { send } = setUp();
    send(C.loopIn(0), press(true));                                   // TRIG page
    send(C.reloop(0), press(true));                                   // FLTR page (2): slot 5 = param 21 (0...2)
    send(C.eqMid(1), absolute(0));
    assert.deepStrictEqual(send(C.eqMid(1), absolute(1)), [{ type: "paramSet", track: 0, param: 21, value: 2 }],
                           "the value clamps to the range");
});

test("jog micro and browse condition on held step", () => {
    const { model, clock, send } = setUp();
    send(C.pad(1, 3), press(true), { mode: "hotCue" });               // step 12 (set on press)
    clock.t += 0.01;
    assert.deepStrictEqual(send(C.jog(0), relative(8)), [{ type: "stepMicro", track: 0, step: 11, micro: 2 }]);
    assert.deepStrictEqual(send(C.browse, relative(1)), [{ type: "stepCond", track: 0, step: 11, cond: 1 }]);
    assert.deepStrictEqual(send(C.browse, relative(-2)), [{ type: "stepCond", track: 0, step: 11, cond: 16 }], "wraps");
    send(C.pad(1, 3), press(false), { mode: "hotCue" });
    assert.ok(model.step(0, 11).trig);
});

test("step pages and length", () => {
    const { model, m, send, tap } = setUp();
    model.lengths[0] = 64;
    send(C.callRight(1), press(true));
    assert.deepStrictEqual(tap(C.pad(0, 0), { mode: "hotCue" }), [stepSet(0, 16, true)]);
    for (let i = 0; i < 5; i++) send(C.callRight(1), press(true));
    assert.equal(m.stepPage, 3, "clamped to the last page");
    model.lengths[0] = 16;
    assert.equal(m.face(C.pad(0, 0)).title, "", "beyond the track's length: blank");
});

// MARK: modes

test("tracks mode selects and mutes", () => {
    const { m, tap } = setUp();
    assert.deepStrictEqual(tap(C.pad(1, 1), { mode: "beatJump" }), [{ type: "selectTrack", track: 5 }]);
    assert.equal(m.track, 5);
    assert.deepStrictEqual(tap(C.pad(1, 5), { mode: "beatJump" }), [{ type: "mute", track: 5, on: true }]);
    assert.deepStrictEqual(tap(C.pad(1, 5), { mode: "beatJump" }), [{ type: "mute", track: 5, on: false }]);
    assert.equal(m.face(C.pad(1, 1)).light, ON);
    assert.equal(m.padFunction(1), "tracks");
    assert.equal(m.padFunction(0), "steps", "each deck keeps its own mode");
});

test("a double-click resets a knob to its default", () => {
    const { m, model, send } = setUp();
    const p = m.page * 8;
    assert.deepStrictEqual(m.reset(C.eqHi(0)), [{ type: "paramSet", track: 0, param: p, value: model.info(0, p).def }]);
    assert.deepStrictEqual(m.reset(C.trim(0)), [], "not a page knob");
    send(C.fxLevel, absolute(0.9));
    assert.deepStrictEqual(m.reset(C.fxLevel), []);
    assert.equal(m.perfAmount, 0.6);
    send(C.pad(0, 2), press(true), { mode: "hotCue" });                  // a held step: its lock back to the track's value
    assert.deepStrictEqual(m.reset(C.eqHi(0)), [{ type: "stepLock", track: 0, step: 2, param: p, value: model.value(0, p) }]);
});

test("a mute pad held is temporary", () => {
    const { model, clock, send } = setUp();
    assert.deepStrictEqual(send(C.pad(1, 5), press(true), { mode: "beatJump" }), [{ type: "mute", track: 5, on: true }]);
    clock.t += 1;
    assert.deepStrictEqual(send(C.pad(1, 5), press(false), { mode: "beatJump" }), [{ type: "mute", track: 5, on: false }]);
    assert.equal(model.muted(5), false);
});

test("solo, solo hopping, and back to the mutes from before", () => {
    const { m, model, clock, send, tap } = setUp();
    const on = () => [0, 1, 2, 3, 4, 5, 6, 7].filter((t) => !model.muted(t));
    const off = () => [0, 1, 2, 3, 4, 5, 6, 7].filter((t) => model.muted(t));
    tap(C.pad(0, 6), { mode: "beatJump" });                              // track 3 muted first
    tap(C.pad(1, 4), { mode: "beatJump", shifted: true });               // solo track 5
    assert.deepStrictEqual(on(), [4]);
    assert.equal(m.soloed(4), true);
    assert.equal(m.face(C.pad(1, 4)).title, "SOLO");
    tap(C.pad(0, 4), { mode: "beatJump", shifted: true });               // to track 1
    assert.deepStrictEqual(on(), [0]);
    tap(C.pad(0, 4), { mode: "beatJump", shifted: true });               // again: back
    assert.deepStrictEqual(off(), [2]);
    send(C.pad(1, 7), press(true), { mode: "beatJump", shifted: true }); // held: a solo while held
    assert.deepStrictEqual(on(), [7]);
    clock.t += 1;
    send(C.pad(1, 7), press(false), { mode: "beatJump", shifted: true });
    assert.deepStrictEqual(off(), [2]);
});

test("keys mode, melodic and drums", () => {
    const { model, send } = setUp();
    for (let i = 0; i < 4; i++) send(C.callRight(0), press(true));    // track 5: SYNTH
    model.values.set(4 * 100 + 0, 48);
    assert.deepStrictEqual(send(C.pad(0, 4), press(true), { mode: "beatLoop" }),
                           [{ type: "trigger", track: 4, note: 48, velocity: 100 }]);
    assert.deepStrictEqual(send(C.pad(0, 4), press(false), { mode: "beatLoop" }), [{ type: "release", track: 4, note: 48 }]);
    assert.deepStrictEqual(send(C.pad(1, 0), press(true), { mode: "beatLoop" }),
                           [{ type: "trigger", track: 4, note: 48 + 12, velocity: 100 }]);
    // a drum track active: a deck's pads play its own 4 tracks
    for (let i = 0; i < 4; i++) send(C.callLeft(0), press(true));
    assert.deepStrictEqual(send(C.pad(1, 6), press(true), { mode: "beatLoop" }),
                           [{ type: "trigger", track: 6, note: 60, velocity: 55 }]);
});

test("keys faces for low notes", () => {
    const { model, m } = setUp();
    model.machines[0] = MACHINE.synth;
    model.values.set(0, 33);                                          // A1
    m.setPadMode("beatLoop", 0);
    assert.equal(m.face(C.pad(0, 4)).title, "A1");
    model.values.set(0, 0);
    m.padLights("beatLoop");                                          // every pad, every note: no throw
    assert.equal(midiNoteName(60), "C4");
    assert.equal(FM1Keyboard.noteName(0), "F3");
});

test("patterns mode queues and copies", () => {
    const { send, tap } = setUp();
    assert.deepStrictEqual(tap(C.pad(1, 2), { mode: "sampler" }), [{ type: "queuePattern", pattern: 10 }]);
    assert.deepStrictEqual(tap(C.pad(0, 7), { mode: "sampler", shifted: true }), [{ type: "copyPattern", from: 0, to: 7 }]);
    assert.deepStrictEqual(send(C.load(1), press(true)), [{ type: "queuePattern", pattern: 1 }]);
});

// MARK: pages, scenes, master

test("pages from loop buttons", () => {
    const { m, send } = setUp();
    send(C.loopOut(1), press(true));
    assert.equal(m.page, PAGE.fx);
    assert.equal(m.face(C.loopOut(1)).light, ON);
    send(C.sync(1), press(true));
    assert.equal(m.page, PAGE.master);
    send(C.eqHi(0), absolute(0.5));
    assert.deepStrictEqual(send(C.eqHi(0), absolute(0.6)), [{ type: "globalSet", slot: 0, value: 60 }]);
    send(C.sync(1), press(true));
    assert.equal(m.page, PAGE.fx, "MASTER again: back to the page before");
});

test("scene edit while CUE held, and clear", () => {
    const { m, send } = setUp();
    send(C.headphoneCue(1), press(true));
    send(C.eqLow(0), absolute(0.5));
    assert.deepStrictEqual(send(C.eqLow(0), absolute(0.4)),
                           [{ type: "sceneSet", scene: 1, track: 0, param: 10, value: 64 - 12 }],
                           "-12.7: 12 now, the 0.7 waits for the next movement");
    assert.equal(m.face(C.eqLow(0)).badge, "B");
    send(C.headphoneCue(1), press(false));
    assert.equal(m.sceneEdit, null);
    assert.deepStrictEqual(send(C.headphoneCue(0), press(true), { shifted: true }), [{ type: "sceneClear", scene: 0 }]);
    assert.deepStrictEqual(send(C.crossfader, absolute(0.25)), [{ type: "crossfader", value: 0.25 }]);
});

// MARK: jogs, tempo, perf FX

test("scratch follows the platter and holds still", () => {
    const { m, clock, send } = setUp();
    assert.deepStrictEqual(send(C.jogTouch(0), press(true)), [{ type: "scratch", deck: 0, touching: true, rate: 0 }]);
    clock.t += 0.01;
    const a = send(C.jog(0), relative(4));                            // 400 ticks/s = normal speed
    assert.equal(a.length, 1);
    assert.equal(a[0].type, "scratch");
    assert.equal(a[0].deck, 0);
    assert.equal(a[0].touching, true);
    near(a[0].rate, 0.5, 0.01, "smoothed towards 1.0");
    clock.t += 0.2;
    assert.deepStrictEqual(m.tick(), [{ type: "scratch", deck: 0, touching: true, rate: 0 }], "no movement: held still");
    assert.deepStrictEqual(send(C.jogTouch(0), press(false)), [{ type: "scratch", deck: 0, touching: false, rate: 0 }]);
});

test("jog side is a dial around the deck's tracks", () => {
    const { m, clock, send } = setUp();
    const sel = (track) => [{ type: "selectTrack", track }];
    clock.t += 0.01;
    assert.equal(m.jogDialPosition(0), 0, "the marker starts on track 1's centre");
    assert.deepStrictEqual(send(C.jog(0), relative(90)), [], "a quarter turn is a track: to the edge, nothing yet");
    assert.equal(m.jogDialPosition(0), 0.5);
    assert.deepStrictEqual(send(C.jog(0), relative(6)), [], "the edge (in the gap between quadrants): still nothing");
    assert.deepStrictEqual(send(C.jog(0), relative(1)), sel(1), "past it: the next quadrant's track");
    near(m.jogDialPosition(0), 1 + (97 - 180) / 180, 1e-9, "the marker did not jump");
    assert.deepStrictEqual(send(C.jog(0), relative(-5)), [], "back a little: no flicker at the edge");
    assert.deepStrictEqual(send(C.jog(0), relative(-10)), sel(0), "back past the edge: track 1 again");
    assert.deepStrictEqual(send(C.jog(0), relative(-180)), sel(3), "around the ring: 1 -> 4");
    near(m.jogDialPosition(0), 3 + 82 / 180, 1e-9, "a whole quarter turn back, same place");
    // a track chosen another way: the marker goes to its quadrant's centre
    m.select(2);
    assert.equal(m.jogDialPosition(0), 2);
    // the right jog: deck B's tracks; while the active track is on deck A, a short turn brings it to deck B
    assert.deepStrictEqual(send(C.jog(1), relative(10)), []);
    assert.deepStrictEqual(send(C.jog(1), relative(14)), sel(4));
    assert.equal(m.face(C.jog(1)).value, "5 SYNTH");
    assert.equal(m.face(C.jog(0)).value, "TURN: TRACK");
    assert.equal(m.jogDialPosition(0), 2, "deck A's marker stays where it was left");
});

test("track length by steps", () => {
    const { model, m, send } = setUp();
    const len = (steps) => [{ type: "length", track: 0, steps }];
    const shifted = { shifted: true };
    // SHIFT + PAGE +: a bar longer (the pads go to the new bar); SHIFT + PAGE -: a bar shorter
    assert.deepStrictEqual(send(C.callRight(1), press(true), shifted), len(32));
    assert.equal(m.stepPage, 1, "the pads show the new bar");
    model.lengths[0] = 32;
    assert.deepStrictEqual(send(C.callRight(1), press(true), shifted), len(48));
    model.lengths[0] = 64;
    assert.deepStrictEqual(send(C.callRight(1), press(true), shifted), [], "64 at most");
    assert.deepStrictEqual(send(C.callLeft(1), press(true), shifted), len(48));
    model.lengths[0] = 37;
    assert.deepStrictEqual(send(C.callLeft(1), press(true), shifted), len(32), "to the bar below");
    // SHIFT + BROWSE: one step
    model.lengths[0] = 16;
    assert.deepStrictEqual(send(C.browse, relative(-4), shifted), len(12));
    assert.ok(!send(C.browse, relative(1)).some((a) => a.type === "length"),
              "without SHIFT: BROWSE's page job, not the length");
    // without SHIFT: PAGE - / + only move the pads' page
    assert.deepStrictEqual(send(C.callLeft(1), press(true)), []);
    assert.equal(m.lengthChanged, 100);
});

test("packs on the MASTER page", () => {
    const { m, send } = setUp();
    send(C.sync(1), press(true)); send(C.sync(1), press(false));     // the MASTER page
    assert.equal(m.page, PAGE.master);
    assert.equal(m.face(C.browse).title, "PACK");
    assert.equal(m.face(C.browse).value, "808");
    assert.deepStrictEqual(send(C.browse, relative(1)), [], "turning only chooses");
    assert.equal(m.face(C.browse).value, "909");
    assert.deepStrictEqual(send(C.browse, relative(-2)), []);
    assert.equal(m.face(C.browse).value, "KEYS", "around the list");
    assert.equal(m.packCursor, 2);
    assert.equal(m.face(C.browsePress).title, "LOAD PACK");
    assert.deepStrictEqual(send(C.browsePress, press(true)), [{ type: "loadPack", pack: 2 }]);
    assert.deepStrictEqual(send(C.browsePress, press(false)), []);
    assert.equal(m.packLoaded?.index, 2);
    assert.deepStrictEqual(send(C.browsePress, press(true), { shifted: true }), [{ type: "revertPack" }]);
    assert.equal(m.packLoaded, null);
    assert.equal(m.packReverted, 100);
    // other pages: BROWSE is the track again
    send(C.sync(1), press(true)); send(C.sync(1), press(false));
    assert.notEqual(m.page, PAGE.master);
    assert.notEqual(m.face(C.browse).title, "PACK");
});

test("SHIFT + jog nudges and releases", () => {
    const { m, clock, send } = setUp();
    clock.t += 0.01;
    assert.deepStrictEqual(send(C.jog(1), relative(3), { shifted: true }), [{ type: "nudge", amount: 0.5 }]);
    clock.t += 0.2;
    assert.deepStrictEqual(m.tick(), [{ type: "nudge", amount: 0 }]);
});

test("tempo soft takeover", () => {
    const { send } = setUp();
    assert.deepStrictEqual(send(C.tempo(0), absolute(1)), [], "200 BPM is far from 120: no jump");
    const v = (120 - 60) / 140;
    assert.deepStrictEqual(send(C.tempo(0), absolute(v)), [{ type: "tempo", bpm: 120 }]);
    assert.deepStrictEqual(send(C.tempo(0), absolute(1)), [{ type: "tempo", bpm: 200 }], "picked up: towards you is faster");
});

test("perf FX toggle, division, target", () => {
    const { m, send } = setUp();
    const perf = (fx, on, amount) => ({ type: "perf", fx, division: 3, target: 0, on, amount });
    assert.equal(m.perfType, 5, "FILTER first");
    send(C.fxSelect, press(true));                                    // REV THROW
    send(C.beatRight, press(true));                                   // 1/4
    send(C.fxChannel(0), press(true));                                // deck A
    assert.deepStrictEqual(send(C.fxOnOff, press(true)), [perf(4, true, 0.6)]);
    assert.equal(m.perfOn, true);
    assert.deepStrictEqual(send(C.fxOnOff, press(false)), [], "a toggle: letting go keeps it on");
    assert.deepStrictEqual(send(C.fxLevel, absolute(0.9)), [perf(4, true, 0.9)]);
    assert.deepStrictEqual(send(C.fxOnOff, press(true)), [perf(4, false, 0.9)], "pressed again: off");
    assert.deepStrictEqual(send(C.fxOnOff, press(false)), []);
    // FX SELECT while one plays: the old one stops before the next starts (no throw left running)
    send(C.fxOnOff, press(true)); send(C.fxOnOff, press(false));
    assert.deepStrictEqual(send(C.fxSelect, press(true)), [perf(4, false, 0.9), perf(0, true, 0.9)]);
    assert.equal(m.header.perf, "REPEAT");
});

test("perf FX menu: FILTER first, no TAPE STOP, SHIFT goes back", () => {
    const { m, send } = setUp();
    const seen = [m.perfType];
    for (let i = 1; i < PERF_MENU.length; i++) { send(C.fxSelect, press(true)); seen.push(m.perfType); }
    assert.equal(new Set(seen).size, PERF_FX.length - 1);
    assert.ok(!seen.includes(PERF_FX.indexOf("TAPE STOP")));
    send(C.fxSelect, press(true));
    assert.equal(m.perfType, 5, "round again");
    send(C.fxSelect, press(true), { shifted: true });
    assert.equal(m.perfType, PERF_FX.indexOf("SWEEP"), "SHIFT: back");
});

test("machine from BROWSE on the SRC page", () => {
    const { send } = setUp();
    assert.deepStrictEqual(send(C.browse, relative(1)), [{ type: "machine", track: 0, machine: 1 }]);
    send(C.loopIn(0), press(true));                                   // TRIG page: BROWSE selects tracks
    assert.deepStrictEqual(send(C.browse, relative(2)), [{ type: "selectTrack", track: 2 }]);
});

// MARK: faces and lights

test("faces show what the controls do", () => {
    const { model, m } = setUp();
    model.steps.set(0 * 100 + 3, stepView({ trig: true, cond: 5, retrig: 2, locks: 1 }));
    model.playhead = 3;
    const f = m.face(C.pad(0, 3));
    assert.equal(f.title, "4");
    assert.equal(f.light, ON);
    assert.equal(f.badge, "• C5 x3");
    assert.ok(f.outlined);
    assert.equal(f.color, COLORS.track[0]);
    const k = m.face(C.eqHi(1));
    assert.equal(k.title, "P12");
    assert.equal(k.color, COLORS.knobRow[0], "HI is blue on both channels");
    near(k.arc, 64 / 127, 1e-9);
    assert.equal(m.face(C.loopOut(0)).light, ON, "SRC is the page at start");
    assert.equal(m.lights().get(C.pad(0, 3)), ON);
    assert.equal(m.padLights("beatJump")[0][0], ON, "TRACKS mode: the active track lit");
});

test("held step shows its lock on the knobs", () => {
    const { model, m, send } = setUp();
    model.locks.set((0 * 100 + 6) * 100 + 8, 99);
    model.steps.set(6, stepView({ trig: true, locks: 1 }));
    send(C.pad(0, 6), press(true), { mode: "hotCue" });
    const k = m.face(C.eqHi(0));
    assert.equal(k.value, "99");
    assert.equal(k.badge, "LOCK");
    assert.deepStrictEqual(m.header.heldSteps, [6]);
});

// MARK: after the FM-1 community firmwares

test("SHIFT + touch captures the deck", () => {
    const { send } = setUp();
    assert.deepStrictEqual(send(C.jogTouch(1), press(true), { shifted: true }), [{ type: "capture", deck: 1, bars: 1 }]);
    assert.deepStrictEqual(send(C.jogTouch(1), press(false), { shifted: true }), [], "no scratch was started");
});

test("a held track pad rotates and makes Euclid", () => {
    const { model, m, send } = setUp();
    model.steps.set(100 + 0, stepView({ trig: true }));
    model.steps.set(100 + 8, stepView({ trig: true }));
    send(C.pad(0, 1), press(true), { mode: "beatJump" });            // hold track 2
    assert.equal(m.face(C.browse).title, "EUCLID");
    assert.equal(m.face(C.browse).value, "2/16");
    assert.deepStrictEqual(send(C.browse, relative(3)), [{ type: "euclid", track: 1, pulses: 5, steps: 16 }]);
    assert.deepStrictEqual(send(C.jog(0), relative(7)), [], "less than 8 ticks: nothing yet");
    assert.deepStrictEqual(send(C.jog(0), relative(10)), [{ type: "rotate", track: 1, amount: 2 }]);
    assert.deepStrictEqual(send(C.browsePress, press(true)), [{ type: "dice", track: 1, amount: 25 }]);
    send(C.pad(0, 1), press(false), { mode: "beatJump" });
    assert.equal(m.face(C.browse).title, "MACHINE", "let go: BROWSE is back to its page job");
});

test("slide and accent on held steps", () => {
    const { model, m, send } = setUp();
    send(C.pad(0, 0), press(true), { mode: "hotCue" });
    assert.equal(m.face(C.load(0)).title, "SLIDE");
    assert.deepStrictEqual(send(C.load(0), press(true)), [{ type: "stepSlide", track: 0, step: 0, on: true }]);
    assert.deepStrictEqual(send(C.load(1), press(true)), [{ type: "stepAccent", track: 0, step: 0, on: true }]);
    send(C.pad(0, 0), press(false), { mode: "hotCue" });
    assert.equal(m.face(C.pad(0, 0)).badge, "~ !");
    assert.ok(model.step(0, 0).trig, "edited while held: kept");
});

test("SHIFT + FILL toggles the autopilot", () => {
    const { m, send } = setUp();
    assert.deepStrictEqual(send(C.sync(0), press(true), { shifted: true }), [{ type: "autopilot", on: true }]);
    assert.deepStrictEqual(send(C.sync(0), press(false), { shifted: true }), [], "a SHIFT press does not hold FILL");
    assert.equal(m.face(C.sync(0)).value, "AUTOPILOT");
    assert.deepStrictEqual(send(C.sync(0), press(true), { shifted: true }), [{ type: "autopilot", on: false }]);
});

// MARK: lights

test("a held pad lights in every mode", () => {
    const { m, send } = setUp();
    send(C.pad(1, 2), press(true), { mode: "beatLoop" });            // KEYS: dim at rest ...
    assert.equal(m.face(C.pad(1, 2)).light, ON, "... lit while held");
    send(C.pad(1, 2), press(false), { mode: "beatLoop" });
    assert.equal(m.face(C.pad(1, 2)).light, DIM);
    send(C.pad(0, 6), press(true), { mode: "beatJump" });            // TRACKS: a mute pad
    assert.equal(m.padLights("beatJump")[0][6], ON);
});

test("running light on the hardware", () => {
    const { model, m } = setUp();
    model.steps.set(0 * 100 + 2, stepView({ trig: true }));
    model.playhead = 2;
    assert.equal(m.padLights("hotCue")[0][2], OFF, "a trig under the playhead: flips dark");
    model.playhead = 3;
    assert.equal(m.padLights("hotCue")[0][3], ON, "an empty step under the playhead: lit");
    assert.equal(m.padLights("hotCue")[0][2], ON);
    assert.equal(m.face(C.pad(0, 3)).light, OFF, "on screen: an outline, not a light");
    assert.equal(m.face(C.pad(0, 3)).outlined, true);
});

test("LED writer budget and order", () => {
    const { model, m } = setUp();
    const w = new DDJ400LEDWriter();
    w.maxPerCall = 24;
    model.steps.set(0, stepView({ trig: true }));
    const notMeter = (x) => x[0] !== 0xB0 && x[0] !== 0xB1;
    const first = w.messages(m, [0, 0]).filter(notMeter);
    assert.equal(first.length, 24, "a refresh goes out 24 at a time");
    assert.ok(first.every((x) => x[0] < 0x97), "buttons first");
    let all = first;
    for (let i = 0; i < 40; i++) all = all.concat(w.messages(m, [0, 0]).filter(notMeter));
    assert.ok(all.some((x) => x[0] === 0x97 && x[1] === 0x00 && x[2] === 0x7F), "the trig's pad (HOT CUE) lit in the end");
    const hotCueAt = all.findIndex((x) => x[0] === 0x97 && x[1] < 0x08);
    const otherAt = all.findIndex((x) => x[0] === 0x97 && x[1] >= 0x60);
    assert.ok(hotCueAt >= 0 && otherAt >= 0 && hotCueAt < otherAt, "the current mode's pads before the other modes'");
    assert.deepStrictEqual(w.messages(m, [0, 0]).filter((x) => x[0] >= 0x90 && x[0] !== 0xB0), [], "nothing left");
});

test("releaseAll lets go", () => {
    const { m, send } = setUp();
    send(C.sync(0), press(true));
    send(C.fxOnOff, press(true));
    send(C.jogTouch(1), press(true));
    send(C.pad(0, 0), press(true), { mode: "beatLoop" });
    const a = m.releaseAll();
    const has = (x) => a.some((y) => { try { assert.deepStrictEqual(y, x); return true; } catch { return false; } });
    assert.ok(has({ type: "fill", on: false }));
    assert.ok(has({ type: "scratch", deck: 1, touching: false, rate: 0 }));
    assert.ok(has({ type: "release", track: 0, note: 60 }));
    assert.ok(a.some((y) => y.type === "perf" && y.on === false));
});

// (beyond the Swift tests: the header and a few faces the app reads)
test("header and faces of the other controls", () => {
    const { model, m } = setUp();
    model.queuedPattern = 3;
    assert.deepStrictEqual(m.header, {
        pattern: 0, queued: 3, bpm: 120, swing: 50, playing: false, recording: false, fill: false, track: 0,
        machine: "KICK", page: PAGE.src, stepPage: 0, pages: 1, perf: null, sceneEdit: null, heldSteps: [],
    });
    assert.equal(m.face(C.tempo(0)).value, "120");
    assert.equal(m.face(C.tempo(1)).value, "50%");
    assert.equal(m.face(C.cue(1)).title, "TAP");
    assert.equal(m.face(C.fxLevel).value, "60%");
    assert.equal(m.face(C.padModeButton(0, "hotCue")).light, ON);
    assert.equal(m.face(C.padModeButton(0, "sampler")).title, "PATTERNS");
    assert.equal(m.face(C.pad(1, 3)).title, "12");
    m.setPadMode("sampler", 1);
    assert.equal(m.face(C.pad(1, 3)).title, "P12");
    assert.equal(m.face(C.pad(0, 3)).outlined, false);
    assert.equal(m.face(C.headphoneLevel), null);
    assert.equal(m.face(C.callLeft(1)).value, "1/1");
});
