// SPDX-License-Identifier: GPL-3.0-only
// The on-screen DDJ-400 (Swift's DDJ400View and DDJ400View+Drawing), on a canvas: a flat drawing of the controller's
// top panel in the app's style where every control shows its face (title, value, arc, colour, light, badge, outline),
// the jogs show their deck's tracks, and a header in the mixer carries the global state. It moves as the hardware is
// played (show(event), meters) and turns clicks, drags and scrolls into the same events the hardware sends.
//
// Public state (assign a new value to change it; each assignment asks for one redraw at the next animation frame):
//   faces        Map (or object) control key -> face (vocab.js face(): title, value, arc, color, light, badge, outlined)
//   lights       Map control -> 0 | 1 | 2 (OFF, DIM, ON), for controls without a face
//   captions     Map control -> string, for controls without a face
//   knobAccents  Map control -> ARGB colour, for controls without a face
//   header       null (the connection line) or { pattern, queued, bpm, swing, playing, recording, fill, track, machine,
//                page (index into TRIG_PAGES), stepPage, pages, perf, sceneEdit } (Swift's TrigHeader)
//   deckTracks   [left, right], each an array of { name, color (ARGB), level 0..1, active, muted }
//   jogDials     [left, right], each null or a position in quadrants of the deck's ring
//   takeovers    Map control -> { target 0..1, active }
//   padModes     [left, right], PAD_MODES names; padMode sets both
//   meters       [left, right], 0..1
//   connected    boolean
//   selectedTrack 0..3; heldButton (control or null), heldColor (ARGB); highlighted (Set of controls)
//   values       Map control -> 0..1 (positions given by the hardware, setPosition or the mouse)
//   helpText     null or (control) => string, shown as the canvas's tooltip
// Methods: show(event), setPosition(control, v), clearPressed(), onEvent(cb), onHover(cb), setDesignWidth(w),
//   resize(cssW, cssH), requestRedraw(), step(control, dir), press(control), pointerUp(), columnFractions.

import { C, ev, press as pressV, absolute, relative, OFF, DIM, ON, parseControl, COLORS, TRIG_PAGES, PAD_MODE_LABEL } from "../vocab.js";
import { S, TRACK4, col, Rect, drawText, box, disc, ellipsePath, fillRect, lineHeight, charWidth, textWidth, maxChars,
         truncate, wrap, ink, sizeCanvas } from "./style.js";

export const MIN_DESIGN_W = 1240, MAX_DESIGN_W = 1720, DESIGN_H = 700;
const METER_SEGMENTS = 15;
const FLASH_DURATION = 0.8, FLASH_ATTRIBUTION = 0.5;
const HW_JOG_RAD_PER_TICK = 2 * Math.PI / 720;
const JOG_TICKS_PER_SCROLL = 15, JOG_TICKS_PER_STEP = 45;
const BROWSE_RAD_PER_TICK = Math.PI / 10;
const DRAG_RANGE = 200;

const clamp = (v) => Math.min(1, Math.max(0, v));
const toMap = (v) => (v instanceof Map ? new Map(v) : new Map(Object.entries(v ?? {})));
const BASE_MODE = { keyboard: "hotCue", padFX1: "beatLoop", padFX2: "beatJump", keyShift: "sampler" };
const baseMode = (m) => BASE_MODE[m] ?? m;
const segments = (v) => Math.round(clamp(v) * METER_SEGMENTS);
const lvlMax = (a, b) => Math.max(a, b);

/** the layout's measures for a design width (Swift's static layout properties) */
export function geometry(designW) {
    const extraW = designW - MIN_DESIGN_W;
    const deckW = Math.round(440 + extraW * 0.3);
    const deckShift = Math.round((deckW - 440) / 2);
    const mixerX = deckW, rightDeckX = designW - deckW;
    const mixerExtra = rightDeckX - mixerX - 360;
    const leftColX = Math.round(mixerX + 60 + mixerExtra * 0.2);
    const rightColX = Math.round(rightDeckX - 90 - mixerExtra * 0.2);
    const mixerMid = (leftColX + rightColX) / 2;
    return {
        designW, deckW, deckShift, mixerX, rightDeckX, mixerExtra, leftColX, rightColX, mixerMid,
        leftColMid: (mixerX + leftColX) / 2, rightColMid: (rightColX + rightDeckX) / 2,
        channelHalf: Math.round(50 + mixerExtra * 0.15),
        beatFXBox: new Rect(rightColX + 4, 222, rightDeckX - rightColX - 8, 350),
        meterRect: new Rect(mixerMid - 10, 222, 20, 178),
        statusRect: new Rect(mixerX + 2, 0, rightDeckX - mixerX - 4, 46),
    };
}

/** the controls' places (Swift's makeLayout). caption: { at: [x, y], width } | { above: width } | { side, width } | "inside" */
function makeLayout(G) {
    const out = [];
    const add = (control, kind, rect, label, labelPos, caption, valuePos = null, extra = {}) => {
        let v = valuePos;
        if (!v) {
            if (caption.side !== undefined) v = "block";
            else if (kind === "knob" || kind === "browse") v = "below";
            else if (kind === "vfader") v = "underTitle";
            else if (kind === "hfader") v = "inline";
            else v = "inside";
        }
        out.push({ control, kind, rect, label, labelPos, caption, valuePos: v, ...extra });
    };
    const at = (x, y, width) => ({ at: [x, y], width });
    const above = (width) => ({ above: width });
    const circle = Rect.circle, centered = Rect.centered;
    for (const deck of [0, 1]) {
        const origin = deck === 0 ? 0 : G.rightDeckX;
        const x0 = origin + G.deckShift, edge = origin + G.deckW;
        const capY = 36;
        add(C.loopIn(deck), "round", circle(x0 + 44, 72, 19), "IN/4BEAT", "below", at(x0 + 44, capY, 54), "inside");
        add(C.loopOut(deck), "round", circle(x0 + 100, 72, 19), "OUT", "below", at(x0 + 100, capY, 54), "inside");
        add(C.reloop(deck), "round", circle(x0 + 154, 72, 14), "RELOOP/EXIT", "below", at(x0 + 154, capY, 50), "inside");
        add(C.callLeft(deck), "button", centered(x0 + 212, 72, 26, 24), "◀", "inside", at(x0 + 212, capY, 58));
        add(C.callRight(deck), "button", centered(x0 + 274, 72, 26, 24), "▶", "inside", at(x0 + 274, capY, 58));
        add(C.sync(deck), "round", circle(edge - 42, 72, 19), "BEAT SYNC", "below", at(edge - 42, capY, 80), "inside");
        add(C.jog(deck), "jog", circle(x0 + 200, 258, 128), "JOG", "none", "inside", "inside");
        add(C.tempo(deck), "vfader", new Rect(edge - 45, 288, 28, 330), "TEMPO", "below", at(edge - 31, 250, 62));
        add(C.shift(deck), "shiftButton", centered(x0 + 46, 452, 52, 22), "SHIFT", "inside", above(84));
        add(C.cue(deck), "round", circle(x0 + 46, 530, 30), "CUE", "inside", above(86));
        add(C.play(deck), "round", circle(x0 + 46, 626, 30), "▶/II", "inside", above(86));
        const padX0 = x0 + 97, padW = 66, gap = 8;
        const modes = [["hotCue", "keyboard"], ["beatLoop", "padFX1"], ["beatJump", "padFX2"], ["sampler", "keyShift"]];
        modes.forEach(([base, alt], i) => {
            const r = new Rect(padX0 + i * (padW + gap), 428, padW, 20);
            add(C.padModeButton(deck, base), "mode", r, PAD_MODE_LABEL[base], "inside", above(padW + 4), null, { base, alt });
        });
        for (let i = 0; i < 8; i++) {
            const c = i % 4, rowY = i < 4 ? 470 : 560;
            add(C.pad(deck, i), "pad", new Rect(padX0 + c * (padW + gap), rowY, padW, 84), "", "none", "inside");
        }
    }
    const mid = G.mixerMid, chL = G.mixerMid - G.channelHalf, chR = G.mixerMid + G.channelHalf;
    add(C.load(0), "button", centered(chL - 10, 80, 46, 22), "LOAD", "inside", at(chL - 10, 56, 70));
    add(C.browse, "browse", circle(mid, 80, 18), "BROWSE", "below", at(mid, 56, 70));
    add(C.load(1), "button", centered(chR + 10, 80, 46, 22), "LOAD", "inside", at(chR + 10, 56, 70));
    for (const deck of [0, 1]) {
        const x = deck === 0 ? chL : chR, n = deck === 0 ? "1" : "2";
        const knobs = [[C.trim(deck), "TRIM", 166], [C.eqHi(deck), "HI", 238], [C.eqMid(deck), "MID", 310],
                       [C.eqLow(deck), "LOW", 382], [C.cfx(deck), "FILTER", 454]];
        for (const [c, label, y] of knobs) add(c, "knob", circle(x, y, 14), label, "below", above(92));
        add(C.headphoneCue(deck), "button", centered(x, 524, 42, 20), "CUE", "inside", above(92));
        add(C.channelFader(deck), "vfader", new Rect(x - 14, 562, 28, 86), n, "none", at(x, 549, 92), "inline");
    }
    add(C.crossfader, "hfader", new Rect(mid - 56, 676, 112, 18), "CROSSFADER", "none", at(mid, 662, 200));
    add(C.headphoneLevel, "knob", circle(G.leftColMid, 360, 13), "MIXING", "below", above(58));
    const rx = G.rightColMid;
    add(C.beatLeft, "button", centered(rx - 21, 262, 36, 20), "◀", "inside", above(44));
    add(C.beatRight, "button", centered(rx + 21, 262, 36, 20), "▶", "inside", above(44));
    add(C.fxSelect, "button", centered(rx, 330, 70, 20), "FX SELECT", "inside", above(86));
    for (let n = 0; n < 3; n++) {
        add(C.fxChannel(n), "segment", new Rect(rx - 38, 358 + n * 18, 76, 16), ["CH 1", "CH 2", "MASTER"][n], "inside", "inside", "inside");
    }
    add(C.fxLevel, "knob", circle(rx, 452, 14), "LEVEL/DEPTH", "below", above(86));
    add(C.fxOnOff, "round", circle(rx, 530, 18), "ON/OFF", "below", above(86), "inside");
    return out;
}

const isRound = (it) => it.kind === "round" || it.kind === "knob" || it.kind === "browse" || it.kind === "jog";
export const knobAngle = (v) => -Math.PI / 2 + (v - 0.5) * 1.5 * Math.PI;
/** the centre of track i's part of a jog's ring of n: track 1 at the top left, then clockwise */
export function ringCentreAngle(i, n) {
    const span = 2 * Math.PI / Math.max(1, n);
    return -Math.PI * 3 / 4 + i * span + (n === 4 ? 0 : span / 2 - Math.PI / 4);
}
/** how far down its slot a vertical fader's value sits (0 top): channel faders send 1 at the top, tempo at the bottom */
const downFraction = (c, v) => (parseControl(c).kind === "tempo" ? v : 1 - v);

export class DDJView {
    constructor(canvas) {
        this.canvas = canvas;
        this.g = canvas.getContext("2d");
        this.G = geometry(MIN_DESIGN_W);
        this.items = makeLayout(this.G);
        this._index();

        this._faces = new Map(); this._lights = new Map(); this._captions = new Map(); this._accents = new Map();
        this._header = null; this._deckTracks = [[], []]; this._jogDials = [null, null]; this._takeovers = new Map();
        this._padModes = ["hotCue", "hotCue"]; this._meters = [0, 0]; this._connected = false; this._selectedTrack = 0;
        this._heldButton = null; this._heldColor = S.fg; this._highlighted = new Set();
        this.helpText = null;

        this._values = new Map();
        this.explicitPosition = new Set();
        this.hwPressed = new Set();
        this.mouseHeld = null;
        this.hovered = null;
        this.shiftLatched = new Set();
        this.jogAngle = [-Math.PI / 2, -Math.PI / 2];
        this.jogTouched = null;
        this.browseAngle = 0;
        this.browseScrollAcc = 0;
        this.flashStart = new Map();
        this.lastTouched = new Map();
        this.drag = null;
        this._listeners = [];
        this._hoverListeners = [];
        this._frame = 0;
        this._flashTimer = null;
        this._static = null;
        this.cssW = 0; this.cssH = 0; this.dpr = 1;
        this.now = () => performance.now() / 1000;

        canvas.style.touchAction = "none";
        canvas.addEventListener("pointerdown", (e) => {
            if (e.button !== 0) return;
            e.preventDefault();
            canvas.setPointerCapture?.(e.pointerId);
            this.pointerDown(e.offsetX, e.offsetY, e.shiftKey);
        });
        canvas.addEventListener("pointermove", (e) => {
            if (this.drag) this.pointerDragged(e.offsetX, e.offsetY);
            else this.pointerMoved(e.offsetX, e.offsetY);
        });
        canvas.addEventListener("pointerup", () => this.pointerUp());
        canvas.addEventListener("pointercancel", () => this.pointerUp());
        canvas.addEventListener("pointerleave", () => { if (!this.drag) this._setHovered(null); });
        canvas.addEventListener("wheel", (e) => {
            // the browser's deltaY grows downwards; AppKit's grows upwards. Pixel deltas are a trackpad's (precise).
            const precise = e.deltaMode === 0;
            const dy = precise ? -e.deltaY / 10 : -e.deltaY;
            if (this.scroll(e.offsetX, e.offsetY, dy, precise, -e.deltaY)) e.preventDefault();
        }, { passive: false });
        window.addEventListener("blur", () => this.pointerUp());
        document.fonts?.addEventListener?.("loadingdone", () => { this._static = null; this.requestRedraw(); });
    }

    // MARK: - API

    /** Clicks, drags and scrolls on the drawing, as vocab events. Returns a function that removes the listener. */
    onEvent(cb) { this._listeners.push(cb); return () => { this._listeners = this._listeners.filter((f) => f !== cb); }; }
    /** The control under the pointer (null when none), on every change. */
    onHover(cb) { this._hoverListeners.push(cb); return () => { this._hoverListeners = this._hoverListeners.filter((f) => f !== cb); }; }
    _emit(e) { for (const f of this._listeners) f(e); }

    get faces() { return this._faces; }
    set faces(v) {
        const nf = toMap(v), t = this.now();
        for (const [c, f] of nf) {
            const o = this._faces.get(c);
            if (f && f.value != null && f.value !== (o ? o.value : null)) {
                // the value changed because this control was just moved: show it big for a moment
                const touched = this.lastTouched.get(c);
                if (touched !== undefined && t - touched < FLASH_ATTRIBUTION) this._flash(c, t);
            }
        }
        this._faces = nf;
        this.requestRedraw();
    }
    get lights() { return this._lights; }
    set lights(v) { this._lights = toMap(v); this.requestRedraw(); }
    get captions() { return this._captions; }
    set captions(v) { this._captions = toMap(v); this.requestRedraw(); }
    get knobAccents() { return this._accents; }
    set knobAccents(v) { this._accents = toMap(v); this.requestRedraw(); }
    get header() { return this._header; }
    set header(v) { this._header = v ?? null; this.requestRedraw(); }
    get deckTracks() { return this._deckTracks; }
    set deckTracks(v) { this._deckTracks = [v?.[0] ?? [], v?.[1] ?? []]; this.requestRedraw(); }
    get jogDials() { return this._jogDials; }
    set jogDials(v) { this._jogDials = [v?.[0] ?? null, v?.[1] ?? null]; this.requestRedraw(); }
    get takeovers() { return this._takeovers; }
    set takeovers(v) { this._takeovers = toMap(v); this.requestRedraw(); }
    get padModes() { return this._padModes; }
    set padModes(v) { this._padModes = [v?.[0] ?? "hotCue", v?.[1] ?? "hotCue"]; this.requestRedraw(); }
    get padMode() { return this._padModes[0]; }
    set padMode(m) { this.padModes = [m, m]; }
    get meters() { return this._meters; }
    set meters(v) {
        const o = this._meters;
        this._meters = [v?.[0] ?? 0, v?.[1] ?? 0];
        if (segments(o[0]) !== segments(this._meters[0]) || segments(o[1]) !== segments(this._meters[1])) this.requestRedraw();
    }
    get connected() { return this._connected; }
    set connected(v) { if (this._connected !== !!v) { this._connected = !!v; this.requestRedraw(); } }
    get selectedTrack() { return this._selectedTrack; }
    set selectedTrack(v) { this._selectedTrack = v; this.requestRedraw(); }
    get heldButton() { return this._heldButton; }
    set heldButton(v) { this._heldButton = v ?? null; this.requestRedraw(); }
    get heldColor() { return this._heldColor; }
    set heldColor(v) { this._heldColor = v; this.requestRedraw(); }
    get highlighted() { return this._highlighted; }
    set highlighted(v) { this._highlighted = new Set(v ?? []); this.requestRedraw(); }
    get values() { return this._values; }
    /** put several controls where the app's values are (no events are sent) */
    set values(v) { for (const [c, x] of toMap(v)) this.setPosition(c, x); }

    /** Reflect the hardware: presses highlight until released, absolute values move knobs and faders, relative ticks
     *  spin the jog's marker (and BROWSE's). */
    show(e) {
        const c = e.control, v = e.value;
        if ("press" in v) {
            if (v.press) this.hwPressed.add(c); else this.hwPressed.delete(c);
        } else if ("absolute" in v) {
            this._values.set(c, clamp(v.absolute));
            this.explicitPosition.add(c);
            this.lastTouched.set(c, this.now());
        } else if ("relative" in v) {
            const p = parseControl(c);
            if (p.kind === "jog") this._spinJog(p.deck, v.relative * HW_JOG_RAD_PER_TICK);
            else if (p.kind === "browse") this.browseAngle += v.relative * BROWSE_RAD_PER_TICK;
            this.lastTouched.set(c, this.now());
        }
        this.requestRedraw();
    }

    /** Put a fader, the tempo slider, the crossfader or a knob where the app's value is (no event is sent). */
    setPosition(control, value) {
        const v = clamp(value);
        this.explicitPosition.add(control);
        if (this._values.get(control) === v) return;
        this._values.set(control, v);
        this.requestRedraw();
    }

    /** Forget the hardware presses (the controller went away). */
    clearPressed() { if (this.hwPressed.size) { this.hwPressed.clear(); this.requestRedraw(); } }

    /** where the columns start, as fractions of the width (the strip above uses the same columns) */
    get columnFractions() { return { mixerStart: this.G.mixerX / this.G.designW, rightStart: this.G.rightDeckX / this.G.designW }; }
    get designW() { return this.G.designW; }

    /** Stretch the drawing to this design width (clamped to 1240...1720): the layout follows. */
    setDesignWidth(w) {
        const nw = Math.min(MAX_DESIGN_W, Math.max(MIN_DESIGN_W, Math.round(w)));
        if (nw === this.G.designW) return;
        this.G = geometry(nw);
        this.items = makeLayout(this.G);
        this._index();
        this._static = null;
        this.requestRedraw();
    }

    /** the canvas's size in CSS pixels */
    resize(w, h) {
        this.cssW = w; this.cssH = h;
        this.dpr = sizeCanvas(this.canvas, w, h);
        this._static = null;
        this._draw();
    }

    /** one redraw at the next animation frame */
    requestRedraw() {
        if (this._frame) return;
        this._frame = requestAnimationFrame(() => { this._frame = 0; this._draw(); });
    }

    // MARK: - Layout lookups

    _index() {
        this.indexByControl = new Map();
        this.items.forEach((it, i) => this.indexByControl.set(it.control, i));
    }

    /** the item that draws a control (aliases: BROWSE's press, a jog's touch, a mode button's shifted mode) */
    itemIndexFor(c) {
        const p = parseControl(c);
        if (p.kind === "browsePress") return this.indexByControl.get(C.browse);
        if (p.kind === "jogTouch") return this.indexByControl.get(C.jog(p.deck));
        if (p.kind === "padModeButton") return this.indexByControl.get(C.padModeButton(p.deck, baseMode(p.mode)));
        return this.indexByControl.get(c);
    }

    // MARK: - Value flashes

    _flash(c, t = this.now()) {
        this.flashStart.set(c, t);
        this.requestRedraw();
        this._scheduleFlash();
    }
    isFlashing(c) {
        const s = this.flashStart.get(c);
        return s !== undefined && this.now() - s < FLASH_DURATION;
    }
    _scheduleFlash() {
        if (this._flashTimer || this.flashStart.size === 0) return;
        const first = Math.min(...this.flashStart.values());
        const delay = Math.max(0.01, first + FLASH_DURATION - this.now());
        this._flashTimer = setTimeout(() => {
            this._flashTimer = null;
            const t = this.now();
            for (const [c, s] of [...this.flashStart]) if (t - s >= FLASH_DURATION - 0.005) this.flashStart.delete(c);
            this.requestRedraw();
            this._scheduleFlash();
        }, delay * 1000);
    }

    // MARK: - State helpers

    value(c) {
        if (this._values.has(c)) return this._values.get(c);
        return parseControl(c).kind === "channelFader" ? 0.8 : 0.5;
    }
    /** where a knob's pointer or a fader's cap is drawn: its given position, else its face's arc, else its default */
    position(c) {
        const a = this._faces.get(c)?.arc;
        if (!this.explicitPosition.has(c) && a != null) return clamp(a);
        return this.value(c);
    }
    mode(d) { return this._padModes[d] ?? "hotCue"; }
    get anyShift() { return this.shiftLatched.size > 0; }
    _spinJog(d, a) { this.jogAngle[d] = (this.jogAngle[d] + a) % (2 * Math.PI); }
    _moved(c, v) {
        this._values.set(c, v);
        this.explicitPosition.add(c);
        this.lastTouched.set(c, this.now());
        this._flash(c);
    }

    // MARK: - Geometry

    get fit() {
        const w = this.cssW || this.canvas.clientWidth, h = this.cssH || this.canvas.clientHeight;
        const s = Math.max(0.01, Math.min(w / this.G.designW, h / DESIGN_H));
        return { scale: s, ox: (w - this.G.designW * s) / 2, oy: (h - DESIGN_H * s) / 2 };
    }
    toDesign(x, y) { const f = this.fit; return [(x - f.ox) / f.scale, (y - f.oy) / f.scale]; }
    toView(x, y) { const f = this.fit; return [f.ox + x * f.scale, f.oy + y * f.scale]; }

    hitRect(it) {
        switch (it.kind) {
        case "knob": case "round": case "browse": case "jog": return it.rect.inset(-4, -4);
        case "vfader": return it.rect.inset(-6, -8);
        case "hfader": return it.rect.inset(-8, -4);
        case "segment": return it.rect.inset(-1, -3);
        default: return it.rect.inset(-3, -3);
        }
    }
    itemAt(vx, vy) {
        const [x, y] = this.toDesign(vx, vy);
        for (const it of this.items) {
            if (isRound(it)) {
                if (Math.hypot(x - it.rect.midX, y - it.rect.midY) <= it.rect.w / 2 + 4) return it;
            } else if (this.hitRect(it).contains(x, y)) return it;
        }
        return null;
    }
    /** the control under a point in CSS pixels of the canvas */
    controlAt(x, y) { return this.itemAt(x, y)?.control ?? null; }
    /** the centre of a control in CSS pixels of the canvas */
    centerOf(c) {
        const i = this.itemIndexFor(c);
        if (i === undefined) return null;
        const r = this.items[i].rect;
        return this.toView(r.midX, r.midY);
    }

    // MARK: - Mouse

    pointerMoved(x, y) { this._setHovered(this.controlAt(x, y)); }

    _setHovered(c) {
        if (c === this.hovered) return;
        this.hovered = c;
        this.canvas.title = c && this.helpText ? (this.helpText(c) ?? "") : "";
        this.requestRedraw();
        for (const f of this._hoverListeners) f(c);
    }

    pointerDown(px, py, shiftKey = false) {
        if (this.drag) this.pointerUp();
        const it = this.itemAt(px, py);
        if (!it) return;
        const shifted = shiftKey || this.anyShift;
        const c = it.control;
        const p = parseControl(c);
        switch (it.kind) {
        case "shiftButton":
            if (this.shiftLatched.has(p.deck)) { this.shiftLatched.delete(p.deck); this._emit(ev(c, pressV(false))); }
            else { this.shiftLatched.add(p.deck); this._emit(ev(c, pressV(true))); }
            break;
        case "button": case "round": case "segment":
            this.mouseHeld = c;
            this.drag = { type: "press", c, shifted, padMode: null };
            this._emit(ev(c, pressV(true), { shifted }));
            break;
        case "pad": {
            const m = this.mode(p.deck);
            this.mouseHeld = c;
            this.drag = { type: "press", c, shifted, padMode: m };
            this._emit(ev(c, pressV(true), { padMode: m, shifted }));
            break;
        }
        case "mode": {
            const m = shifted ? it.alt : it.base;
            const ctl = C.padModeButton(p.deck, m);
            this.mouseHeld = ctl;
            this.drag = { type: "press", c: ctl, shifted, padMode: m };
            this._padModes[p.deck] = m;
            this._emit(ev(ctl, pressV(true), { padMode: m, shifted }));
            break;
        }
        case "knob": case "vfader":
            this.drag = { type: "value", c, start: this.position(c), fx: px, fy: py, horizontal: false, shifted };
            this.mouseHeld = c;
            break;
        case "hfader":
            this.drag = { type: "value", c, start: this.position(c), fx: px, fy: py, horizontal: true, shifted };
            this.mouseHeld = c;
            break;
        case "jog": {
            const [dx, dy] = this.toDesign(px, py);
            this.drag = { type: "jog", deck: p.deck, last: Math.atan2(dy - it.rect.midY, dx - it.rect.midX), acc: 0, shifted };
            this.jogTouched = p.deck;
            break;
        }
        case "browse":
            this.drag = { type: "browse", fx: px, fy: py, lastY: py, acc: 0, moved: false, shifted };
            this.mouseHeld = C.browse;
            break;
        }
        this.requestRedraw();
    }

    pointerDragged(px, py) {
        const dr = this.drag;
        if (!dr) return;
        if (dr.type === "value") {
            let d = dr.horizontal ? px - dr.fx : dr.fy - py;
            if (parseControl(dr.c).kind === "tempo") d = -d;          // (its value grows downwards, as on the hardware)
            const v = clamp(dr.start + d / DRAG_RANGE);
            if (v !== this.position(dr.c) || !this.explicitPosition.has(dr.c)) {
                this._moved(dr.c, v);
                this._emit(ev(dr.c, absolute(v), { shifted: dr.shifted }));
                this.requestRedraw();
            }
        } else if (dr.type === "jog") {
            const i = this.itemIndexFor(C.jog(dr.deck));
            if (i === undefined) return;
            const r = this.items[i].rect;
            const [qx, qy] = this.toDesign(px, py);
            const a = Math.atan2(qy - r.midY, qx - r.midX);
            let delta = a - dr.last;
            if (delta > Math.PI) delta -= 2 * Math.PI;
            if (delta < -Math.PI) delta += 2 * Math.PI;
            this._spinJog(dr.deck, delta);
            dr.acc += delta;
            const ticks = Math.trunc(dr.acc / HW_JOG_RAD_PER_TICK);
            if (ticks !== 0) {
                dr.acc -= ticks * HW_JOG_RAD_PER_TICK;
                this.lastTouched.set(C.jog(dr.deck), this.now());
                this._emit(ev(C.jog(dr.deck), relative(ticks), { shifted: dr.shifted }));
            }
            dr.last = a;
            this.requestRedraw();
        } else if (dr.type === "browse") {
            if (Math.hypot(px - dr.fx, py - dr.fy) > 3) dr.moved = true;
            dr.acc += dr.lastY - py;
            const step = 12;
            while (Math.abs(dr.acc) >= step) {
                const n = dr.acc > 0 ? 1 : -1;
                dr.acc -= n * step;
                this.browseAngle += n * BROWSE_RAD_PER_TICK;
                this.lastTouched.set(C.browse, this.now());
                this._emit(ev(C.browse, relative(n), { shifted: dr.shifted }));
                this.requestRedraw();
            }
            dr.lastY = py;
        }
    }

    /** Release whatever the mouse holds (also when the window loses focus). */
    pointerUp() {
        const dr = this.drag;
        if (!dr) return;
        this.drag = null;
        this.mouseHeld = null;
        if (dr.type === "press") {
            this._emit(ev(dr.c, pressV(false), { padMode: dr.padMode, shifted: dr.shifted }));
        } else if (dr.type === "browse") {
            if (!dr.moved) {
                this._emit(ev(C.browsePress, pressV(true), { shifted: dr.shifted }));
                this._emit(ev(C.browsePress, pressV(false), { shifted: dr.shifted }));
            }
        } else if (dr.type === "jog") {
            this.jogTouched = null;
        }
        this.requestRedraw();
    }

    /** Scroll over a jog, BROWSE, a knob or a fader (deltaY up positive, as AppKit's). False when nothing takes it. */
    scroll(px, py, deltaY, precise = false, preciseDeltaY = 0) {
        if (deltaY === 0 && preciseDeltaY === 0) return false;
        const it = this.itemAt(px, py);
        if (!it) return false;
        const shifted = this.anyShift;
        switch (it.kind) {
        case "jog": {
            if (deltaY === 0) return true;
            let units = Math.trunc(deltaY * 2);
            if (units === 0) units = deltaY > 0 ? 1 : -1;
            const ticks = units * JOG_TICKS_PER_SCROLL;
            this._spinJog(parseControl(it.control).deck, ticks * HW_JOG_RAD_PER_TICK);
            this.lastTouched.set(it.control, this.now());
            this._emit(ev(it.control, relative(ticks), { shifted }));
            break;
        }
        case "browse": {
            const steps = [];
            if (precise) {
                this.browseScrollAcc += preciseDeltaY;
                while (Math.abs(this.browseScrollAcc) >= 12) {
                    const n = this.browseScrollAcc > 0 ? 1 : -1;
                    this.browseScrollAcc -= n * 12;
                    steps.push(n);
                }
            } else if (deltaY !== 0) steps.push(deltaY > 0 ? 1 : -1);
            for (const n of steps) {
                this.browseAngle += n * BROWSE_RAD_PER_TICK;
                this.lastTouched.set(C.browse, this.now());
                this._emit(ev(C.browse, relative(n), { shifted }));
            }
            break;
        }
        case "knob": case "vfader": case "hfader": {
            const d = precise ? preciseDeltaY / DRAG_RANGE : deltaY * 0.02;
            const dir = parseControl(it.control).kind === "tempo" ? -1 : 1;   // (scroll up: the cap goes up)
            const v = clamp(this.position(it.control) + dir * d);
            if (v !== this.position(it.control)) {
                this._moved(it.control, v);
                this._emit(ev(it.control, absolute(v), { shifted }));
            }
            break;
        }
        default:
            return false;
        }
        this.requestRedraw();
        return true;
    }

    /** One step of a knob, fader or endless control, as from the keyboard (+1 up / clockwise). */
    step(c, dir) {
        const i = this.itemIndexFor(c);
        if (i === undefined) return false;
        const it = this.items[i];
        switch (it.kind) {
        case "knob": case "vfader": case "hfader": {
            const v = clamp(this.position(it.control) + 0.05 * dir);
            if (v === this.position(it.control)) return true;
            this._moved(it.control, v);
            this._emit(ev(it.control, absolute(v), { shifted: this.anyShift }));
            break;
        }
        case "jog": {
            this._spinJog(parseControl(it.control).deck, dir * JOG_TICKS_PER_STEP * HW_JOG_RAD_PER_TICK);
            this.lastTouched.set(it.control, this.now());
            this._emit(ev(it.control, relative(dir * JOG_TICKS_PER_STEP), { shifted: this.anyShift }));
            break;
        }
        case "browse":
            this.browseAngle += dir * BROWSE_RAD_PER_TICK;
            this.lastTouched.set(C.browse, this.now());
            this._emit(ev(C.browse, relative(dir), { shifted: this.anyShift }));
            break;
        default:
            return false;
        }
        this.requestRedraw();
        return true;
    }

    /** Press and release a button or pad (SHIFT toggles its latch). */
    press(c) {
        const i = this.itemIndexFor(c);
        if (i === undefined) return false;
        const it = this.items[i];
        if (["button", "shiftButton", "round", "pad", "mode", "browse", "segment"].includes(it.kind)) {
            const [x, y] = this.toView(it.rect.midX, it.rect.midY);
            this.pointerDown(x, y);
            this.pointerUp();
            return true;
        }
        return false;
    }

    // MARK: - Looks and text metrics

    /** what a control shows now: its face, or its caption / light / accent */
    look(c) {
        const f = this._faces.get(c);
        if (f) {
            const title = f.title ? String(f.title).toUpperCase() : "";
            return { title, value: f.value != null ? String(f.value).toUpperCase() : null, color: f.color ?? null,
                     light: f.light ?? OFF, badge: f.badge ?? null, outlined: !!f.outlined, arc: f.arc ?? null,
                     hasFace: true, empty: !title };
        }
        return { title: this.caption(c), value: null, color: this._accents.get(c) ?? null, light: this._lights.get(c) ?? OFF,
                 badge: null, outlined: false, arc: null, hasFace: false, empty: false };
    }
    caption(c) { const s = this._captions.get(c); return s ? s.toUpperCase() : null; }

    /** title size in design units: 16, or more when the drawing is small, so titles stay >= 12 px on screen */
    get capSize() { return Math.max(16, 12 / this.fit.scale); }
    get valSize() { return Math.max(12, 9 / this.fit.scale); }
    get lblSize() { return Math.max(10, 8 / this.fit.scale); }
    get labelColor() { return col(S.fg2, 0.8); }
    get trackColor() { return TRACK4[Math.min(TRACK4.length - 1, Math.max(0, this._selectedTrack))]; }

    _line(s, size, color, x, y, align, width) { drawText(this.g, truncate(s, maxChars(width, size)), size, color, x, y, align); }
    _label(s, x, y, align = "center", color = null, maxWidth = Infinity) {
        drawText(this.g, s, this.lblSize, color ?? this.labelColor, x, y, align, maxWidth, true);
    }

    sideGap(it) { return it.kind === "knob" ? 10 : 8; }
    /** where an item's title goes, in design space (null: on the control) */
    captionRect(it) {
        const lh = lineHeight(this.capSize), cp = it.caption;
        if (cp.at) return new Rect(cp.at[0] - cp.width / 2, cp.at[1] - lh / 2, cp.width, lh);
        if (cp.above !== undefined) return new Rect(it.rect.midX - cp.above / 2, it.rect.minY - 5 - lh, cp.above, lh);
        if (cp.side !== undefined) {
            const h = lh + lineHeight(this.capSize) + lineHeight(this.lblSize) + 4;
            const edge = cp.side ? it.rect.minX - this.sideGap(it) : it.rect.maxX + this.sideGap(it);
            return new Rect(cp.side ? edge - cp.width : edge, it.rect.midY - h / 2, cp.width, h);
        }
        return null;
    }

    isHeld(it) {
        const h = this._heldButton;
        if (!h) return false;
        if (h === it.control) return true;
        if (parseControl(h).kind === "padModeButton") {
            const i = this.itemIndexFor(h);
            return i !== undefined && this.items[i].control === it.control;
        }
        return false;
    }
    titleColor(it, lk) {
        if (this.isHeld(it)) return this._heldColor;
        if (lk.color != null) return lk.color;
        if (!lk.hasFace && lk.title && (lk.title.startsWith("REC") || lk.title.startsWith("FX "))) return S.red;
        return S.fg;
    }
    isPressed(c) { return this.hwPressed.has(c) || this.mouseHeld === c; }
    /** a value's size and colour: bigger and brighter for a moment while its control moves */
    valueStyle(c, lk) { return this.isFlashing(c) ? [this.capSize, lk.color ?? S.fg] : [this.valSize, S.fg2]; }

    // MARK: - Draw

    _draw() {
        const g = this.g, cv = this.canvas;
        if (!this.cssW) { this.cssW = cv.clientWidth; this.cssH = cv.clientHeight; this.dpr = sizeCanvas(cv, this.cssW, this.cssH); }
        g.setTransform(1, 0, 0, 1, 0, 0);
        g.clearRect(0, 0, cv.width, cv.height);
        g.drawImage(this._staticImage(), 0, 0);
        const f = this.fit;
        g.setTransform(this.dpr * f.scale, 0, 0, this.dpr * f.scale, this.dpr * f.ox, this.dpr * f.oy);
        this._drawHeader();
        this._drawMeters();
        for (const it of this.items) {
            const lk = this.look(it.control);
            this._drawItem(it, lk);
            this._drawTexts(it, lk);
        }
        if (this.flashStart.size) this._scheduleFlash();
    }

    /** the static parts (panels, printed labels, scales), drawn again only when the size changes */
    _staticImage() {
        const cv = this.canvas;
        if (this._static && this._static.width === cv.width && this._static.height === cv.height) return this._static;
        const off = document.createElement("canvas");
        off.width = cv.width; off.height = cv.height;
        const saved = this.g;
        this.g = off.getContext("2d");
        const f = this.fit;
        this.g.setTransform(this.dpr * f.scale, 0, 0, this.dpr * f.scale, this.dpr * f.ox, this.dpr * f.oy);
        this._drawStatic();
        this.g = saved;
        this._static = off;
        return off;
    }

    _stroke(color, width) { const g = this.g; g.strokeStyle = typeof color === "string" ? color : col(color); g.lineWidth = width; }

    _printedKnob(cx, cy, r) {
        const g = this.g, rect = Rect.circle(cx, cy, r);
        ellipsePath(g, rect);
        g.fillStyle = col(S.s2); g.fill();
        this._stroke(S.line, 1); g.stroke();
        this._stroke(S.dim, 2);
        g.beginPath(); g.moveTo(cx, cy - r * 0.2); g.lineTo(cx, cy - r * 0.85); g.stroke();
    }

    _drawStatic() {
        const g = this.g, G = this.G, W = G.designW, H = DESIGN_H;
        const px = 1 / this.fit.scale;
        fillRect(g, new Rect(0, 0, W, H), S.s1);
        this._stroke(S.line, px);
        g.beginPath();
        for (const x of [px / 2, G.mixerX, G.rightDeckX, W - px / 2]) { g.moveTo(x, 0); g.lineTo(x, H); }
        g.moveTo(0, H - px / 2); g.lineTo(W, H - px / 2);
        g.stroke();

        for (const deck of [0, 1]) {
            const x0 = deck === 0 ? 0 : G.rightDeckX;
            drawText(g, deck === 0 ? "1" : "2", 16, S.fg2, x0 + 10, 13, "left");
            this._label("CUE/LOOP CALL", x0 + G.deckShift + 243, 100);
        }

        // beat FX group
        const fx = G.beatFXBox;
        this._stroke(S.line, px);
        g.strokeRect(fx.x + px / 2, fx.y + px / 2, fx.w - px, fx.h - px);
        const legend = "BEAT FX";
        const lw = legend.length * charWidth(this.lblSize) + 8;
        fillRect(g, new Rect(fx.minX + 6, fx.minY - 6, lw, 12), S.s1);
        this._label(legend, fx.minX + 6, fx.minY, "left", S.fg2);
        this._label("BEAT", G.rightColMid, 282);

        // the mixer's column lines, from under the LOAD row to the channel faders' foot
        this._stroke(S.line, px);
        g.beginPath();
        for (const x of [G.leftColX, G.rightColX]) { g.moveTo(x, 116); g.lineTo(x, 640); }
        g.stroke();

        const lx = G.leftColMid;
        this._label("PERFORMANCE", lx, 134, "center", S.fg2);
        drawText(g, "DDJ-400", 14, S.fg2, lx, 154);
        this._label("HEADPHONES", lx, 312, "center", S.fg2);
        this._label("LEVEL", lx, 432);
        this._printedKnob(lx, 456, 12);

        const rx = G.rightColMid;
        this._label("MASTER", rx, 132, "center", S.fg2);
        this._label("LEVEL", rx, 148);
        this._printedKnob(rx, 170, 12);
        this._stroke(col(TRACK4[2], 0.6), 1.5);
        const cue = new Rect(rx - 18, 190, 36, 14);
        g.strokeRect(cue.x, cue.y, cue.w, cue.h);
        this._label("CUE", rx, cue.midY, "center", S.fg2);

        // fader scale between the channel faders
        const i = this.itemIndexFor(C.channelFader(0));
        if (i !== undefined) {
            const r = this.items[i].rect;
            for (let v = 0; v <= 10; v += 2) this._label(String(v), G.mixerMid, r.minY + r.h * (10 - v) / 10);
        }

        for (const it of this.items) {
            if (it.kind === "knob") {
                const rad = it.rect.w / 2;
                this._stroke(S.s3, 2);
                g.beginPath(); g.arc(it.rect.midX, it.rect.midY, rad + 4, knobAngle(0), knobAngle(1), false); g.stroke();
            } else if (it.kind === "vfader") this._drawVFaderTrack(it);
            else if (it.kind === "hfader") this._drawHFaderTrack(it);
        }
    }

    _drawVFaderTrack(it) {
        const g = this.g, r = it.rect;
        const isTempo = parseControl(it.control).kind === "tempo";
        this._stroke(S.s3, 1);
        const n = isTempo ? 20 : 10;
        g.beginPath();
        for (let i = 0; i <= n; i++) {
            const y = r.minY + r.h * i / n;
            const long = i === 0 || i === n || (isTempo && i === n / 2) || (!isTempo && i % 2 === 0);
            const w = long ? 6 : 3;
            g.moveTo(r.minX, y); g.lineTo(r.minX + w, y);
            g.moveTo(r.maxX - w, y); g.lineTo(r.maxX, y);
        }
        g.stroke();
        if (isTempo) fillRect(g, new Rect(r.maxX + 2, r.midY - 2, 4, 4), TRACK4[1]);
        box(g, new Rect(r.midX - 2, r.minY - 4, 4, r.h + 8), S.bg, S.line);
    }

    _drawHFaderTrack(it) {
        const g = this.g, r = it.rect;
        this._stroke(S.s3, 1);
        g.beginPath();
        for (let i = 0; i <= 8; i++) {
            const x = r.minX + r.w * i / 8;
            const h = i === 4 || i === 0 || i === 8 ? 5 : 3;
            g.moveTo(x, r.minY); g.lineTo(x, r.minY + h);
            g.moveTo(x, r.maxY - h); g.lineTo(x, r.maxY);
        }
        g.stroke();
        box(g, new Rect(r.minX - 4, r.midY - 2, r.w + 8, 4), S.bg, S.line);
    }

    // MARK: - Header (global state) or status line

    _drawHeader() {
        const g = this.g, r = this.G.statusRect, h = this._header;
        const dot = this._connected ? TRACK4[1] : TRACK4[3];
        if (!h) {
            const s = this._connected ? "DDJ CONNECTED" : "NO DDJ (400 / FLX4): CLICK TO PLAY";
            const w = textWidth(s, this.lblSize);
            const x0 = r.midX - (w + 10) / 2;
            fillRect(g, new Rect(x0, 8, 6, 6), dot);
            drawText(g, s, this.lblSize, S.fg2, x0 + 10, 11, "left");
            return;
        }
        fillRect(g, new Rect(r.minX + 6, 6, 5, 5), dot);
        const fg = S.fg, fg2 = S.fg2;
        const pad2 = (n) => String(n).padStart(2, "0");
        const line1 = [];
        let pat = `P${pad2(h.pattern + 1)}`;
        if (h.queued != null && h.queued !== h.pattern) pat += ` → P${pad2(h.queued + 1)}`;
        line1.push([pat, fg, 0]);
        const bpm = Math.abs(h.bpm - Math.round(h.bpm)) < 0.05 ? h.bpm.toFixed(0) : h.bpm.toFixed(1);
        line1.push([bpm + " BPM", fg, 0]);
        line1.push([`SW ${h.swing}%`, fg2, 3]);
        if (h.recording) line1.push(["● REC", S.red, 1]);
        else line1.push([h.playing ? "▶" : "■", h.playing ? TRACK4[1] : fg2, 1]);
        if (h.fill) line1.push(["FILL", COLORS.track[2], 2]);
        const line2 = [];
        const tc = COLORS.track[Math.min(COLORS.track.length - 1, Math.max(0, h.track))];
        line2.push([`T${h.track + 1} ${String(h.machine ?? "").toUpperCase()}`, tc, 0]);
        line2.push([TRIG_PAGES[h.page] ?? "", COLORS.page[Math.min(COLORS.page.length - 1, h.page)], 0]);
        if (h.pages > 1) {
            let s = "";
            for (let i = 0; i < h.pages; i++) s += i === h.stepPage ? "●" : "○";
            line2.push([s, fg2, 3]);
        }
        if (h.perf) line2.push([h.perf.toUpperCase(), fg, 2]);
        if (h.sceneEdit != null) {
            line2.push([h.sceneEdit === 0 ? "SCENE A EDIT" : "SCENE B EDIT", h.sceneEdit === 0 ? COLORS.sceneA : COLORS.sceneB, 1]);
        }
        const maxW = r.w - 24;
        this._drawRuns(line1, this.capSize, r.midX, 14, maxW);
        this._drawRuns(line2, this.valSize, r.midX, 34, maxW);
    }

    /** a line of coloured runs, centred; the least important runs are dropped until it fits */
    _drawRuns(runs, size, centreX, y, maxWidth) {
        runs = runs.slice();
        const gap = charWidth(size) * 1.5;
        const width = () => runs.reduce((a, r) => a + textWidth(r[0], size), 0) + gap * Math.max(0, runs.length - 1);
        while (width() > maxWidth && runs.length > 1) {
            let i = 0;
            runs.forEach((r, k) => { if (r[2] >= runs[i][2]) i = k; });
            if (runs[i][2] <= 0) break;
            runs.splice(i, 1);
        }
        let x = centreX - width() / 2;
        for (const [t, c] of runs) {
            drawText(this.g, t, size, c, x, y, "left");
            x += textWidth(t, size) + gap;
        }
    }

    // MARK: - Texts around a control: title, value, printed label, badge

    _drawTexts(it, lk) {
        const g = this.g, c = it.control, value = lk.value;
        const [vSize, vColor] = this.valueStyle(c, lk);
        const cp = it.caption;
        if (cp.at || cp.above !== undefined) {
            const r = this.captionRect(it);
            const t = lk.title;
            if (t) {
                if (it.valuePos === "inline" && value != null) {
                    // title and value on one line
                    const tw = textWidth(t, this.capSize), gap = charWidth(this.capSize);
                    const room = r.w - tw - gap;
                    const v = truncate(value, maxChars(Math.max(0, room), vSize));
                    const total = tw + gap + textWidth(v, vSize);
                    drawText(g, t, this.capSize, this.titleColor(it, lk), r.midX - total / 2, r.midY, "left");
                    drawText(g, v, vSize, vColor, r.midX - total / 2 + tw + gap, r.midY, "left");
                } else {
                    this._titleWithBadge(t, lk, it, r, "center");
                }
            }
            if (it.valuePos === "underTitle" && value != null) {
                this._line(value, vSize, vColor, r.midX, r.maxY + lineHeight(vSize) / 2, "center", r.w);
            }
        } else if (cp.side !== undefined) {
            const r = this.captionRect(it);
            const align = cp.side ? "right" : "left";
            const x = cp.side ? r.maxX : r.minX;
            const lh = lineHeight(this.capSize), vlh = lineHeight(vSize), llh = lineHeight(this.lblSize);
            let titleLines = [];
            if (lk.title) titleLines = wrap(lk.title, maxChars(r.w - this.badgeWidth(lk), this.capSize), value == null ? 2 : 1);
            const h = titleLines.length * lh + (value != null ? vlh : 0) + llh;
            let y = it.rect.midY - h / 2;
            titleLines.forEach((line, i) => {
                const lr = new Rect(r.minX, y, r.w, lh);
                if (i === 0) this._titleWithBadge(line, lk, it, lr, align);
                else drawText(g, line, this.capSize, this.titleColor(it, lk), x, y + lh / 2, align);
                y += lh;
            });
            if (value != null) { this._line(value, vSize, vColor, x, y + vlh / 2, align, r.w); y += vlh; }
            this._label(it.label, x, y + llh / 2, align, null, r.w);
        }
        // under the control: its value (knobs, BROWSE) or its printed label
        const below = it.rect.maxY + (isRound(it) ? 6 : 3) + (it.kind === "knob" ? 4 : 0) + (it.kind === "vfader" ? 9 : 0);
        if (it.valuePos === "below" && value != null) {
            this._line(value, vSize, vColor, it.rect.midX, below + lineHeight(vSize) / 2, "center", 110);
        } else if (it.labelPos === "below") {
            this._label(it.label, it.rect.midX, below + lineHeight(this.lblSize) / 2, "center", null, 70);
        }
        if (it.labelPos === "left") this._label(it.label, it.rect.minX - 10, it.rect.midY, "right");
        // BROWSE: what pushing it does
        if (it.kind === "browse") {
            const push = this._faces.get(C.browsePress)?.title ?? this.caption(C.browsePress);
            if (push) {
                const y = below + Math.max(lineHeight(vSize), lineHeight(this.lblSize)) + 2 + lineHeight(this.valSize) / 2;
                this._line("PUSH: " + push.toUpperCase(), this.valSize, S.fg2, it.rect.midX, y, "center", 130);
            }
        }
    }

    badgeWidth(lk) { return lk.badge ? textWidth(lk.badge, this.lblSize) + 8 + 4 : 0; }

    /** a title on one line; a badge ("LOCK", "A", "B") as a small tag beside it, on the side away from the control */
    _titleWithBadge(t, lk, it, r, align) {
        const g = this.g, bw = this.badgeWidth(lk);
        const line = truncate(t, maxChars(r.w - bw, this.capSize));
        const color = this.titleColor(it, lk);
        const tw = textWidth(line, this.capSize);
        let tx = align === "left" ? r.minX : align === "right" ? r.maxX - tw : r.midX - (tw + bw) / 2;
        if (bw > 0) {
            const tagW = bw - 4, tagH = lineHeight(this.lblSize) + 2;
            const tagX = align === "right" ? tx - bw : tx + tw + 4;
            const tag = new Rect(tagX, r.midY - tagH / 2, tagW, tagH);
            const fill = lk.color ?? S.fg;
            box(g, tag, fill, null);
            drawText(g, lk.badge, this.lblSize, ink(fill), tag.midX, tag.midY);
        }
        drawText(g, line, this.capSize, color, tx, r.midY, "left");
    }

    // MARK: - Controls

    _drawItem(it, lk) {
        const held = this.isHeld(it);
        const p = parseControl(it.control);
        switch (it.kind) {
        case "button": case "shiftButton": {
            let lvl = lk.light;
            if (p.kind === "shift" && this.shiftLatched.has(p.deck)) lvl = ON;
            let hue = lk.color ?? S.fg;
            if (lk.color == null && p.kind === "headphoneCue") hue = TRACK4[3];
            this._rectButton(it, lk, lvl, hue, held);
            break;
        }
        case "round": this._roundButton(it, lk, held); break;
        case "mode": this._modeButton(it, held); break;
        case "pad": this._pad(it, lk, held); break;
        case "knob": this._knob(it, lk); break;
        case "browse": this._browse(it); break;
        case "vfader": this._vfaderCap(it, lk); break;
        case "hfader": this._hfaderCap(it, lk); break;
        case "jog": this._jog(it, lk); break;
        case "segment": this._segment(it, lk); break;
        }
        if (held) this._outline(it, this._heldColor, 2, 4);
        if (this._highlighted.has(it.control)) this._outline(it, S.fg, 2, held ? 7 : 4);
        else if (this.hovered === it.control && !held) this._outline(it, col(S.fg2, 0.6), 1, 4);
    }

    _outline(it, color, width, offset) {
        const g = this.g;
        this._stroke(color, width);
        let r = it.rect;
        if (it.kind === "vfader") r = r.inset(0, -8);
        if (it.kind === "knob") r = r.inset(-4, -4);
        r = r.inset(-offset, -offset);
        if (isRound(it)) ellipsePath(g, r);
        else { g.beginPath(); g.roundRect(r.x, r.y, r.w, r.h, 3); }
        g.stroke();
    }

    _pressRing(r, round) {
        const g = this.g;
        this._stroke(S.fg, 1.5);
        const q = r.inset(-2, -2);
        if (round) ellipsePath(g, q);
        else { g.beginPath(); g.roundRect(q.x, q.y, q.w, q.h, 3); }
        g.stroke();
    }

    /** fill for a light level: lit full, dim the colour at a quarter, off the plain control */
    _lightFill(r, level, hue, pressed) {
        const g = this.g;
        if (level === ON) box(g, r, hue, null);
        else if (level === DIM) { box(g, r, S.s2, null); box(g, r, col(hue, 0.25), col(hue, 0.5)); }
        else box(g, r, pressed ? S.s3 : S.s2, S.s3);
    }

    _rectButton(it, lk, level, hue, held) {
        const g = this.g, r = it.rect, pressed = this.isPressed(it.control);
        if (held) {
            box(g, r, this._heldColor, null);
            if (pressed) this._pressRing(r, false);
            drawText(g, "HELD", this.lblSize, ink(this._heldColor), r.midX, r.midY, "center", r.w - 4, true);
            return;
        }
        this._lightFill(r, level, hue, pressed);
        if (pressed) this._pressRing(r, false);
        const lc = level === ON ? ink(hue) : S.fg2;
        if (it.valuePos === "inside" && lk.value != null) {
            const [vs, vc] = this.valueStyle(it.control, lk);
            this._line(lk.value, vs, level === ON ? ink(hue) : (this.isFlashing(it.control) ? vc : S.fg), r.midX, r.midY, "center", r.w - 4);
        } else {
            drawText(g, it.label, this.lblSize, lc, r.midX, r.midY, "center", r.w - 4, true);
        }
    }

    _roundButton(it, lk, held) {
        const g = this.g, r = it.rect, lvl = lk.light, pressed = this.isPressed(it.control);
        const big = r.w >= 40;
        const kind = parseControl(it.control).kind;
        const ringColor = lk.color ?? (kind === "play" ? TRACK4[1] : kind === "cue" ? S.red : kind === "fxOnOff" ? S.fg : this.trackColor);
        disc(g, r, pressed ? S.s3 : S.s2, S.s3);
        if (held) {
            disc(g, r.inset(1, 1), this._heldColor, null);
        } else if (big) {
            const ring = r.inset(3, 3);
            this._stroke(lvl === ON ? col(ringColor) : lvl === DIM ? col(ringColor, 0.35) : col(S.line), 3);
            ellipsePath(g, ring); g.stroke();
        } else if (lvl === ON) disc(g, r.inset(1, 1), ringColor, null);
        else if (lvl === DIM) disc(g, r.inset(1, 1), col(ringColor, 0.25), col(ringColor, 0.5));
        if (pressed) this._pressRing(r, true);
        const onFill = held ? this._heldColor : ringColor;
        const inkC = (held || (!big && lvl === ON)) ? ink(onFill) : (lvl === ON ? S.fg : S.fg2);
        if (big) {
            if (lk.value != null) {
                const [vs] = this.valueStyle(it.control, lk);
                this._line(lk.value, Math.max(vs, 16), held ? inkC : S.fg, r.midX, r.midY - 5, "center", r.w - 10);
                drawText(g, held ? "HELD" : it.label, this.lblSize, inkC, r.midX, r.midY + 12, "center", r.w - 16, true);
            } else if (held) {
                drawText(g, it.label, 16, inkC, r.midX, r.midY - 5, "center", r.w - 10, true);
                drawText(g, "HELD", this.lblSize, inkC, r.midX, r.midY + 11, "center", r.w - 12, true);
            } else {
                drawText(g, it.label, 16, inkC, r.midX, r.midY, "center", r.w - 10, true);
            }
        } else if (held) {
            drawText(g, "HELD", this.lblSize, inkC, r.midX, r.midY, "center", r.w - 4, true);
        } else if (it.valuePos === "inside" && lk.value != null) {
            this._line(lk.value, this.lblSize, inkC, r.midX, r.midY, "center", r.w - 4);
        } else if (it.labelPos === "inside") {
            drawText(g, it.label, this.lblSize, inkC, r.midX, r.midY, "center", r.w - 10, true);
        }
    }

    _modeButton(it, held) {
        const g = this.g, deck = parseControl(it.control).deck, base = it.base, alt = it.alt;
        const m = this.mode(deck);
        const baseLook = this.look(C.padModeButton(deck, base)), altLook = this.look(C.padModeButton(deck, alt));
        const altActive = m === alt || altLook.light === ON;
        const active = m === base || altActive || baseLook.light === ON;
        const lvl = active ? ON : lvlMax(baseLook.light, altLook.light);
        const pressed = this.isPressed(C.padModeButton(deck, base)) || this.isPressed(C.padModeButton(deck, alt));
        const r = it.rect, hue = baseLook.color ?? S.fg;
        if (held) box(g, r, this._heldColor, null); else this._lightFill(r, lvl, hue, pressed);
        if (pressed) this._pressRing(r, false);
        const mainColor = (lvl === ON || held) ? col(ink(held ? this._heldColor : hue), altActive ? 0.45 : 1) : S.fg2;
        drawText(g, PAD_MODE_LABEL[base], this.lblSize, mainColor, r.midX, r.midY, "center", r.w - 4, true);
        // the shifted mode printed under the button, red when it is the one selected
        this._label(PAD_MODE_LABEL[alt], r.midX, r.maxY + 3 + lineHeight(this.lblSize) / 2, "center",
                    altActive ? S.red : this.labelColor, r.w);
    }

    _segment(it, lk) {
        const r = it.rect, pressed = this.isPressed(it.control), hue = lk.color ?? S.fg;
        this._lightFill(r, lk.light, hue, pressed);
        if (pressed) this._pressRing(r, false);
        const title = lk.title || it.label;
        const color = lk.light === ON ? ink(hue) : lk.light === DIM ? S.fg : S.fg2;
        this._line(title, this.lblSize, color, r.midX, r.midY, "center", r.w - 6);
    }

    _pad(it, lk, held) {
        const g = this.g, r = it.rect, pressed = this.isPressed(it.control);
        if (lk.empty && !held) {   // nothing here (past the track's length)
            box(g, r, S.s1, S.line);
            if (pressed) this._pressRing(r, false);
            return;
        }
        const lvl = held ? ON : lk.light;
        const title = lk.title;
        const isFX = !lk.hasFace && !!title && title.startsWith("FX");
        const hue = held ? this._heldColor : (lk.color ?? (isFX ? S.red : this.trackColor));
        this._lightFill(r, lvl, hue, pressed);
        if (pressed) this._pressRing(r, false);
        // the playhead or a held step: a crisp outline inside the pad
        if (lk.outlined) {
            this._stroke(lvl === ON ? ink(hue) : S.fg, 2);
            const q = r.inset(4, 4);
            g.beginPath(); g.roundRect(q.x, q.y, q.w, q.h, 2); g.stroke();
        }
        const onInk = ink(hue);
        if (lk.badge) {
            this._line(lk.badge, this.lblSize, lvl === ON ? onInk : S.fg, r.maxX - 7, r.minY + 6 + lineHeight(this.lblSize) / 2, "right", r.w - 12);
        }
        if (!title) return;
        let color;
        if (lvl === ON) color = isFX ? S.fg2 : onInk;
        else if (lvl === DIM) color = S.fg;
        else color = lk.hasFace ? S.fg2 : (this._accents.get(it.control) ?? (isFX ? S.red : S.fg));
        const cs = this.capSize, lh = lineHeight(cs);
        const lines = wrap(title, maxChars(r.w - 8, cs), lk.value == null ? 2 : 1);
        const vlh = lk.value == null ? 0 : lineHeight(this.valSize);
        let y = r.midY - (lines.length * lh + vlh) / 2 + lh / 2 + 3;
        for (const line of lines) { drawText(g, line, cs, color, r.midX, y); y += lh; }
        if (lk.value != null) {
            const vc = lvl === ON ? col(onInk, 0.8) : S.fg2;
            this._line(lk.value, this.valSize, vc, r.midX, y - lh / 2 + vlh / 2 + 1, "center", r.w - 6);
        }
    }

    _knob(it, lk) {
        const g = this.g, r = it.rect, cx = r.midX, cy = r.midY, rad = r.w / 2;
        const pressed = this.isPressed(it.control);
        const accent = lk.color;
        const take = this._takeovers.get(it.control);
        const ap = knobAngle(this.position(it.control));
        if (lk.arc != null) {
            // the parameter's value as a ring from the minimum, in the knob row's colour
            if (lk.arc > 0.001) {
                this._stroke(accent ?? S.fg2, 3);
                g.beginPath(); g.arc(cx, cy, rad + 4, knobAngle(0), knobAngle(clamp(lk.arc)), false); g.stroke();
            }
        } else {
            const amid = knobAngle(0.5);
            if (Math.abs(ap - amid) > 0.001) {
                this._stroke(accent ?? S.fg2, 2);
                g.beginPath(); g.arc(cx, cy, rad + 4, Math.min(amid, ap), Math.max(amid, ap), false); g.stroke();
            }
        }
        disc(g, r, pressed ? S.s3 : S.s2, pressed ? S.fg2 : (accent ?? S.s3));
        const inactive = take ? !take.active : false;
        this._stroke(inactive ? S.dim : (accent ?? S.fg), 2);
        g.beginPath();
        g.moveTo(cx + Math.cos(ap) * rad * 0.25, cy + Math.sin(ap) * rad * 0.25);
        g.lineTo(cx + Math.cos(ap) * (rad - 2), cy + Math.sin(ap) * (rad - 2));
        g.stroke();
        if (take) {
            const a = knobAngle(clamp(take.target));
            disc(g, new Rect(cx + Math.cos(a) * (rad + 4) - 2.5, cy + Math.sin(a) * (rad + 4) - 2.5, 5, 5), accent ?? this.trackColor, null);
        }
    }

    _browse(it) {
        const g = this.g, r = it.rect, cx = r.midX, cy = r.midY, rad = r.w / 2;
        const pressed = this.isPressed(C.browse) || this.isPressed(C.browsePress);
        disc(g, r, pressed ? S.s3 : S.s2, S.s3);
        this._stroke(S.line, 1);
        g.beginPath();
        for (let i = 0; i < 24; i++) {
            const a = this.browseAngle + i * Math.PI / 12;
            g.moveTo(cx + Math.cos(a) * (rad - 5), cy + Math.sin(a) * (rad - 5));
            g.lineTo(cx + Math.cos(a) * (rad - 1.5), cy + Math.sin(a) * (rad - 1.5));
        }
        g.stroke();
        const a = this.browseAngle - Math.PI / 2;
        fillRect(g, new Rect(cx + Math.cos(a) * rad * 0.55 - 2, cy + Math.sin(a) * rad * 0.55 - 2, 4, 4), S.fg);
        if (pressed) this._pressRing(r, true);
    }

    _takeoverColor(lk) { return lk.color ?? this.trackColor; }

    _vfaderCap(it, lk) {
        const g = this.g, r = it.rect;
        const isTempo = parseControl(it.control).kind === "tempo";
        const take = this._takeovers.get(it.control);
        if (take) {   // where the value really is: a mark either side of the slot
            const y = r.minY + downFraction(it.control, clamp(take.target)) * r.h;
            g.fillStyle = col(this._takeoverColor(lk));
            g.beginPath();
            for (const [x, dir] of [[r.minX - 1, 1], [r.maxX + 1, -1]]) {
                g.moveTo(x, y - 5); g.lineTo(x + 7 * dir, y); g.lineTo(x, y + 5); g.closePath();
            }
            g.fill();
        }
        const y = r.minY + downFraction(it.control, this.position(it.control)) * r.h;
        const capH = isTempo ? 16 : 14;
        this._cap(new Rect(r.minX + 3, y - capH / 2, r.w - 6, capH), this.isPressed(it.control), true, take ? !take.active : false);
    }

    _hfaderCap(it, lk) {
        const g = this.g, r = it.rect;
        const take = this._takeovers.get(it.control);
        if (take) {
            const x = r.minX + clamp(take.target) * r.w;
            g.fillStyle = col(this._takeoverColor(lk));
            g.beginPath();
            for (const [y, dir] of [[r.minY - 1, 1], [r.maxY + 1, -1]]) {
                g.moveTo(x - 5, y); g.lineTo(x, y + 7 * dir); g.lineTo(x + 5, y); g.closePath();
            }
            g.fill();
        }
        const x = r.minX + this.position(it.control) * r.w;
        this._cap(new Rect(x - 7, r.minY + 2, 14, r.h - 4), this.isPressed(it.control), false, take ? !take.active : false);
    }

    _cap(cap, pressed, vertical, inactive) {
        const g = this.g;
        if (inactive) box(g, cap, S.s1, col(S.fg2, 0.7), 1, true);   // hollow: not in control until moved past the mark
        else box(g, cap, pressed ? S.s3 : S.s2, pressed ? S.fg2 : S.s3);
        const c = inactive ? S.dim : S.fg;
        if (vertical) fillRect(g, new Rect(cap.minX + 3, cap.midY - 1, cap.w - 6, 2), c);
        else fillRect(g, new Rect(cap.midX - 1, cap.minY + 3, 2, cap.h - 6), c);
    }

    // MARK: - Jog: the deck's display

    _jog(it, lk) {
        const g = this.g, deck = parseControl(it.control).deck;
        const r = it.rect, cx = r.midX, cy = r.midY, R = r.w / 2;
        const touched = this.jogTouched === deck || this.hwPressed.has(C.jogTouch(deck)) || this.isPressed(C.jog(deck)) || lk.light === ON;
        const angle = this.jogAngle[deck];
        const accent = lk.color;
        const tracks = this._deckTracks[deck] ?? [];

        // outer ring with grip notches that turn with the platter
        disc(g, r, S.s2, S.s3);
        this._stroke(S.line, 1.5);
        g.beginPath();
        for (let i = 0; i < 72; i++) {
            const a = angle + i * Math.PI / 36;
            g.moveTo(cx + Math.cos(a) * R * 0.88, cy + Math.sin(a) * R * 0.88);
            g.lineTo(cx + Math.cos(a) * (R - 3), cy + Math.sin(a) * (R - 3));
        }
        g.stroke();
        const pr = R * 0.85;
        const pRect = Rect.circle(cx, cy, pr);
        disc(g, pRect, S.s1, S.s3);
        const cr = R * 0.42;

        if (!tracks.length) {
            this._stroke(S.line, 1);
            ellipsePath(g, pRect.inset(pr * 0.28, pr * 0.28)); g.stroke();
            this._stroke(accent ?? S.fg, 3);
            g.lineCap = "round";
            g.beginPath();
            g.moveTo(cx + Math.cos(angle) * pr * 0.5, cy + Math.sin(angle) * pr * 0.5);
            g.lineTo(cx + Math.cos(angle) * pr * 0.95, cy + Math.sin(angle) * pr * 0.95);
            g.stroke();
            g.lineCap = "butt";
        } else {
            this._deckTracks4(tracks, cx, cy, cr, pr);
            // the marker on the grip ring: the dial (it points into the active track's quadrant), else the platter
            const dial = this._jogDials[deck];
            const ma = dial != null ? ringCentreAngle(0, tracks.length) + dial * 2 * Math.PI / tracks.length : angle;
            this._stroke(accent ?? S.fg, 4);
            g.beginPath();
            g.moveTo(cx + Math.cos(ma) * R * 0.87, cy + Math.sin(ma) * R * 0.87);
            g.lineTo(cx + Math.cos(ma) * (R - 2), cy + Math.sin(ma) * (R - 2));
            g.stroke();
        }

        // touched: the scratch ring, its dashes turning with the platter
        if (touched) {
            g.save();
            this._stroke(accent ?? S.fg, 3);
            g.setLineDash([10, 6]);
            g.lineDashOffset = -angle * (pr + 3);
            g.beginPath(); g.arc(cx, cy, pr + 3, 0, 2 * Math.PI); g.stroke();
            g.restore();
        }

        // centre: the deck's title and value (the active track's machine)
        disc(g, Rect.circle(cx, cy, cr), S.s2, S.s3);
        const cs = this.capSize, llh = lineHeight(this.lblSize), lh = lineHeight(cs), vlh = lineHeight(this.valSize);
        const foot = touched ? "SCRATCH" : "JOG";
        const footColor = touched ? (accent ?? S.fg) : this.labelColor;
        if (lk.title) {
            const lines = wrap(lk.title, maxChars(cr * 1.75, cs), lk.value == null ? 2 : 1);
            const h = lines.length * lh + (lk.value != null ? vlh : 0) + llh;
            let y = cy - h / 2 + lh / 2;
            const tc = lk.hasFace ? S.fg : this.titleColor(it, lk);
            for (const line of lines) { drawText(g, line, cs, tc, cx, y); y += lh; }
            y -= lh / 2;
            if (lk.value != null) { this._line(lk.value, this.valSize, accent ?? S.fg2, cx, y + vlh / 2, "center", cr * 1.8); y += vlh; }
            this._label(foot, cx, y + llh / 2 + 1, "center", footColor);
        } else {
            this._label(foot, cx, cy, "center", footColor);
        }
    }

    /** the deck's tracks as arc segments between the centre and the platter's edge: a coloured band (bright when
     *  active, hollow when muted), the name, and a level meter along the inner edge */
    _deckTracks4(tracks, cx, cy, inner, outer) {
        const g = this.g, n = tracks.length;
        const span = 2 * Math.PI / n, gap = 0.09;
        const bandR = outer - 9, meterR = inner + 9, nameR = (bandR + meterR) / 2 + 1;
        tracks.forEach((t, i) => {
            const mid = ringCentreAngle(i, n);
            const a0 = mid - span / 2 + gap, a1 = mid + span / 2 - gap;
            g.lineCap = "butt";
            if (t.muted) {
                this._stroke(col(t.color, 0.6), 1.5);
                g.beginPath();
                g.arc(cx, cy, bandR + 5, a0, a1, false);
                g.arc(cx, cy, bandR - 5, a1, a0, true);
                g.closePath();
                g.stroke();
            } else {
                this._stroke(col(t.color, t.active ? 1 : 0.5), t.active ? 12 : 8);
                g.beginPath(); g.arc(cx, cy, bandR, a0, a1, false); g.stroke();
            }
            this._stroke(S.s2, 6);
            g.beginPath(); g.arc(cx, cy, meterR, a0, a1, false); g.stroke();
            const lv = clamp(t.level ?? 0);
            if (!t.muted && lv > 0.005) {
                this._stroke(col(t.color, t.active ? 1 : 0.75), 6);
                g.beginPath(); g.arc(cx, cy, meterR, a0, a0 + (a1 - a0) * lv, false); g.stroke();
            }
            const px = cx + Math.cos(mid) * nameR, py = cy + Math.sin(mid) * nameR;
            const nc = t.muted ? S.dim : (t.active ? S.fg : S.fg2);
            const name = t.muted ? truncate(String(t.name).toUpperCase(), 6) : String(t.name).toUpperCase();
            this._line(name, this.valSize, nc, px, py - (t.muted ? 4 : 0), "center", 56);
            if (t.muted) this._label("MUTE", px, py + 7, "center", S.dim);
        });
    }

    _drawMeters() {
        const g = this.g, r = this.G.meterRect;
        const top = r.minY + 4, h = r.h - 8;
        const segs = METER_SEGMENTS, gap = 3;
        const segH = (h - (segs - 1) * gap) / segs;
        [r.minX + 2, r.minX + 11].forEach((x, k) => {
            const lit = segments(this._meters[k]);
            for (let i = 0; i < segs; i++) {
                const y = top + h - (i + 1) * segH - i * gap;
                const color = i >= segs - 2 ? S.red : (i >= segs - 5 ? TRACK4[2] : TRACK4[1]);
                fillRect(g, new Rect(x, y, 7, segH), i < lit ? color : S.s2);
            }
        });
    }
}
