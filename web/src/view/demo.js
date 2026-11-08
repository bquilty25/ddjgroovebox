// SPDX-License-Identifier: GPL-3.0-only
// A demo of the view with fake but realistic data (as the Mac app's snapshot shows it): faces on the knobs and pads, a
// header, deck tracks, a pattern playing at 122 BPM. view-demo.html?stage=map|scope|packs|figures picks what the stage
// shows (default map, as just after selecting a track with several step pages); &page=src|fltr|amp|fx|lfo|master|trig
// picks the page picture for stage=scope. Events from the mouse and keyboard are logged to the console and shown.

import { C, face, OFF, DIM, ON, COLORS, PAGE, MACHINE, TRIG_PAGES } from "../vocab.js";
import { GrooveboxLayout } from "./layout.js";

const params = new URLSearchParams(location.search);
const stageMode = params.get("stage") ?? "map";
const pageName = (params.get("page") ?? "fltr").toUpperCase();

await document.fonts.load('16px "Terminus"').catch(() => {});

const layout = new GrooveboxLayout(document.getElementById("app"));
const { ddj, strip } = layout;
layout.onEvent((e) => { console.log("event", JSON.stringify(e)); ddj.show(e); });

const T = COLORS.track, K = COLORS.knobRow, P = COLORS.page;
const bpm = 122;
const names = ["KICK", "HAT", "CLAP", "HAT", "SYNTH", "SYNTH", "FM", "SAMPLE"];
const machines = [MACHINE.kick, MACHINE.hat, MACHINE.clap, MACHINE.hat, MACHINE.synth, MACHINE.synth, MACHINE.fm, MACHINE.sample];
const pat = (s) => [...s].map((ch) => ({ ".": 0, x: 1, l: 2, X: 3 })[ch]);
const steps = [
    pat("x.X.x...x.x...xx"),
    pat("..x..x..x.x...x."),
    pat("....x...x...x..x"),
    pat("xxxxxxxxxxxxxxxx"),
    pat("x.x.xx.x.xx.x.x.xx.x.x.xx.x.x.x.x.xx.x.x.xx.x.xx.x.x.x.xx.x.x.x."),
    pat("x.x.x..x..x.x.x.x.x..x..x.x.x.x.x.x...x.x.x.x.x..x.x..x.x.x.x..x"),
    pat("x...x..x....x...x.x.....x...x.....x...x...x...x.....x...x.x...x."),
    pat("......x........."),
];
const active = 5;

function faces() {
    const f = new Map();
    // left deck: the pages, tracks, fill; the steps on the pads
    f.set(C.loopIn(0), face("TRIG", { color: P[0] }));
    f.set(C.loopOut(0), face("SRC", { color: P[1] }));
    f.set(C.reloop(0), face("FLTR", { color: P[2], light: ON }));
    f.set(C.callLeft(0), face("TRK -"));
    f.set(C.callRight(0), face("TRK +"));
    f.set(C.sync(0), face("FILL"));
    f.set(C.jog(0), face("DECK A", { value: "TURN: TRACK" }));
    f.set(C.tempo(0), face("TEMPO", { value: String(bpm) }));
    f.set(C.shift(0), face("SHIFT"));
    f.set(C.cue(0), face("REC", { color: 0xFFFF2C34 }));
    f.set(C.play(0), face("STOP", { color: 0xFF1ECC70, light: ON }));
    ["STEPS", "KEYS", "TRACKS", "PATTERNS"].forEach((t, i) => {
        f.set(C.padModeButton(0, ["hotCue", "beatLoop", "beatJump", "sampler"][i]), face(t, { light: i === 0 ? ON : OFF }));
    });
    for (let i = 0; i < 8; i++) {
        const on = i === 2 || i === 7;
        f.set(C.pad(0, i), face(String(i + 1), { color: T[active], light: on ? ON : OFF, badge: on ? "•" : null }));
    }
    // right deck: the other pages, step pages, MASTER; the tracks on the pads
    f.set(C.loopIn(1), face("AMP", { color: P[3] }));
    f.set(C.loopOut(1), face("FX", { color: P[4] }));
    f.set(C.reloop(1), face("LFO", { color: P[5] }));
    f.set(C.callLeft(1), face("PAGE -", { value: "1/4" }));
    f.set(C.callRight(1), face("PAGE +", { value: "1/4", light: DIM }));
    f.set(C.sync(1), face("MASTER", { color: P[6] }));
    f.set(C.jog(1), face("DECK B", { value: `${active + 1} ${names[active]}`, color: T[active] }));
    f.set(C.tempo(1), face("SWING", { value: "54%" }));
    f.set(C.shift(1), face("SHIFT"));
    f.set(C.cue(1), face("TAP", { value: String(bpm) }));
    f.set(C.play(1), face("STOP", { color: 0xFF1ECC70, light: ON }));
    ["STEPS", "KEYS", "TRACKS", "PATTERNS"].forEach((t, i) => {
        f.set(C.padModeButton(1, ["hotCue", "beatLoop", "beatJump", "sampler"][i]), face(t, { light: i === 2 ? ON : OFF }));
    });
    for (let i = 0; i < 4; i++) {
        const t = 4 + i;
        f.set(C.pad(1, i), face(names[t], { value: String(t + 1), color: T[t], light: t === active ? ON : DIM }));
        f.set(C.pad(1, 4 + i), face("ON", { color: T[t], light: DIM }));
    }
    // the mixer: the page's knobs in the knob rows' colours, the scenes, BEAT FX
    f.set(C.load(0), face("PAT -"));
    f.set(C.load(1), face("PAT +"));
    f.set(C.browse, face("TRACK", { value: String(active + 1), color: T[active] }));
    f.set(C.browsePress, face("AUDITION"));
    f.set(C.trim(0), face("DRIVE A"));
    f.set(C.trim(1), face("DRIVE B"));
    const knobs = [[C.eqHi(0), "CUTOFF", "258 HZ", 0.55, K[0]], [C.eqHi(1), "ATTACK", "0.0 MS", 0, K[0]],
                   [C.eqMid(0), "RESO", "24 %", 0.24, K[1]], [C.eqMid(1), "DECAY", "56 MS", 0.3, K[1]],
                   [C.eqLow(0), "TYPE", "LP", 0, K[2]], [C.eqLow(1), "KEYTRK", "25 %", 0.25, K[2]],
                   [C.cfx(0), "ENV", "+24", 0.69, K[3]], [C.cfx(1), "DRIVE", "0 %", 0, K[3]]];
    for (const [c, t, v, arc, color] of knobs) f.set(c, face(t, { value: v, arc, color }));
    f.set(C.headphoneCue(0), face("SCENE A", { color: COLORS.sceneA }));
    f.set(C.headphoneCue(1), face("SCENE B", { color: COLORS.sceneB }));
    f.set(C.channelFader(0), face("DECK A"));
    f.set(C.channelFader(1), face("DECK B"));
    f.set(C.crossfader, face("A ◀ SCENE ▶ B"));
    f.set(C.beatLeft, face("DIV-", { value: "1/8" }));
    f.set(C.beatRight, face("DIV+", { value: "1/8" }));
    f.set(C.fxSelect, face("FX", { value: "REPEAT" }));
    f.set(C.fxChannel(0), face("DECK A"));
    f.set(C.fxChannel(1), face("DECK B"));
    f.set(C.fxChannel(2), face("MASTER", { light: ON }));
    f.set(C.fxLevel, face("AMOUNT", { value: "60%", arc: 0.6 }));
    f.set(C.fxOnOff, face("REPEAT", { color: 0xFF2E7CFF }));
    return f;
}

ddj.faces = faces();
ddj.padModes = ["hotCue", "beatJump"];
// (?takeover: a soft takeover on the left channel fader, its cap hollow until moved past the mark)
if (params.has("takeover")) ddj.takeovers = new Map([[C.channelFader(0), { target: 0.4, active: false }]]);
ddj.setPosition(C.tempo(0), 0.5);
ddj.setPosition(C.tempo(1), 0.5);
ddj.setPosition(C.crossfader, 0.5);
ddj.jogDials = [null, 1];

const pageIndex = Math.max(0, TRIG_PAGES.indexOf(pageName));
const header = (stepPage) => ({
    pattern: 0, queued: null, bpm, swing: 54, playing: true, recording: false, fill: false,
    track: active, machine: names[active], page: stageMode === "scope" ? pageIndex : PAGE.fltr, stepPage,
    pages: Math.ceil(steps[active].length / 16), perf: null, sceneEdit: null, heldSteps: [],
});

// fake page values per page (raw, normalised, shown) for the picture
const scopeValues = {
    [PAGE.trig]: [[60, 100, 40, 2, 0, 0, 0, 0], [0.5, 0.8, 0.4, 0.25, 0, 0, 0, 0], ["C4", "100", "1/16", "2", "", "", "", ""]],
    [PAGE.src]: [[0, 1, 0, 0, 30, 20, 0, 0], [0.5, 0.45, 0.2, 0.3, 0.3, 0.2, 0, 0], ["", "", "", "", "", "", "", ""]],
    [PAGE.fltr]: [[80, 30, 0, 24, 0, 0, 0, 0], [0.55, 0.24, 0, 0.69, 0, 0, 0, 0], ["258 HZ", "24 %", "LP", "+24", "", "", "", ""]],
    [PAGE.amp]: [[0, 0, 40, 60, 30, 64, 100, 0], [0.05, 0, 0.3, 0.5, 0.3, 0.5, 0.8, 0], ["", "", "", "", "", "", "", ""]],
    [PAGE.fx]: [[40, 30, 0, 0, 0, 10, 0, 64], [0.4, 0.3, 0.1, 0, 0, 0.1, 0.3, 0.5], ["", "", "", "", "", "", "", ""]],
    [PAGE.lfo]: [[30, 2, 0, 3, 40, 0, 0, 0], [0.3, 0.2, 0, 0.1, 0.6, 0.5, 0, 0], ["", "", "", "CUTOFF", "", "", "", ""]],
    [PAGE.master]: [[4, 60, 0, 70, 30, 0, 0, 0], [0.4, 0.5, 0, 0.6, 0.3, 0, 0, 0], ["", "", "", "", "", "", "", ""]],
};

const t0 = performance.now() / 1000;
const levels = new Array(8).fill(0);
let lastStep = -1, wobble = 0;

function tick() {
    const now = performance.now() / 1000;
    const step = Math.floor((now - t0) / (60 / bpm / 4));
    if (step !== lastStep) {
        lastStep = step;
        for (let t = 0; t < 8; t++) {
            const s = steps[t][step % steps[t].length];
            if (s === 1 || s === 3) levels[t] = 0.9;
        }
    }
    for (let t = 0; t < 8; t++) levels[t] *= 0.9;
    const playheads = steps.map((s) => step % s.length);
    const h = header(0);                                           // (the pads show the first step page)
    const rows = names.map((name, t) => ({
        name, color: T[t], active: t === active, muted: false, level: levels[t], steps: steps[t], playhead: playheads[t],
    }));
    let scope = null;
    const page = h.page;
    const sv = scopeValues[page];
    if (sv) {
        // stage=scope: a knob turning (the picture shows while a value changes)
        if (stageMode === "scope") wobble = 1 - wobble;
        scope = { page, machine: machines[active], track: active, color: T[active],
                  raw: sv[0].map((v, i) => (i === 7 ? v + wobble : v)), normalised: sv[1], shown: sv[2] };
    }
    strip.update({
        header: h, rows, crossfader: 0.62, autopilot: false, connected: false, problem: null, scope,
        packs: { names: ["HOUSE", "TECHNO", "GARAGE", "DNB", "DUB"], cursor: 1,
                 browsedAt: stageMode === "packs" ? now : -100, loaded: null, revertedAt: null },
        lengthChangedAt: stageMode === "map" ? Math.floor((now - t0) / 2) * 2 : -100,
        clock: now,
    });
    if (stageMode !== "map" && now - t0 < 0.05) strip.pageChanged = -100;
    ddj.header = h;
    ddj.deckTracks = [0, 1].map((d) => [0, 1, 2, 3].map((i) => {
        const t = d * 4 + i;
        return { name: names[t], color: T[t], level: Math.round(levels[t] * 8) / 8, active: t === active, muted: false };
    }));
    ddj.meters = [Math.max(...levels) * 0.8, Math.max(...levels) * 0.75];
    requestAnimationFrame(tick);
}
tick();
