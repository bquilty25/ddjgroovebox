// SPDX-License-Identifier: GPL-3.0-only
// The engine as the page sees it: the WebAssembly engine runs in an AudioWorklet (worklet.js); this mirrors its state
// from snapshots it posts (TrigModel, as Sources/DDJGroovebox/TrigEngine.swift implements it over C calls), turns
// actions into engine calls (TrigEngine.apply), keeps the project in the browser, and answers parameter descriptions
// and value formatting from a second, silent instance of the engine on this thread.
import { instantiateAsync, cString } from "./wasi.js";
import { MACHINE } from "./vocab.js";

const TRACKS = 8, NPARAMS = 48, SLOTS = 8, STEPS = 64, LPS = 8, SCENE_MAX = 128;
const PER_TRACK = 5 + 2 * NPARAMS;
const LIGHT = 8 + TRACKS * PER_TRACK + SLOTS + 2;           // (td_web.c's SNAP_LIGHT: + the output peaks)
const STEPS_AT = LIGHT, LOCKS_AT = STEPS_AT + TRACKS * STEPS * 9, SCENES_AT = LOCKS_AT + TRACKS * STEPS * LPS * 2;
const PATTERNS = 16, NAME = 12, NAMES_AT = SCENES_AT + 2 * (1 + SCENE_MAX * 3);   // (the patterns' names)
const PROJECT_KEY = "trigdeck.project.v2";

export class TrigWebEngine {
    constructor() {
        this.light = new Int32Array(LIGHT);
        this.heavy = new Int32Array(NAMES_AT + PATTERNS * NAME);
        this.infoCache = new Map();
        this.nextCaptureSlot = 4;
        this.dirty = false;
        this.lastSave = 0;
        this.onState = null;                                  // called after each snapshot
    }

    /** Start: the silent helper, then the audio (needs a user gesture), then the saved project if there is one. */
    async start() {
        const bytes = await (await fetch(new URL("../trigcore.wasm", import.meta.url))).arrayBuffer();
        this.helper = await instantiateAsync(bytes);
        this.helper.tdw_init();
        this.packNames = Array.from({ length: this.helper.td_pack_count() }, (_, i) => cString(this.helper, this.helper.tdw_pack_name(i)));
        this.condCount = this.helper.tdw_cond_count();
        this.ctx = new AudioContext({ sampleRate: 48000, latencyHint: "interactive" });
        await this.ctx.audioWorklet.addModule(new URL("../worklet.js", import.meta.url));
        const saved = loadProject();
        this.node = new AudioWorkletNode(this.ctx, "trigdeck", {
            outputChannelCount: [2], processorOptions: { wasm: bytes, project: saved },
        });
        this.node.port.onmessage = (e) => this.receive(e.data);
        this.node.onprocessorerror = (e) => this.onError?.("the engine stopped: " + (e.message || "an error"));
        this.node.connect(this.ctx.destination);
        await new Promise((res) => { this.ready = res; });
        addEventListener("pagehide", () => this.save(true));
        setInterval(() => this.save(false), 1000);
    }

    receive(m) {
        if (m.error) { this.onError?.(m.error); return; }
        if (m.light) this.light = m.light;
        if (m.heavy) this.heavy = m.heavy;
        if (m.project) { storeProject(m.project); this.lastSave = performance.now(); }
        if (this.ready) { this.ready(); this.ready = null; }
        this.onState?.();
    }

    /** Engine calls, in order: [[name, ...args], ...] */
    call(calls) { if (calls.length) this.node?.port.postMessage({ calls }); }

    /** TrigEngine.apply: the mapper's actions -> engine calls */
    apply(actions) {
        if (!actions?.length) return;
        const c = [];
        const b = (x) => (x ? 1 : 0);
        for (const a of actions) {
            switch (a.type) {
            case "togglePlay": c.push(["td_play", this.playing ? 0 : 1]); break;
            case "restart": c.push(["td_restart"]); break;
            case "toggleRecord": c.push(["td_record", this.recording ? 0 : 1]); break;
            case "tapTempo": c.push(["td_tap_tempo"]); break;
            case "fill": c.push(["td_fill", b(a.on)]); break;
            case "tempo": c.push(["td_set_tempo", a.bpm]); break;
            case "swing": c.push(["td_set_swing", a.swing]); break;
            case "nudge": c.push(["td_nudge", a.amount]); break;
            case "selectTrack": case "page": case "stepPage": break;          // (the mapper's own state)
            case "stepSet": c.push(["td_step_set", a.track, a.step, b(a.on)]); break;
            case "stepLock": c.push(["td_step_lock", a.track, a.step, a.param, a.value]); break;
            case "stepMicro": c.push(["td_step_micro", a.track, a.step, a.micro]); break;
            case "stepCond": c.push(["td_step_cond", a.track, a.step, a.cond]); break;
            case "paramSet": c.push(["td_param_set", a.track, a.param, a.value]); break;
            case "globalSet": c.push(["td_global_set", a.slot, a.value]); break;
            case "machine": c.push(["td_set_machine", a.track, a.machine]); break;
            case "trigger": c.push(["td_trigger", a.track, a.note, a.velocity]); break;
            case "release": c.push(["td_release", a.track, a.note]); break;
            case "mute": c.push(["td_mute", a.track, b(a.on)]); break;
            case "queuePattern": c.push(["td_pattern_queue", a.pattern]); break;
            case "copyPattern": c.push(["td_pattern_copy", a.from, a.to]); break;
            case "sceneSet": c.push(["td_scene_set", a.scene, a.track, a.param, a.value]); break;
            case "sceneClear": c.push(["td_scene_clear", a.scene]); break;
            case "crossfader": c.push(["td_crossfader", a.value]); break;
            case "deckLevel": c.push(["td_deck_level", a.deck, a.level]); break;
            case "deckDrive": c.push(["td_deck_drive", a.deck, a.drive]); break;
            case "scratch": c.push(["td_scratch", a.deck, b(a.touching), a.rate]); break;
            case "perf": c.push(["td_perf", a.fx, a.division, a.target, b(a.on), a.amount]); break;
            case "capture": {                                                  // into the next free slot (4 .. 15)
                const slot = this.nextCaptureSlot;
                this.nextCaptureSlot = slot >= 15 ? 4 : slot + 1;
                c.push(["td_capture", a.deck, a.bars, slot]);
                this.lastCapture = { deck: a.deck, slot };
                break;
            }
            case "euclid": c.push(["td_euclid", a.track, a.pulses, a.steps, 0]); break;
            case "rotate": c.push(["td_rotate", a.track, a.amount]); break;
            case "dice": c.push(["td_dice", a.track, a.amount]); break;
            case "stepSlide": c.push(["td_step_slide", a.track, a.step, b(a.on)]); break;
            case "stepAccent": c.push(["td_step_accent", a.track, a.step, b(a.on)]); break;
            case "autopilot": c.push(["td_autopilot", b(a.on)]); break;
            case "loadPack": c.push(["td_pack_load", a.pack]); break;
            case "revertPack": c.push(["td_pack_revert"]); break;
            case "length": c.push(["td_set_length_repeat", a.track, a.steps]); break;
            default: console.warn("TRIGDECK: no engine call for", a);
            }
        }
        this.call(c);
        this.dirty = true;
    }

    /** a resample lands on the active track when it is a SAMPLE track (TrigInstrument.handle) */
    captureTo(track) {
        if (this.lastCapture && this.machine(track) === MACHINE.sample) this.call([["td_set_sample", track, this.lastCapture.slot]]);
        this.lastCapture = null;
    }

    newProject() { this.call([["td_project_new"]]); this.dirty = true; }

    /** the project into the browser's storage: asked of the worklet, stored when it answers (see receive) */
    save(force) {
        if (!this.node || (!this.dirty && !force)) return;
        if (!force && performance.now() - this.lastSave < 5000) return;
        this.dirty = false;
        this.node.port.postMessage({ save: true });
    }

    // ---- TrigModel (vocab.js), over the snapshots
    get playing() { return this.light[0] !== 0; }
    get recording() { return this.light[1] !== 0; }
    get pattern() { return this.light[2]; }
    /** a pattern's name ("DEEP"; "" without one) */
    patternName(p) {
        if (!(p >= 0 && p < PATTERNS)) return "";
        let s = "";
        for (let k = 0; k < NAME; k++) { const ch = this.heavy[NAMES_AT + p * NAME + k]; if (!ch) break; s += String.fromCharCode(ch); }
        return s;
    }
    get queuedPattern() { const q = this.light[3]; return q >= 0 && q !== this.light[2] ? q : null; }
    get bpm() { return this.light[4] / 100; }
    get swing() { return this.light[5]; }
    get autopilot() { return this.light[6] !== 0; }
    t(track) { return 8 + track * PER_TRACK; }
    machine(track) { return this.light[this.t(track)]; }
    muted(track) { return this.light[this.t(track) + 1] !== 0; }
    length(track) { return Math.max(1, this.light[this.t(track) + 2]); }
    playingStep(track) { const s = this.light[this.t(track) + 3]; return s >= 0 ? s : null; }
    trackLevel(track) { return this.light[this.t(track) + 4] / 1e6; }
    value(track, param) { return this.light[this.t(track) + 5 + param]; }
    liveValue(track, param) { return this.light[this.t(track) + 5 + NPARAMS + param]; }
    globalValue(slot) { return this.light[8 + TRACKS * PER_TRACK + slot]; }
    /** the output's peaks, left and right, 0..1 (the meters) */
    get meters() { const o = 8 + TRACKS * PER_TRACK + SLOTS; return [Math.min(1, this.light[o] / 1e6), Math.min(1, this.light[o + 1] / 1e6)]; }

    info(track, param) { return this.infoFor(this.machine(track), param); }
    infoFor(machine, param) {
        const key = (machine << 8) | param;
        if (!this.infoCache.has(key)) this.infoCache.set(key, this.readInfo(this.helper.tdw_info(machine, param), { machine, param }));
        return this.infoCache.get(key);
    }
    globalInfo(slot) {
        const key = 0x10000 | slot;
        if (!this.infoCache.has(key)) this.infoCache.set(key, this.readInfo(this.helper.tdw_ginfo(slot), { slot }));
        return this.infoCache.get(key);
    }
    readInfo(ptr, where) {
        if (!ptr) return null;
        const h = this.helper, dv = new DataView(h.memory.buffer, ptr, 32);
        return {
            name: cString(h, ptr), shortName: cString(h, ptr + 16),
            min: dv.getInt16(24, true), max: dv.getInt16(26, true), def: dv.getInt16(28, true), lockable: dv.getUint8(31) !== 0,
            _where: where,                                    // (to format it)
        };
    }
    format(info, value) {
        const w = info?._where;
        if (!w) return String(value);
        const p = w.slot !== undefined ? this.helper.tdw_gformat(w.slot, value) : this.helper.tdw_format(w.machine, w.param, value);
        return cString(this.helper, p) || String(value);
    }
    step(track, s) {
        s = Math.max(0, Math.min(STEPS - 1, s));
        const o = STEPS_AT + (track * STEPS + s) * 9, h = this.heavy;
        return { trig: h[o] !== 0, note: h[o + 1], velocity: h[o + 2], cond: h[o + 3], retrig: h[o + 4], micro: h[o + 5],
                 locks: h[o + 6], slide: h[o + 7] !== 0, accent: h[o + 8] !== 0 };
    }
    lock(track, s, param) {
        const o = LOCKS_AT + (track * STEPS + s) * LPS * 2;
        for (let k = 0; k < LPS; k++) if (this.heavy[o + 2 * k] === param) return this.heavy[o + 2 * k + 1];
        return null;
    }
    scene(sc, track, param) {
        const o = SCENES_AT + sc * (1 + SCENE_MAX * 3), n = this.heavy[o];
        for (let k = 0; k < n; k++) {
            const e = o + 1 + 3 * k;
            if (this.heavy[e] === track && this.heavy[e + 1] === param) return this.heavy[e + 2];
        }
        return null;
    }
    condName(c) { return cString(this.helper, this.helper.tdw_cond_name(c)); }
    /** what a parameter does (the help line), for the track's machine; the MASTER page's by slot */
    paramHelp(track, param) { return cString(this.helper, this.helper.tdw_param_help(this.machine(track), param)); }
    globalHelp(slot) { return cString(this.helper, this.helper.tdw_global_help(slot)); }
    machineName(m) { return cString(this.helper, this.helper.tdw_machine_name(m)); }
}

// ---- the project in the browser's storage (base64 in localStorage: about 100 KB)
function storeProject(bytes) {
    try {
        let s = "";
        for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
        localStorage.setItem(PROJECT_KEY, btoa(s));
    } catch (e) { console.warn("TRIGDECK: the project could not be saved", e); }
}
function loadProject() {
    try {
        const s = localStorage.getItem(PROJECT_KEY);
        if (!s) return null;
        const bin = atob(s), out = new Uint8Array(bin.length);
        for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
        return out;
    } catch { return null; }
}
export function exportProject() { const p = loadProject(); return p ? new Blob([p], { type: "application/octet-stream" }) : null; }
export function forgetProject() { try { localStorage.removeItem(PROJECT_KEY); } catch {} }
