// SPDX-License-Identifier: GPL-3.0-only
// The shared vocabulary of TRIGDECK's web version, one for one with the Swift side (Sources/DDJMapping/DDJTypes.swift
// and TrigTypes.swift): the DDJ's controls, the events they make, what a control shows (its face), what the engine is
// asked to do (actions), and what the mapper reads from the engine (the model). Plain values, no browser APIs.
//
// Conventions (both the logic in mapping/ and the drawing in view/ rely on them):
// - a deck is 0 (left) or 1 (right);
// - a control is a string key, made by the functions in C (C.play(0) === "play:0", C.pad(1, 3) === "pad:1:3",
//   C.padModeButton(0, "sampler") === "padModeButton:0:sampler", C.crossfader === "crossfader"), so it can key a Map;
//   parseControl(key) gives { kind, deck, index, mode } back;
// - an event is { control, value, padMode, shifted } with value one of { press: bool }, { absolute: 0..1 },
//   { relative: ticks } (Swift's DDJValue);
// - a light is 0 (off), 1 (dim), 2 (on) (Swift's LightLevel);
// - a face is { title, value, arc, color, light, badge, outlined } (Swift's ControlFace; color: ARGB number or null);
// - an action is { type, ...fields } with Swift's case name as type and its labels as fields; unlabelled payloads use
//   the names given in ACTION_FIELDS below (Swift .tempo(120) is { type: "tempo", bpm: 120 }).

export const DECKS = [0, 1];

/** Pad modes: the DDJ sends a different note range in each (Mixxx's mapping). Index order as Swift's PadMode. */
export const PAD_MODES = ["hotCue", "beatLoop", "beatJump", "sampler", "keyboard", "padFX1", "padFX2", "keyShift"];
export const PAD_MODE_OFFSET = { hotCue: 0x00, beatLoop: 0x60, beatJump: 0x20, sampler: 0x30,
                                 keyboard: 0x40, padFX1: 0x10, padFX2: 0x50, keyShift: 0x70 };
export const PAD_MODE_LABEL = { hotCue: "HOT CUE", beatLoop: "BEAT LOOP", beatJump: "BEAT JUMP", sampler: "SAMPLER",
                                keyboard: "KEYBOARD", padFX1: "PAD FX1", padFX2: "PAD FX2", keyShift: "KEY SHIFT" };

/** The controls (Swift's DDJControl). Deck controls are functions of the deck; the rest are constants. */
export const C = {
    play: (d) => `play:${d}`, cue: (d) => `cue:${d}`, sync: (d) => `sync:${d}`, shift: (d) => `shift:${d}`,
    loopIn: (d) => `loopIn:${d}`, loopOut: (d) => `loopOut:${d}`, reloop: (d) => `reloop:${d}`,
    callLeft: (d) => `callLeft:${d}`, callRight: (d) => `callRight:${d}`,
    pad: (d, i) => `pad:${d}:${i}`,                       // 0..7: top row 0-3, bottom row 4-7
    padModeButton: (d, mode) => `padModeButton:${d}:${mode}`,
    jog: (d) => `jog:${d}`, jogTouch: (d) => `jogTouch:${d}`, tempo: (d) => `tempo:${d}`,
    channelFader: (d) => `channelFader:${d}`, trim: (d) => `trim:${d}`, eqHi: (d) => `eqHi:${d}`, eqMid: (d) => `eqMid:${d}`,
    eqLow: (d) => `eqLow:${d}`, cfx: (d) => `cfx:${d}`, headphoneCue: (d) => `headphoneCue:${d}`, load: (d) => `load:${d}`,
    crossfader: "crossfader", browse: "browse", browsePress: "browsePress",
    beatLeft: "beatLeft", beatRight: "beatRight", fxSelect: "fxSelect", fxOnOff: "fxOnOff", fxLevel: "fxLevel",
    fxChannel: (n) => `fxChannel:${n}`,                   // 0 CH1, 1 CH2, 2 MASTER
    headphoneLevel: "headphoneLevel",
};

/** "pad:1:3" -> { kind: "pad", deck: 1, index: 3 }; "padModeButton:0:sampler" -> { kind, deck: 0, mode: "sampler" };
 *  "fxChannel:2" -> { kind: "fxChannel", index: 2 }; "crossfader" -> { kind: "crossfader" } */
export function parseControl(key) {
    const [kind, a, b] = key.split(":");
    if (kind === "fxChannel") return { kind, index: Number(a) };
    if (a === undefined) return { kind };
    const out = { kind, deck: Number(a) };
    if (kind === "pad") out.index = Number(b);
    if (kind === "padModeButton") out.mode = b;
    return out;
}
export const deckOf = (key) => parseControl(key).deck;

/** An event (Swift's DDJEvent). */
export const ev = (control, value, { padMode = null, shifted = false } = {}) => ({ control, value, padMode, shifted });
export const press = (down) => ({ press: down });
export const absolute = (x) => ({ absolute: x });
export const relative = (n) => ({ relative: n });

export const OFF = 0, DIM = 1, ON = 2;

/** A face (Swift's ControlFace). */
export const face = (title, { value = null, arc = null, color = null, light = OFF, badge = null, outlined = false } = {}) =>
    ({ title, value, arc, color, light, badge, outlined });

export const TRIG_PAGES = ["TRIG", "SRC", "FLTR", "AMP", "FX", "LFO", "MASTER"];   // index = TrigPage.rawValue
export const PAGE = { trig: 0, src: 1, fltr: 2, amp: 3, fx: 4, lfo: 5, master: 6 };
export const PERF_FX = ["REPEAT", "STUTTER", "TAPE STOP", "DLY THROW", "REV THROW", "FILTER", "CRUSH", "DUB HOLD",
                        "ECHO", "FLANGER", "PHASER", "TRANS", "AUTOPAN", "REVERSE", "HALF", "NOISE", "DISTORT", "SWEEP"];
/** FX SELECT's order, as indices into PERF_FX (FILTER first; TAPE STOP left out); Swift's PerfFX.menu */
export const PERF_MENU = [5, 4, 0, 1, 3, 8, 6, 7, 9, 10, 11, 12, 13, 14, 15, 16, 17];
export const MACHINE = { kick: 0, snare: 1, hat: 2, clap: 3, tom: 4, synth: 5, fm: 6, sample: 7 };
export const isMelodic = (m) => m === MACHINE.synth || m === MACHINE.fm || m === MACHINE.sample;

/** Unlabelled Swift payloads -> field names. Labelled ones keep Swift's labels (stepSet: { track, step, on }). */
export const ACTION_FIELDS = {
    fill: ["on"], tempo: ["bpm"], swing: ["swing"], nudge: ["amount"], selectTrack: ["track"], page: ["page"],
    stepPage: ["page"], queuePattern: ["pattern"], sceneClear: ["scene"], crossfader: ["value"], autopilot: ["on"],
    loadPack: ["pack"],
};

/** The colours (Swift's TrigColors), ARGB. */
export const COLORS = {
    knobRow: [0xFF2E7CFF, 0xFF1ECC70, 0xFFE8E8EC, 0xFFFF621A],
    track: [0xFFFF4A3D, 0xFFFF9F1C, 0xFFFFD60A, 0xFF7CDB3C, 0xFF2EC4F2, 0xFF4D7CFF, 0xFFA66BFF, 0xFFFF5FB4],
    page: [0xFFF2F2F2, 0xFFFFD60A, 0xFF2EC4F2, 0xFF7CDB3C, 0xFFA66BFF, 0xFFFF9F1C, 0xFFFF4A3D],
    sceneA: 0xFF2E7CFF, sceneB: 0xFFFF621A,
};
/** ARGB number -> CSS colour */
export const css = (argb, alpha = 1) =>
    `rgba(${(argb >>> 16) & 255},${(argb >>> 8) & 255},${argb & 255},${(((argb >>> 24) & 255) / 255) * alpha})`;

/** "C4" for MIDI 60 */
export function midiNoteName(m) {
    const names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
    m = Math.max(0, Math.min(127, m));
    return names[m % 12] + String(Math.floor(m / 12) - 1);
}

/**
 * The model the mapper reads (Swift's TrigModel). The app implements it over the engine's state; tests use a fake.
 * @typedef {Object} TrigModel
 * @property {boolean} playing @property {boolean} recording @property {number} pattern @property {?number} queuedPattern
 * @property {number} bpm @property {number} swing @property {boolean} autopilot @property {string[]} packNames
 * @property {number} condCount
 * machine(track) -> machine index; muted(track) -> bool; length(track) -> steps; playingStep(track) -> step or null;
 * info(track, param) -> { name, shortName, min, max, def, lockable } or null (param = page * 8 + slot);
 * globalInfo(slot) -> the same for the MASTER page; value(track, param), liveValue(track, param), globalValue(slot) -> int;
 * format(info, value) -> "2.4 kHz"; step(track, step) -> { trig, note, velocity, cond, retrig, micro, locks, slide,
 * accent }; lock(track, step, param) -> int or null; scene(scene, track, param) -> int or null; condName(cond) -> "1:2";
 * machineName(m) -> "KICK"; trackLevel(track) -> 0..1.
 */
