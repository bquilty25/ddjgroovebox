// SPDX-License-Identifier: GPL-3.0-only
// TRIGDECK on the DDJ-400 (a port of Sources/DDJMapping/TrigMapper.swift; the Swift app is the reference): DDJ events
// in, engine actions out, and for every control the face it shows on the augmented controller (its job, value, arc,
// colour, light). Pure logic over a TrigModel (see vocab.js). Names follow the Swift source.

import {
    C, DECKS, PAD_MODES, OFF, DIM, ON, face, parseControl, PAGE, TRIG_PAGES, PERF_FX, PERF_MENU, MACHINE, isMelodic, COLORS,
    midiNoteName,
} from "../vocab.js";

/** The pad functions (Swift's PadFunction) and their names. */
export const PAD_FUNCTION_NAME = { steps: "STEPS", keys: "KEYS", tracks: "TRACKS", patterns: "PATTERNS" };

/** HOT CUE (and its SHIFT twin PAD FX1) = STEPS, BEAT LOOP / KEYBOARD = KEYS, BEAT JUMP / PAD FX2 = TRACKS,
 *  SAMPLER / KEY SHIFT = PATTERNS */
export function padFunctionOf(mode) {
    switch (mode) {
    case "hotCue": case "padFX1": return "steps";
    case "beatLoop": case "keyboard": return "keys";
    case "beatJump": case "padFX2": return "tracks";
    case "sampler": case "keyShift": return "patterns";
    default: return "steps";
    }
}

const MACHINE_COUNT = Object.keys(MACHINE).length;
const PERF_COUNT = PERF_FX.length;

/** Swift's Double.rounded(): half away from zero */
const roundSwift = (x) => Math.sign(x) * Math.round(Math.abs(x));
/** String(format: "%.0f", x): half to even */
function format0(x) {
    const f = Math.floor(x), diff = x - f;
    const r = diff > 0.5 ? f + 1 : diff < 0.5 ? f : (f % 2 === 0 ? f : f + 1);
    return String(r === 0 ? 0 : r);
}
const clampInt = (lo, hi, x) => Math.min(hi, Math.max(lo, x));
const mod = (a, k) => ((a % k) + k) % k;
const deckBase = (d) => (d === 0 ? 0 : 4);
const inDeck = (d, t) => t >= deckBase(d) && t < deckBase(d) + 4;

const defaultClock = () => performance.now() / 1000;
/** a mute or solo pad held at least this long (seconds) is temporary: letting go puts the tracks back */
export const HOLD_TO_RESTORE = 0.4;
/** a knob with no parameter on this page: the Swift face's title, a long dash (U+2014) */
const NO_PARAM = String.fromCharCode(0x2014);

export class TrigMapper {
    static ticksPerSecondAtSpeed = 400;   // 720 ticks a turn at 33 1/3 rpm (Mixxx's DDJ-400 setting)
    static divisions = ["1/32", "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS", "4 BARS"];   // the engine's TD_PERF_DIVS

    #pageBeforeMaster = PAGE.src;
    #modes = ["hotCue", "hotCue"];
    #held = new Map();             // a step pad held -> its step
    #heldTrackPads = new Map();    // TRACKS mode: a track's pad held -> the track
    #muteHolds = new Map();        // a mute pad held -> { at, before: the 8 mutes before }
    #soloSaved = null;             // the mutes before a solo (restored when it ends)
    #pressedPads = new Set();
    #euclidPulses = new Map();
    #rotateAcc = new Map();
    #setOnPress = new Set();       // steps a press set (not cleared on its release)
    #edited = new Set();           // held steps changed by a knob / jog / BROWSE
    #playing = new Map();          // KEYS pads / audition held -> { track, note }
    #knobLast = new Map();
    #knobAcc = new Map();
    #touching = new Set();
    #jogLastTime = new Map();
    #scratchRate = new Map();
    #nudgeLast = null;
    #tempoPicked = false;
    #crossfaderLast = null;        // (the crossfader passes the middle once before it moves the scenes)
    #swingPicked = false;
    #lastTempoSet = null;
    #lastSwingSet = null;
    #jogDial = new Map();          // deck -> { anchor, pos }
    #jogFocusAcc = new Map();

    constructor(model) {
        this.model = model;
        /** the clock, in seconds (injectable for tests) */
        this.now = defaultClock;

        this.track = 0;
        this.page = PAGE.src;               // a TrigPage index (PAGE, TRIG_PAGES)
        this.stepPage = 0;
        this.fillHeld = false;
        this.sceneEdit = null;
        this.perfType = PERF_MENU[0];
        this.tempoBeforeTap = null;     // { bpm, pattern }: the tempo before TAP on this pattern (SHIFT + TAP: back)
        this.crossfaderPicked = false;  // the crossfader has passed the middle since the start
        this.perfDivision = 2;
        this.perfTarget = 2;
        this.perfAmount = 0.6;
        this.perfOn = false;
        /** when the length last changed (the strip shows the page map) */
        this.lengthChanged = -100;
        /** the pack BROWSE points at on the MASTER page, and when it last moved or loaded (the strip shows the list) */
        this.packCursor = 0;
        this.packBrowsed = -100;
        /** the pack last loaded (its name slides in on the strip), and when: { index, at } or null */
        this.packLoaded = null;
        this.packReverted = null;
        /** ticks of a jog's side per track: a quarter turn of the platter (720 ticks a turn), so the marker on the drawn
         *  jog turns with the real one and the quadrant it points into is always the active track */
        this.jogTicksPerTrack = 180;
        /** ticks past a quadrant's edge before the next track (no flicker on the edge) */
        this.jogHysteresis = 6;
        /** ticks of a jog whose deck does not hold the active track before it brings it to its deck */
        this.jogFocusTicks = 24;
    }

    padMode(deck) { return this.#modes[deck] ?? "hotCue"; }
    padFunction(deck) { return padFunctionOf(this.padMode(deck)); }
    setPadMode(mode, deck) { this.#modes[deck] = mode; }

    /** a knob's slot 0...7 (EQ HI, MID, LOW, CFX; left deck 0-3, right 4-7), or null */
    static slot(c) {
        const { kind, deck } = parseControl(c);
        switch (kind) {
        case "eqHi": return deck === 0 ? 0 : 4;
        case "eqMid": return deck === 0 ? 1 : 5;
        case "eqLow": return deck === 0 ? 2 : 6;
        case "cfx": return deck === 0 ? 3 : 7;
        default: return null;
        }
    }
    param(slot) { return this.page * 8 + slot; }
    get heldSteps() { return [...new Set(this.#held.values())].sort((a, b) => a - b); }

    #packNames() { return this.model.packNames ?? []; }
    #firstHeldTrack() { for (const t of this.#heldTrackPads.values()) return t; return null; }

    // MARK: events

    handle(e) {
        const c = e.control, v = e.value;
        const { kind, deck: d } = parseControl(c);
        if (kind === "pad" && e.padMode != null) {
            this.#modes[d] = e.padMode;
            return this.#pad(d, parseControl(c).index, e);
        }
        const isPress = "press" in v, down = v.press === true;
        const pressed = isPress && down;
        const model = this.model;
        if (kind === "padModeButton" && pressed) { this.#modes[d] = parseControl(c).mode; return []; }
        if (kind === "play" && pressed) return [{ type: e.shifted ? "restart" : "togglePlay" }];
        if (kind === "cue" && d === 0 && pressed) return [{ type: "toggleRecord" }];
        if (kind === "cue" && d === 1 && pressed) {
            // TAP; SHIFT + TAP: the tempo from before the taps comes back (kept until then, or another pattern)
            const b = this.tempoBeforeTap;
            if (e.shifted) {
                this.tempoBeforeTap = null;
                return b && b.pattern === this.model.pattern ? [{ type: "tempo", bpm: b.bpm }] : [];
            }
            if (!b || b.pattern !== this.model.pattern) this.tempoBeforeTap = { bpm: this.model.bpm, pattern: this.model.pattern };
            return [{ type: "tapTempo" }];
        }
        if (kind === "sync" && d === 0 && pressed && e.shifted) {            // SHIFT + FILL
            return [{ type: "autopilot", on: !model.autopilot }];
        }
        if (kind === "sync" && d === 0 && isPress) {
            if (!down && !this.fillHeld) return [];
            this.fillHeld = down;
            return [{ type: "fill", on: down }];
        }
        if (kind === "sync" && d === 1 && pressed) {
            if (this.page === PAGE.master) this.page = this.#pageBeforeMaster;
            else { this.#pageBeforeMaster = this.page; this.page = PAGE.master; }
            return [];
        }
        if (kind === "loopIn" && pressed) return this.#setPage(d === 0 ? PAGE.trig : PAGE.amp);
        if (kind === "loopOut" && pressed) return this.#setPage(d === 0 ? PAGE.src : PAGE.fx);
        if (kind === "reloop" && pressed) return this.#setPage(d === 0 ? PAGE.fltr : PAGE.lfo);
        if (kind === "callLeft" && d === 0 && pressed) return this.#selectTrack(this.track - 1);
        if (kind === "callRight" && d === 0 && pressed) return this.#selectTrack(this.track + 1);
        if (kind === "callLeft" && d === 1 && pressed && e.shifted) {        // SHIFT + PAGE -: a bar shorter
            const n = model.length(this.track);
            return this.#setLength(n > 16 ? Math.trunc((n - 1) / 16) * 16 : n);
        }
        if (kind === "callRight" && d === 1 && pressed && e.shifted) {       // SHIFT + PAGE +: a bar longer
            return this.#setLength(Math.min(64, (Math.trunc(model.length(this.track) / 16) + 1) * 16));
        }
        if (kind === "callLeft" && d === 1 && pressed) { this.stepPage = Math.max(0, this.stepPage - 1); return []; }
        if (kind === "callRight" && d === 1 && pressed) {
            this.stepPage = Math.min(this.pageCount - 1, this.stepPage + 1);
            return [];
        }
        if (kind === "browse" && "relative" in v && e.shifted && this.#held.size === 0 && this.#heldTrackPads.size === 0) {
            return this.#setLength(Math.min(64, Math.max(1, model.length(this.track) + v.relative)));   // SHIFT: a step
        }
        if (kind === "load" && pressed) {
            if (this.#held.size > 0) {                       // a step held: LOAD left = slide, right = accent (acid)
                const steps = this.heldSteps;
                const first = model.step(this.track, steps[0]);
                return steps.map((st) => {
                    this.#edited.add(st);
                    return d === 0 ? { type: "stepSlide", track: this.track, step: st, on: !first.slide }
                                   : { type: "stepAccent", track: this.track, step: st, on: !first.accent };
                });
            }
            const p = (model.queuedPattern ?? model.pattern) + (d === 0 ? -1 : 1);
            return [{ type: "queuePattern", pattern: clampInt(0, 15, p) }];
        }
        if (kind === "browse" && "relative" in v) return this.#browse(v.relative);
        if (kind === "browsePress" && isPress) {
            const t = this.#firstHeldTrack();
            if (t !== null) return down ? [{ type: "dice", track: t, amount: 25 }] : [];   // a track held: the dice
            if (this.page === PAGE.master && this.#packNames().length > 0) {   // load (SHIFT: put back the kit)
                if (!down) return [];
                this.packBrowsed = this.now();
                if (e.shifted) { this.packLoaded = null; this.packReverted = this.now(); return [{ type: "revertPack" }]; }
                this.packLoaded = { index: this.packCursor, at: this.now() };
                this.packReverted = null;
                return [{ type: "loadPack", pack: this.packCursor }];
            }
            return this.#audition(C.browsePress, down);
        }
        if (kind === "headphoneCue" && isPress) {
            const s = d === 0 ? 0 : 1;
            if (down && e.shifted) return [{ type: "sceneClear", scene: s }];
            this.sceneEdit = down ? s : (this.sceneEdit === s ? null : this.sceneEdit);
            return [];
        }
        if (kind === "fxSelect" && pressed) {
            // while one plays: it stops, the next starts (a throw or DUB HOLD left on would never be turned off)
            const old = this.perfOn ? this.#perfAction(false) : null;
            const at = Math.max(0, PERF_MENU.indexOf(this.perfType));
            this.perfType = PERF_MENU[(at + (e.shifted ? PERF_MENU.length - 1 : 1)) % PERF_MENU.length];
            return this.perfOn ? [old, this.#perfAction(true)] : [];
        }
        if (kind === "beatLeft" && pressed) {
            this.perfDivision = Math.max(0, this.perfDivision - 1);
            return this.perfOn ? [this.#perfAction(true)] : [];
        }
        if (kind === "beatRight" && pressed) {
            this.perfDivision = Math.min(TrigMapper.divisions.length - 1, this.perfDivision + 1);
            return this.perfOn ? [this.#perfAction(true)] : [];
        }
        if (kind === "fxOnOff" && isPress) {                 // a toggle: press on, press off
            if (!down) return [];
            this.perfOn = !this.perfOn;
            return [this.#perfAction(this.perfOn)];
        }
        if (kind === "fxChannel" && pressed) {
            this.setPerfTarget(parseControl(c).index);
            return this.perfOn ? [this.#perfAction(true)] : [];
        }
        if ("absolute" in v) {
            const x = v.absolute;
            if (kind === "fxLevel") { this.perfAmount = x; return this.perfOn ? [this.#perfAction(true)] : []; }
            if (kind === "trim") return [{ type: "deckDrive", deck: d, drive: x }];
            if (kind === "channelFader") return [{ type: "deckLevel", deck: d, level: x }];
            if (kind === "crossfader") {
                // the app starts in the middle (the pattern as written): it takes over once it passes the middle
                if (!this.crossfaderPicked) {
                    const crossed = this.#crossfaderLast !== null && (this.#crossfaderLast - 0.5) * (x - 0.5) <= 0;
                    this.#crossfaderLast = x;
                    if (!(Math.abs(x - 0.5) <= 0.06 || crossed)) return [];
                    this.crossfaderPicked = true;
                }
                return [{ type: "crossfader", value: x }];
            }
            if (kind === "tempo" && d === 0) return this.#tempo(x);
            if (kind === "tempo" && d === 1) return this.#swing(x);
        }
        if (kind === "jogTouch" && pressed && e.shifted) {
            return [{ type: "capture", deck: d, bars: 1 }];  // SHIFT + touch: resample the deck's last bar
        }
        if (kind === "jogTouch" && isPress) {
            if (!down && !this.#touching.has(d)) return [];
            if (down) this.#touching.add(d); else this.#touching.delete(d);
            this.#scratchRate.set(d, 0);
            return [{ type: "scratch", deck: d, touching: down, rate: 0 }];
        }
        if (kind === "jog" && "relative" in v) return this.#jog(d, v.relative, e.shifted);
        const s = TrigMapper.slot(c);
        if (s !== null && "absolute" in v) return this.#knob(c, s, v.absolute);
        return [];
    }

    /** Time-based releases (call ~60 times a second): a scratched jog that stops moving holds still; a nudge ends. */
    tick() {
        const out = [];
        const t = this.now();
        for (const d of this.#touching) {
            if ((this.#scratchRate.get(d) ?? 0) !== 0 && t - (this.#jogLastTime.get(d) ?? t) > 0.05) {
                this.#scratchRate.set(d, 0);
                out.push({ type: "scratch", deck: d, touching: true, rate: 0 });
            }
        }
        if (this.#nudgeLast !== null && t - this.#nudgeLast > 0.08) {
            this.#nudgeLast = null;
            out.push({ type: "nudge", amount: 0 });
        }
        return out;
    }

    /** Let everything go (focus lost, controller unplugged). */
    releaseAll() {
        const out = [...this.#playing.values()].map((p) => ({ type: "release", track: p.track, note: p.note }));
        if (this.fillHeld) out.push({ type: "fill", on: false });
        if (this.perfOn) out.push(this.#perfAction(false));
        for (const d of this.#touching) out.push({ type: "scratch", deck: d, touching: false, rate: 0 });
        if (this.#nudgeLast !== null) out.push({ type: "nudge", amount: 0 });
        const holds = [...this.#muteHolds.values()].sort((a, b) => a.at - b.at);
        if (holds.length && this.now() - holds[0].at >= HOLD_TO_RESTORE) out.push(...this.#mutesTo(holds[0].before));
        this.#muteHolds = new Map();
        this.#playing = new Map(); this.#held = new Map(); this.#setOnPress = new Set(); this.#edited = new Set();
        this.#touching = new Set(); this.fillHeld = false; this.perfOn = false;
        this.#heldTrackPads = new Map(); this.#rotateAcc = new Map(); this.#euclidPulses = new Map();
        this.#pressedPads = new Set();
        this.sceneEdit = null; this.#nudgeLast = null; this.#knobLast = new Map(); this.#knobAcc = new Map();
        this.#tempoPicked = false; this.#swingPicked = false;
        return out;
    }

    /** A new length for the active track; the pads go to the page holding its last step (to see what was added). */
    #setLength(n) {
        if (n === this.model.length(this.track)) return [];
        this.stepPage = Math.trunc((n - 1) / 16);
        this.lengthChanged = this.now();
        return [{ type: "length", track: this.track, steps: n }];
    }

    /** the active track's step pages (16 steps each) */
    get pageCount() { return Math.max(1, Math.trunc((this.model.length(this.track) + 15) / 16)); }

    #setPage(p) { this.page = p; return []; }

    #selectTrack(t) {
        const n = clampInt(0, 7, t);
        if (n === this.track) return [];
        this.track = n;
        const d = n < 4 ? 0 : 1;                         // its deck's dial points at it (re-centred unless the jog chose it)
        if (this.#jogDial.get(d)?.anchor !== n - deckBase(d)) this.#jogDial.set(d, { anchor: n - deckBase(d), pos: 0 });
        this.stepPage = Math.min(this.stepPage, this.pageCount - 1);
        return [{ type: "selectTrack", track: n }];
    }

    #perfAction(on) {
        return { type: "perf", fx: this.perfType, division: this.perfDivision, target: this.perfTarget, on,
                 amount: this.perfAmount };
    }

    /** Select a track from elsewhere (a click on its row in the strip above the controller). */
    select(track) { return this.#selectTrack(track); }

    /** The beat FX channel switch (1, 2, MASTER). */
    setPerfTarget(t) { this.perfTarget = clampInt(0, 2, t); }

    // MARK: pads

    /** whether track t plays alone (it is on, every other track muted) */
    soloed(t) {
        if (this.model.muted(t)) return false;
        for (let u = 0; u < 8; u++) if (u !== t && !this.model.muted(u)) return false;
        return true;
    }
    /** the mutes that take the tracks from now to target (8 booleans) */
    #mutesTo(target) {
        const out = [];
        for (let u = 0; u < 8; u++) if (this.model.muted(u) !== target[u]) out.push({ type: "mute", track: u, on: target[u] });
        return out;
    }
    /** solo track t; on a soloed track: the mutes from before the solo come back */
    #solo(t, before) {
        if (this.soloed(t)) {
            const back = this.#soloSaved ?? new Array(8).fill(false);
            this.#soloSaved = null;
            return this.#mutesTo(back);
        }
        if (this.#soloSaved === null) this.#soloSaved = before;   // (from one solo to the next: the first state stays)
        return this.#mutesTo(Array.from({ length: 8 }, (_, u) => u !== t));
    }

    #pad(deck, i, e) {
        const c = e.control;
        if (!("press" in e.value)) return [];
        const down = e.value.press;
        if (down) this.#pressedPads.add(c); else this.#pressedPads.delete(c);   // (lit while held, any mode)
        const model = this.model, track = this.track;
        switch (padFunctionOf(this.padMode(deck))) {
        case "steps": {
            const step = this.stepPage * 16 + (deck === 0 ? 0 : 8) + i;
            if (down) {
                this.#held.set(c, step);
                if (!model.step(track, step).trig) {
                    this.#setOnPress.add(step);
                    return [{ type: "stepSet", track, step, on: true }];
                }
                return [];
            }
            if (!this.#held.has(c)) return [];
            const s = this.#held.get(c);
            this.#held.delete(c);
            let out = [];
            if (!this.#setOnPress.has(s) && !this.#edited.has(s) && model.step(track, s).trig) {
                out = [{ type: "stepSet", track, step: s, on: false }];
            }
            if (![...this.#held.values()].includes(s)) { this.#setOnPress.delete(s); this.#edited.delete(s); }
            return out;
        }
        case "keys": {
            if (down) {
                const k = this.keyNote(deck, i);
                this.#playing.set(c, { track: k.track, note: k.note });
                return [{ type: "trigger", track: k.track, note: k.note, velocity: k.velocity }];
            }
            const p = this.#playing.get(c);
            if (p === undefined) return [];
            this.#playing.delete(c);
            return [{ type: "release", track: p.track, note: p.note }];
        }
        case "tracks": {
            const t = deckBase(deck) + (i % 4);
            if (i < 4) {                                 // select; held: the jog rotates it, BROWSE makes it Euclidean
                if (!down) { this.#heldTrackPads.delete(c); return []; }
                this.#heldTrackPads.set(c, t);
                return this.#selectTrack(t);
            }
            // the bottom row: a tap mutes / unmutes, SHIFT + a tap solos; held, either lasts only while held
            if (down) {
                const before = Array.from({ length: 8 }, (_, u) => model.muted(u));
                this.#muteHolds.set(c, { at: this.now(), before });
                if (e.shifted) return this.#solo(t, before);
                this.#soloSaved = null;
                return [{ type: "mute", track: t, on: !before[t] }];
            }
            const h = this.#muteHolds.get(c);
            this.#muteHolds.delete(c);
            if (!h || this.now() - h.at < HOLD_TO_RESTORE) return [];
            this.#soloSaved = null;
            return this.#mutesTo(h.before);
        }
        case "patterns": {
            if (!down) return [];
            const p = (deck === 0 ? 0 : 8) + i;
            return e.shifted ? [{ type: "copyPattern", from: model.pattern, to: p }] : [{ type: "queuePattern", pattern: p }];
        }
        }
        return [];
    }

    /** KEYS: a melodic track plays 16 semitones up from its TRIG note; a drum deck plays its own 4 tracks
     *  (top row full, bottom row soft). -> { track, note, velocity } */
    keyNote(deck, i) {
        const model = this.model;
        if (isMelodic(model.machine(this.track))) {
            const base = model.value(this.track, 0);
            const k = (deck === 0 ? 0 : 8) + (i < 4 ? i + 4 : i - 4);   // bottom row lower, as a keyboard reads
            return { track: this.track, note: Math.min(127, base + k), velocity: 100 };
        }
        return { track: deckBase(deck) + (i % 4), note: 60, velocity: i < 4 ? 110 : 55 };
    }

    #audition(c, down) {
        if (down) {
            const note = isMelodic(this.model.machine(this.track)) ? this.model.value(this.track, 0) : 60;
            this.#playing.set(c, { track: this.track, note });
            return [{ type: "trigger", track: this.track, note, velocity: 100 }];
        }
        const p = this.#playing.get(c);
        if (p === undefined) return [];
        this.#playing.delete(c);
        return [{ type: "release", track: p.track, note: p.note }];
    }

    // MARK: knobs, BROWSE, jogs, sliders

    /** A double-click on a control on screen: its value back to the default (Swift's reset). A page knob: the
     *  parameter's default for the track's machine (MASTER: the global's); a held step: its lock back to the track's
     *  value; a scene being edited: the scene's value back to the track's. BEAT FX LEVEL: 60 %. */
    reset(c) {
        if (c === C.fxLevel) { this.perfAmount = 0.6; return this.perfOn ? [this.#perfAction(true)] : []; }
        const slot = TrigMapper.slot(c);
        if (slot === null || slot === undefined) return [];
        const model = this.model, track = this.track;
        if (this.page === PAGE.master) {
            const info = model.globalInfo(slot);
            return info ? [{ type: "globalSet", slot, value: info.def }] : [];
        }
        const p = this.param(slot), info = model.info(track, p);
        if (!info) return [];
        const base = model.value(track, p);
        if (this.sceneEdit !== null) return [{ type: "sceneSet", scene: this.sceneEdit, track, param: p, value: base }];
        if (this.#held.size > 0 && info.lockable) {
            return this.heldSteps.map((st) => { this.#edited.add(st); return { type: "stepLock", track, step: st, param: p, value: base }; });
        }
        return [{ type: "paramSet", track, param: p, value: info.def }];
    }

    #knob(c, slot, v) {
        if (!this.#knobLast.has(c)) { this.#knobLast.set(c, v); return []; }
        const last = this.#knobLast.get(c);
        this.#knobLast.set(c, v);
        const model = this.model, track = this.track;
        const info = this.page === PAGE.master ? model.globalInfo(slot) : model.info(track, this.param(slot));
        if (!info || !(info.max > info.min)) return [];
        // a full turn sweeps the whole range; the fraction left over waits for the next movement
        const acc = (this.#knobAcc.get(c) ?? 0) + (v - last) * (info.max - info.min);
        const n = Math.trunc(acc + (acc >= 0 ? 1e-9 : -1e-9));   // (0.6 - 0.5 is 0.0999...: not a step short)
        this.#knobAcc.set(c, acc - n);
        if (n === 0) return [];
        const clamped = (x) => clampInt(info.min, info.max, x);
        if (this.page === PAGE.master) return [{ type: "globalSet", slot, value: clamped(model.globalValue(slot) + n) }];
        const p = this.param(slot);
        if (this.sceneEdit !== null) {
            const s = this.sceneEdit;
            const base = model.scene(s, track, p) ?? model.value(track, p);
            return [{ type: "sceneSet", scene: s, track, param: p, value: clamped(base + n) }];
        }
        if (this.#held.size > 0 && info.lockable) {
            return this.heldSteps.map((st) => {
                this.#edited.add(st);
                const base = model.lock(track, st, p) ?? model.value(track, p);
                return { type: "stepLock", track, step: st, param: p, value: clamped(base + n) };
            });
        }
        return [{ type: "paramSet", track, param: p, value: clamped(model.value(track, p) + n) }];
    }

    /** how many trigs a track has now (the start of a Euclidean edit) */
    trigCount(t) {
        let n = 0;
        for (let s = 0; s < this.model.length(t); s++) if (this.model.step(t, s).trig) n++;
        return n;
    }

    #browse(n) {
        const model = this.model, track = this.track;
        const t = this.#firstHeldTrack();
        if (t !== null) {                                // Euclid: n pulses spread over the track's length
            const len = model.length(t);
            const p = Math.min(len, Math.max(0, (this.#euclidPulses.get(t) ?? this.trigCount(t)) + n));
            this.#euclidPulses.set(t, p);
            return [{ type: "euclid", track: t, pulses: p, steps: len }];
        }
        if (this.#held.size > 0) {
            const k = Math.max(1, model.condCount);
            return this.heldSteps.map((st) => {
                this.#edited.add(st);
                return { type: "stepCond", track, step: st, cond: mod(model.step(track, st).cond + n, k) };
            });
        }
        if (this.page === PAGE.src) {
            return [{ type: "machine", track, machine: mod(model.machine(track) + n, MACHINE_COUNT) }];
        }
        const packs = this.#packNames();
        if (this.page === PAGE.master && packs.length > 0) {   // the sound packs: turn to choose, press to load
            this.packCursor = mod(this.packCursor + n, packs.length);
            this.packBrowsed = this.now();
            return [];
        }
        return this.#selectTrack(track + n);
    }

    /** Where a deck's track-ring marker points, in quadrants: 0 is the centre of the deck's first track, 1 the
     *  second's (0..<4, then around). On the active track's quadrant when the active track is on this deck
     *  (re-centred when it was chosen another way), else where the deck's marker was left. */
    jogDialPosition(d) {
        const dial = this.#currentDial(d);
        return dial.anchor + dial.pos / this.jogTicksPerTrack;
    }

    #currentDial(d) {
        const base = deckBase(d);
        const dial = this.#jogDial.get(d) ?? { anchor: 0, pos: 0 };
        if (inDeck(d, this.track) && this.track - base !== dial.anchor) return { anchor: this.track - base, pos: 0 };
        return { anchor: dial.anchor, pos: dial.pos };
    }

    #jog(d, n, shifted) {
        const t = this.now();
        try {
            return this.#jogAt(d, n, shifted, t);
        } finally {
            this.#jogLastTime.set(d, t);
        }
    }

    #jogAt(d, n, shifted, t) {
        const model = this.model;
        if (this.#touching.has(d)) {                     // scratch: the platter's speed is the playback rate
            const dt = Math.max(0.002, t - (this.#jogLastTime.get(d) ?? t - 0.01));
            const r = Math.max(-4, Math.min(4, n / dt / TrigMapper.ticksPerSecondAtSpeed));
            const rate = 0.5 * (this.#scratchRate.get(d) ?? 0) + 0.5 * r;
            this.#scratchRate.set(d, rate);
            return [{ type: "scratch", deck: d, touching: true, rate }];
        }
        const tr = this.#firstHeldTrack();
        if (tr !== null) {                               // a track held: rotate it, one step per 8 ticks
            const acc = (this.#rotateAcc.get(d) ?? 0) + n;
            const k = Math.trunc(acc / 8);
            this.#rotateAcc.set(d, acc - k * 8);
            return k !== 0 ? [{ type: "rotate", track: tr, amount: k }] : [];
        }
        if (this.#held.size > 0) {                       // micro timing of the held steps: 1 per 4 ticks
            const q = Math.trunc(n / 4);
            const delta = q === 0 ? (n > 0 ? 1 : -1) : q;
            const track = this.track;
            return this.heldSteps.map((st) => {
                this.#edited.add(st);
                const m = model.step(track, st).micro + delta;
                return { type: "stepMicro", track, step: st, micro: clampInt(-23, 23, m) };
            });
        }
        if (shifted) {                                   // SHIFT + jog: a DJ nudge of the tempo
            this.#nudgeLast = t;
            return [{ type: "nudge", amount: Math.max(-1, Math.min(1, n / 6)) }];
        }
        // the jog's side: a dial around the deck's ring of four tracks (left: 1-4, right: 5-8). Its marker turns with
        // the platter, a quarter turn a track; the quadrant it points into is the active track.
        const base = deckBase(d);
        const dial = this.#currentDial(d);
        dial.pos += n;
        const edge = Math.trunc(this.jogTicksPerTrack / 2) + this.jogHysteresis;
        while (dial.pos > edge) { dial.pos -= this.jogTicksPerTrack; dial.anchor = (dial.anchor + 1) % 4; }
        while (dial.pos < -edge) { dial.pos += this.jogTicksPerTrack; dial.anchor = (dial.anchor + 3) % 4; }
        this.#jogDial.set(d, dial);
        if (inDeck(d, this.track)) return this.#selectTrack(base + dial.anchor);   // (the same track: nothing)
        // the active track is on the other deck: a short turn of this one brings it here, to its marker's quadrant
        const idle = t - (this.#jogLastTime.get(d) ?? 0) > 0.3;
        const acc = (idle ? 0 : (this.#jogFocusAcc.get(d) ?? 0)) + Math.abs(n);
        this.#jogFocusAcc.set(d, acc >= this.jogFocusTicks ? 0 : acc);
        return acc >= this.jogFocusTicks ? this.#selectTrack(base + dial.anchor) : [];
    }

    #tempo(v) {
        const model = this.model;
        const bpm = roundSwift(60 + v * 140);            // towards you (down, the "+" end) = faster
        if (this.#lastTempoSet !== null && Math.abs(model.bpm - this.#lastTempoSet) > 1.5) this.#tempoPicked = false;
        if (!this.#tempoPicked && Math.abs(bpm - model.bpm) > 2) return [];   // soft takeover
        this.#tempoPicked = true;
        this.#lastTempoSet = bpm;
        return [{ type: "tempo", bpm }];
    }

    #swing(v) {
        const model = this.model;
        const s = roundSwift(50 + v * 30);
        if (this.#lastSwingSet !== null && model.swing !== this.#lastSwingSet) this.#swingPicked = false;
        if (!this.#swingPicked && Math.abs(s - model.swing) > 1) return [];
        this.#swingPicked = true;
        this.#lastSwingSet = s;
        return [{ type: "swing", swing: s }];
    }

    // MARK: faces

    /** what the screen's header shows */
    get header() {
        const model = this.model;
        return {
            pattern: model.pattern, queued: model.queuedPattern ?? null, bpm: model.bpm, swing: model.swing,
            playing: model.playing, recording: model.recording, fill: this.fillHeld, track: this.track,
            machine: model.machineName(model.machine(this.track)), page: this.page, stepPage: this.stepPage,
            pages: this.pageCount, perf: this.perfOn ? (PERF_FX[this.perfType] ?? null) : null,
            sceneEdit: this.sceneEdit, heldSteps: this.heldSteps,
        };
    }

    /** What control c shows (a face, see vocab.js), or null for a control with no job. */
    face(c) {
        const model = this.model;
        const trackColor = COLORS.track[this.track];
        const pc = parseControl(c);
        const d = pc.deck;
        switch (pc.kind) {
        case "pad": return this.padFace(d, pc.index, this.padMode(d));
        case "padModeButton":
            return face(PAD_FUNCTION_NAME[padFunctionOf(pc.mode)], { light: this.padMode(d) === pc.mode ? ON : OFF });
        case "play":
            return face(model.playing ? "STOP" : "PLAY", { color: 0xFF1ECC70, light: model.playing ? ON : OFF });
        case "cue":
            if (d === 0) return face("REC", { color: 0xFFFF2C34, light: model.recording ? ON : OFF });
        {   // (SHIFT + TAP: back to the tempo before the taps; shown while there is one to go back to)
            const b = this.tempoBeforeTap, back = b && b.pattern === model.pattern ? b.bpm : null;
            return face(back !== null ? `TAP · SHIFT: ${roundSwift(back)}` : "TAP", { value: String(roundSwift(model.bpm)) });
        }
        case "sync":
            if (d === 0) {
                return face("FILL", { value: model.autopilot ? "AUTOPILOT" : null,
                                      light: this.fillHeld || model.autopilot ? ON : OFF });
            }
            return face("MASTER", { color: COLORS.page[6], light: this.page === PAGE.master ? ON : OFF });
        case "loopIn": return this.#pageFace(d === 0 ? PAGE.trig : PAGE.amp);
        case "loopOut": return this.#pageFace(d === 0 ? PAGE.src : PAGE.fx);
        case "reloop": return this.#pageFace(d === 0 ? PAGE.fltr : PAGE.lfo);
        case "callLeft":
            if (d === 0) return face("TRK -");
            // (the value: which of the track's step pages the pads show)
            return face("PAGE -", { value: `${this.stepPage + 1}/${this.pageCount}`, light: this.stepPage > 0 ? DIM : OFF });
        case "callRight":
            if (d === 0) return face("TRK +");
            return face("PAGE +", { value: `${this.stepPage + 1}/${this.pageCount}`,
                                    light: this.stepPage < this.pageCount - 1 ? DIM : OFF });
        case "load": {
            const steps = this.heldSteps;
            if (steps.length > 0) {
                const sv = model.step(this.track, steps[0]);
                return d === 0 ? face("SLIDE", { light: sv.slide ? ON : OFF })
                               : face("ACCENT", { light: sv.accent ? ON : OFF });
            }
            return face(d === 0 ? "PAT -" : "PAT +");
        }
        case "browse": {
            const t = this.#firstHeldTrack();
            if (t !== null) {
                return face("EUCLID", { value: `${this.#euclidPulses.get(t) ?? this.trigCount(t)}/${model.length(t)}`,
                                        color: COLORS.track[t] });
            }
            const steps = this.heldSteps;
            if (steps.length > 0) return face("COND", { value: model.condName(model.step(this.track, steps[0]).cond) });
            if (this.page === PAGE.src) {
                return face("MACHINE", { value: model.machineName(model.machine(this.track)), color: trackColor });
            }
            const packs = this.#packNames();
            if (this.page === PAGE.master && this.packCursor < packs.length) {
                return face("PACK", { value: packs[this.packCursor], color: COLORS.page[PAGE.master] });
            }
            return face("TRACK", { value: String(this.track + 1), color: trackColor });
        }
        case "browsePress":
            if (this.#heldTrackPads.size === 0 && this.page === PAGE.master && this.#packNames().length > 0) {
                return face("LOAD PACK");
            }
            return face(this.#heldTrackPads.size === 0 ? "AUDITION" : "DICE");
        case "headphoneCue": {
            const s = d === 0 ? 0 : 1;
            return face(s === 0 ? "SCENE A" : "SCENE B", { color: s === 0 ? COLORS.sceneA : COLORS.sceneB,
                                                           light: this.sceneEdit === s ? ON : OFF });
        }
        case "fxSelect": return face("FX", { value: PERF_FX[this.perfType] ?? null });
        case "beatLeft": return face("DIV-", { value: TrigMapper.divisions[this.perfDivision] });
        case "beatRight": return face("DIV+", { value: TrigMapper.divisions[this.perfDivision] });
        case "fxOnOff": return face(PERF_FX[this.perfType] ?? "FX", { color: 0xFF2E7CFF, light: this.perfOn ? ON : OFF });
        case "fxLevel":
            return face("AMOUNT", { value: `${Math.trunc(this.perfAmount * 100)}%`, arc: this.perfAmount });
        case "fxChannel": {
            const n = pc.index;
            return face(["DECK A", "DECK B", "MASTER"][clampInt(0, 2, n)], { light: this.perfTarget === n ? ON : OFF });
        }
        case "trim": return face(d === 0 ? "DRIVE A" : "DRIVE B");
        case "channelFader": return face(d === 0 ? "DECK A" : "DECK B");
        case "crossfader": return face("SCENE A ◀ AS WRITTEN ▶ SCENE B", { value: this.crossfaderPicked ? null : "MOVE THROUGH THE MIDDLE" });
        case "tempo":
            if (d === 0) return face("TEMPO", { value: format0(model.bpm) });
            return face("SWING", { value: `${model.swing}%` });
        case "jog": case "jogTouch": {
            const t = this.#firstHeldTrack();
            if (t !== null) {
                return face("ROTATE", { value: model.machineName(model.machine(t)), color: COLORS.track[t] });
            }
            const steps = this.heldSteps;
            if (steps.length > 0) return face("MICRO", { value: String(model.step(this.track, steps[0]).micro) });
            const here = inDeck(d, this.track);
            return face(d === 0 ? "DECK A" : "DECK B", {
                value: here ? `${this.track + 1} ` + model.machineName(model.machine(this.track)) : "TURN: TRACK",
                color: here ? trackColor : null, light: this.#touching.has(d) ? ON : OFF,
            });
        }
        case "shift": return face("SHIFT");
        default: {
            const s = TrigMapper.slot(c);
            return s !== null ? this.knobFace(s) : null;
        }
        }
    }

    #pageFace(p) {
        return face(TRIG_PAGES[p], { color: COLORS.page[p], light: this.page === p ? ON : OFF });
    }

    knobFace(slot) {
        const model = this.model, track = this.track;
        const color = COLORS.knobRow[slot % 4];
        if (this.page === PAGE.master) {
            const info = model.globalInfo(slot);
            if (!info) return face(NO_PARAM, { color });
            const v = model.globalValue(slot);
            return face(info.name, { value: model.format(info, v), arc: arcOf(info, v), color });
        }
        const p = this.param(slot);
        const info = model.info(track, p);
        if (!info) return face(NO_PARAM, { color });
        let v = model.liveValue(track, p);
        let badge = null;
        const steps = this.heldSteps;
        if (this.sceneEdit !== null) {
            v = model.scene(this.sceneEdit, track, p) ?? model.value(track, p);
            badge = this.sceneEdit === 0 ? "A" : "B";
        } else if (steps.length > 0) {
            const l = model.lock(track, steps[0], p);
            if (l !== null && l !== undefined) { v = l; badge = "LOCK"; } else v = model.value(track, p);
        } else {                                      // a parameter the scenes move (the crossfader turns this knob)
            const a = model.scene(0, track, p) != null, b = model.scene(1, track, p) != null;
            badge = a && b ? "A·B" : a ? "A" : b ? "B" : null;
        }
        // the full name: the mixer has room for it (CUTOFF, not CUT)
        return face(info.name, { value: model.format(info, v), arc: arcOf(info, v), color, badge });
    }

    /** pad i of deck d's face in a pad mode (a held pad of the deck's current mode is lit) */
    padFace(d, i, mode) {
        const f = this.#padFaceUnpressed(d, i, mode);
        if (this.#pressedPads.has(C.pad(d, i)) && mode === this.padMode(d)) f.light = ON;   // lit while held
        return f;
    }

    #padFaceUnpressed(d, i, mode) {
        const model = this.model, track = this.track;
        switch (padFunctionOf(mode)) {
        case "steps": {
            const step = this.stepPage * 16 + (d === 0 ? 0 : 8) + i;
            if (!(step < model.length(track))) return face("");
            const sv = model.step(track, step);
            const badges = [];
            if (sv.locks > 0) badges.push("•");
            if (sv.cond !== 0) badges.push(model.condName(sv.cond));
            if (sv.retrig > 0) badges.push(`x${sv.retrig + 1}`);
            if (sv.slide) badges.push("~");
            if (sv.accent) badges.push("!");
            const isHeld = [...this.#held.values()].includes(step);
            return face(String(step + 1), {
                color: COLORS.track[track], light: sv.trig ? ON : (sv.locks > 0 ? DIM : OFF),
                badge: badges.length === 0 ? null : badges.join(" "),
                outlined: isHeld || model.playingStep(track) === step,
            });
        }
        case "keys": {
            const k = this.keyNote(d, i);
            const title = isMelodic(model.machine(track)) ? midiNoteName(k.note)
                : model.machineName(model.machine(k.track)) + (k.velocity < 100 ? "·" : "");
            return face(title, { color: COLORS.track[k.track], light: DIM });
        }
        case "tracks": {
            const t = deckBase(d) + (i % 4);
            if (i < 4) {
                return face(model.machineName(model.machine(t)), { value: String(t + 1), color: COLORS.track[t],
                                                                   light: t === track ? ON : DIM });
            }
            const muted = model.muted(t), solo = this.soloed(t);
            return face(muted ? "MUTED" : (solo ? "SOLO" : "ON"), { color: COLORS.track[t], light: muted ? OFF : (solo ? ON : DIM) });
        }
        case "patterns": {
            const p = (d === 0 ? 0 : 8) + i;
            const number = `P${String(p + 1).padStart(2, "0")}`, name = model.patternName?.(p) ?? "";   // (its name, its number under)
            return face(name || number, { value: name ? number : null, light: p === model.pattern ? ON : OFF,
                                          outlined: p === (model.queuedPattern ?? null) });
        }
        }
        return face("");
    }

    /** Every control's light, for the DDJ-400's LEDs: Map(control -> light). */
    lights() {
        const out = new Map();
        for (const c of TrigMapper.litControls) {
            const f = this.face(c);
            if (f) out.set(c, f.light);
        }
        return out;
    }

    /** The pads' lights in a given mode, [deck][pad] (the hardware shows each mode's own LEDs when switched to it).
     *  In STEPS, the playing step's pad flips (Elektron's running light): the hardware cannot draw the outline. */
    padLights(mode) {
        const isSteps = padFunctionOf(mode) === "steps";
        const playingStep = isSteps ? this.model.playingStep(this.track) : null;
        return DECKS.map((d) => Array.from({ length: 8 }, (_, i) => {
            const f = this.padFace(d, i, mode);
            const step = this.stepPage * 16 + (d === 0 ? 0 : 8) + i;
            if (isSteps && playingStep === step && !this.#pressedPads.has(C.pad(d, i))) return f.light === ON ? OFF : ON;
            return f.light;
        }));
    }

    static litControls = [
        ...DECKS.flatMap((d) => [
            C.play(d), C.cue(d), C.sync(d), C.loopIn(d), C.loopOut(d), C.reloop(d), C.callLeft(d), C.callRight(d),
            C.headphoneCue(d), ...Array.from({ length: 8 }, (_, i) => C.pad(d, i)),
            ...PAD_MODES.map((m) => C.padModeButton(d, m)),
        ]),
        C.fxOnOff,
    ];
}

function arcOf(info, v) {
    return info.max > info.min ? (v - info.min) / (info.max - info.min) : 0;
}
