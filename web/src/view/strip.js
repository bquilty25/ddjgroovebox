// SPDX-License-Identifier: GPL-3.0-only
// The strip above the controller (Swift's TrigStripView), on a canvas, in the controller's three columns: tracks 1-4
// over the left deck, the transport and the stage over the mixer, tracks 5-8 over the right deck. Each track row: its
// colour, machine, every step of the track (trigs, locks, the playhead, the page on the pads framed), mute and level.
// The stage in the middle shows a figure per track (jumping when it plays), the page's picture while a value on it
// changes (scope.js), the step pages just after a page change, and the sound packs while browsing them.
//
// It cannot read the engine: the app gives it a plain object each tick, strip.update(input):
//   header        the same object as DDJView.header (Swift's TrigHeader): pattern, queued, bpm, swing, playing,
//                 recording, fill, track (the active track 0..7), machine, page (index into TRIG_PAGES), stepPage,
//                 pages (the active track's step page count), perf (string or null), sceneEdit (null, 0 or 1)
//   rows          8 of { name: machine name ("KICK" ...), color: ARGB, active, muted, level: 0..1 (engine.trackLevel),
//                 steps: one per step of the track's length, 0 off, 1 trig, 2 locks only, 3 trig with locks,
//                 playhead: the playing step or null }
//   crossfader    0..1 (the scene morph)            autopilot  boolean
//   connected     a controller is connected         problem    null or a line in red (audio not running)
//   scope         null or the page's picture (see scope.js: page, machine, track, color, raw[8], normalised[8], shown[8])
//   packs         { names: [string], cursor, browsedAt, loaded: null or { index, at }, revertedAt: null or time }
//   lengthChangedAt  the time of the last length change (SHIFT + PAGE, SHIFT + BROWSE)
//   clock         the current time on the clock of browsedAt, loaded.at, revertedAt (the mapper's), in seconds;
//                 left out: performance.now() / 1000 is used for both
// Other API: new StripView(canvas), resize(cssW, cssH), preferredHeight(width), columns = { mixerStart, rightStart }
// (fractions of the width, DDJView.columnFractions), onSelectTrack = (track) => {} for a click on a row, requestRedraw().

import { COLORS, TRIG_PAGES } from "../vocab.js";
import { S, TRACK4, col, Rect, drawTextTop, textWidth, fillRect, sizeCanvas } from "./style.js";
import { drawScope, scopeEquals } from "./scope.js";
import { geometry, MIN_DESIGN_W } from "./ddjview.js";

const SCOPE_SHOWN = 4.0;      // seconds the page's picture stays after an edit
const DECK_HEAD = 20;         // (DJ decks) the height of each side's deck header
const PAGE_MAP_SHOWN = 2.5;
const pad2 = (n) => String(n).padStart(2, "0");

export class StripView {
    constructor(canvas) {
        this.canvas = canvas;
        this.g = canvas.getContext("2d");
        const G = geometry(MIN_DESIGN_W);
        this.columns = { mixerStart: G.mixerX / G.designW, rightStart: G.rightDeckX / G.designW };
        this.onSelectTrack = null;
        this.rows = [];
        this.decks = [];
        this.blend = null;
        this.stageRows = [];
        this.center = null;
        this.stageLevels = new Array(8).fill(0);
        this.stageStep = null;
        this.jump = new Array(8).fill(0);
        this.lastHit = new Array(8).fill(-100);
        this.stageScope = null;
        this.lastEdit = -100;
        this.packs = []; this.packCursor = 0; this.packBrowsed = -100; this.packLoaded = null; this.packReverted = null;
        this.clockOffset = 0;
        this.stepPageSeen = [0, 1, 0];
        this.pageChanged = -100;
        this.lengthSeen = -100;
        this.cartPos = 0;
        this.lastDraw = null;
        this._frame = 0;
        this.cssW = 0; this.cssH = 0; this.dpr = 1;
        this.now = () => performance.now() / 1000;

        // (DJ decks) 16 rows: deck A's 8 tracks on the left (0-7), deck B's on the right (8-15)
        canvas.addEventListener("pointerdown", (e) => {
            if (e.button !== 0) return;
            for (let t = 0; t < 16; t++) if (this.rowRect(t).contains(e.offsetX, e.offsetY)) { this.onSelectTrack?.(t); return; }
        });
        canvas.addEventListener("pointermove", (e) => {
            let over = false;
            for (let t = 0; t < 16; t++) if (this.rowRect(t).contains(e.offsetX, e.offsetY)) over = true;
            canvas.style.cursor = over ? "pointer" : "";
        });
        document.fonts?.addEventListener?.("loadingdone", () => this.requestRedraw());
    }

    preferredHeight(w) { return Math.round(w * 0.2); }   // (DJ decks: 8 rows a side)

    resize(w, h) {
        this.cssW = w; this.cssH = h;
        this.dpr = sizeCanvas(this.canvas, w, h);
        this._draw();
    }

    requestRedraw() {
        if (this._frame) return;
        this._frame = requestAnimationFrame(() => { this._frame = 0; this._draw(); });
    }

    /** the app's state, once per tick (see the header comment) */
    update(input) {
        const h = input.header;
        const now = this.now();
        const clock = input.clock ?? now;
        this.rows = (input.rows ?? []).map((r) => ({
            name: r.name, color: r.color, active: !!r.active, muted: !!r.muted,
            level: Math.round((r.level ?? 0) * 10), steps: r.steps?.length ? r.steps : [0], playhead: r.playhead ?? null,
            page: h.stepPage,
        }));
        // (DJ decks) the two decks' headers; the blend (null: none)
        this.decks = input.decks ?? [];
        this.blend = input.blend ?? null;
        this.focusDeck = input.focus ?? 0;
        const names = input.patternNames ?? [];                  // "P01 DEEP"
        const named = (p) => `P${pad2(p + 1)}` + (names[p] ? " " + names[p] : "");
        const pat = named(h.pattern) + (h.queued != null ? " → " + named(h.queued) : "");
        this.center = {
            pattern: pat, bpm: h.bpm.toFixed(0),
            transport: h.recording ? "● REC" : h.playing ? "▶ PLAY" : "■ STOP",
            page: TRIG_PAGES[h.page] ?? "", pageColor: COLORS.page[h.page] ?? S.fg,
            crossfader: input.crossfader ?? 0.5, autopilot: !!input.autopilot, perf: h.perf ?? null,
            connected: !!input.connected, scene: h.sceneEdit ?? null, problem: input.problem ?? null,
        };
        // the stage: each track's level (its figure jumps: the deck on air's), the playhead, the transport
        const air = (input.onAir ?? 0) * 8, airRows = (input.rows ?? []).slice(air, air + 8);
        this.stageRows = airRows.map((r) => ({ name: r.name, color: r.color, muted: !!r.muted }));
        this.stageLevels = airRows.map((r) => r.level ?? 0);
        const p0 = airRows[0]?.playhead;
        this.stageStep = h.playing && p0 != null ? p0 % 16 : null;
        // the page's picture: shown while the page, the track or a value on it changes, and a few seconds after
        // the scenes' picture: shown while the crossfader moves or a scene is being edited, and a few seconds after
        this.sceneRows = input.sceneRows ?? [];
        const xf = input.crossfader ?? 0.5;
        if ((this.lastXf !== undefined && xf !== this.lastXf) || h.sceneEdit != null) this.sceneShown = now;
        this.lastXf = xf;
        // (scopeKey, when given: what the knobs show; the scope itself is drawn from the live values)
        const scope = input.scope ?? null, key = input.scopeKey ?? null;
        if (key !== null) {
            if (this.scopeKey != null && key !== this.scopeKey) this.lastEdit = now;
            this.scopeKey = key;
        } else if (!scopeEquals(scope, this.stageScope) && this.stageScope) this.lastEdit = now;
        this.stageScope = scope;
        // the pack browser (MASTER page, BROWSE): shown while browsing and a few seconds after
        const pk = input.packs ?? {};
        this.packs = pk.names ?? [];
        this.packKind = pk.kind ?? "pack";
        this.packCursor = pk.cursor ?? 0;
        this.packLoaded = pk.loaded ?? null;
        this.packReverted = pk.revertedAt ?? null;
        this.packBrowsed = pk.browsedAt ?? -100;
        this.clockOffset = clock - now;
        // the step page: a change shows the page map for a moment
        const sp = [h.stepPage, Math.max(1, h.pages ?? 1), h.track];
        const seen = this.stepPageSeen;
        if (sp[0] !== seen[0] || sp[1] !== seen[1] || sp[2] !== seen[2]) {
            if ((seen[1] > 1 || sp[1] > 1) && (sp[0] !== seen[0] || sp[2] !== seen[2])) this.pageChanged = now;
            this.stepPageSeen = sp;
        }
        const lc = input.lengthChangedAt ?? -100;
        if (lc !== this.lengthSeen) { this.lengthSeen = lc; this.pageChanged = now; }
        this.requestRedraw();
    }

    // MARK: - Geometry

    get W() { return this.cssW || this.canvas.clientWidth; }
    get H() { return this.cssH || this.canvas.clientHeight; }
    get mixerX() { return Math.round(this.W * this.columns.mixerStart); }
    get rightX() { return Math.round(this.W * this.columns.rightStart); }
    /** (DJ decks) row t: deck A's tracks 0-7 on the left, deck B's 8-15 on the right, under each side's header */
    rowRect(t) {
        const top = 8 + DECK_HEAD, rowH = (this.H - 8 - top) / 8;
        const x0 = t < 8 ? 8 : this.rightX + 8;
        const colW = (t < 8 ? this.mixerX : this.W - this.rightX) - 16;
        return new Rect(x0, top + (t % 8) * rowH, colW, rowH - 2);
    }
    deckHeadRect(d) {
        const x0 = d === 0 ? 8 : this.rightX + 8, colW = (d === 0 ? this.mixerX : this.W - this.rightX) - 16;
        return new Rect(x0, 6, colW, DECK_HEAD - 2);
    }
    get centerRect() { return new Rect(this.mixerX + 10, 8, this.rightX - this.mixerX - 20, this.H - 16); }
    /** the empty middle of the strip, under the page line */
    get stageRect() {
        const r = this.centerRect;
        return new Rect(r.minX, r.minY + 104, r.w, Math.max(0, r.maxY - 4 - (r.minY + 104)));
    }

    // MARK: - Draw

    _draw() {
        const g = this.g, cv = this.canvas;
        if (!this.cssW) { this.cssW = cv.clientWidth; this.cssH = cv.clientHeight; this.dpr = sizeCanvas(cv, this.cssW, this.cssH); }
        g.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
        const W = this.W, H = this.H, mx = this.mixerX, rx = this.rightX;
        fillRect(g, new Rect(0, 0, W, H), S.s1);
        // the three columns, open at the bottom: the controller continues below
        g.strokeStyle = col(S.line); g.lineWidth = 1;
        g.beginPath();
        for (const x of [0.5, mx + 0.5, rx + 0.5, W - 0.5]) { g.moveTo(x, H); g.lineTo(x, 0.5); }
        g.moveTo(0, 0.5); g.lineTo(W, 0.5);
        g.stroke();
        this.decks.forEach((d, i) => this._deckHead(d, this.deckHeadRect(i)));
        this.rows.forEach((r, t) => { if (t < 16) this._row(r, (t % 8) + 1, this.rowRect(t)); });
        if (this.center) this._center(this.centerRect);
        g.save();
        this._stage(this.stageRect);
        g.restore();
    }

    /** (DJ decks) a side's header: "A  P01 DEEP → P02", playing or not, on air, edited (outlined) */
    _deckHead(d, r) {
        const g = this.g;
        if (d.focus) { g.strokeStyle = col(COLORS.page[6]); g.lineWidth = 1.5; g.strokeRect(r.x - 4, r.y - 2, r.w + 8, r.h + 2); }
        let x = r.minX;
        x += drawTextTop(g, d.label, x, r.minY + 1, 14, d.onAir ? S.fg : S.fg2) + 10;
        x += drawTextTop(g, d.pattern, x, r.minY + 2, 12, d.playing ? S.fg : S.fg2) + 12;
        const state = d.playing ? (d.onAir ? "▶ ON AIR" : "▶ PLAYING") : "■ WAITING";
        drawTextTop(g, state, x, r.minY + 2, 12, d.playing ? TRACK4[1] : S.dim);
        if (d.focus) drawTextTop(g, "EDITING", r.maxX, r.minY + 2, 12, COLORS.page[6], true);
    }

    _row(r, number, rect) {
        const g = this.g;
        const size = rect.h > 40 ? 16 : 12;
        const c = r.color;
        // number chip and machine
        const chip = new Rect(rect.minX, rect.minY + 2, rect.h - 4, rect.h - 4);
        fillRect(g, chip, r.active ? col(c) : col(c, 0.35));
        drawTextTop(g, String(number), chip.midX - size * 0.25, chip.midY - size * 0.5, size, r.active ? S.bg : S.fg);
        const nameW = Math.min(96, rect.w * 0.22);
        drawTextTop(g, r.muted ? "MUTED" : r.name, chip.maxX + 6, rect.midY - size * 0.5, size,
                    r.muted ? S.dim : (r.active ? S.fg : S.fg2));
        // the whole track (up to 4 bars), a gap between bars; the 16 steps the pads show are framed
        const gx = chip.maxX + 10 + nameW, gw = rect.maxX - gx - 14;
        const n = r.steps.length, bars = Math.floor((n + 15) / 16);
        const barGap = bars > 1 ? 4 : 0;
        const cell = (gw - (bars - 1) * barGap) / Math.max(16, n);
        const pad = cell > 8 ? 1 : 0.5;
        const x = (i) => gx + i * cell + Math.floor(i / 16) * barGap;
        const cy = rect.minY + rect.h * 0.25, ch = rect.h * 0.5;
        r.steps.forEach((s, i) => {
            const cr = new Rect(x(i) + pad, cy, cell - 2 * pad, ch);
            // s: 1 a trig, 2 locks, 4 a condition (it plays only some times: drawn half full)
            let fill;
            if (s & 1) fill = r.muted ? col(c, 0.3) : col(c);
            else if (s & 2) fill = col(c, 0.25);
            else fill = col(i % 4 === 0 ? S.s3 : S.s2);
            if (s & 4) {
                fillRect(g, cr, col(c, 0.25));
                fillRect(g, new Rect(cr.minX, cr.midY, cr.w, cr.h / 2), fill);
            } else fillRect(g, cr, fill);
            if ((s & 2) && cell > 6) fillRect(g, new Rect(cr.midX - 1.5, cr.maxY - 5, 3, 3), S.fg);
            if (r.playhead === i) {
                g.strokeStyle = col(S.fg); g.lineWidth = 2;
                g.strokeRect(cr.x - 1, cr.y - 1, cr.w + 2, cr.h + 2);
            }
        });
        if (bars > 1 && r.active) {   // the page on the pads (the active track's): a frame
            const p = Math.min(r.page, bars - 1);
            const fx = x(p * 16) - 2;
            g.strokeStyle = col(S.fg); g.lineWidth = 1.5;
            g.strokeRect(fx, cy - 4, x(Math.min(n, p * 16 + 16) - 1) + cell - x(p * 16) + 4, ch + 8);
        }
        // level
        const lv = new Rect(rect.maxX - 8, rect.minY + 2, 5, rect.h - 4);
        fillRect(g, lv, S.s2);
        const h = lv.h * Math.min(10, r.level) / 10;
        fillRect(g, new Rect(lv.minX, lv.maxY - h, lv.w, h), c);
    }

    _center(r) {
        const g = this.g, c = this.center;
        let y = r.minY;
        // TRIGDECK, in the track colours
        let x = r.minX;
        [..."TRIGDECK"].forEach((ch, i) => { x += drawTextTop(g, ch, x, y, 16, COLORS.track[i]); });
        drawTextTop(g, c.connected ? "DDJ ●" : "NO DDJ", r.maxX, y, 12, c.connected ? TRACK4[1] : S.dim, true);
        y += 22;
        drawTextTop(g, c.bpm, r.minX, y, 32, S.fg);
        drawTextTop(g, "BPM", r.minX + 70, y + 14, 12, S.dim);
        drawTextTop(g, c.pattern, r.maxX, y, 16, S.fg, true);
        drawTextTop(g, c.transport, r.maxX, y + 18, 12, c.transport.startsWith("●") ? S.red : S.fg2, true);
        y += 40;
        drawTextTop(g, c.page, r.minX, y, 16, c.pageColor);
        if (c.perf) drawTextTop(g, "BEAT FX " + c.perf, r.maxX, y, 16, 0xFF2E7CFF, true);
        else if (c.autopilot) drawTextTop(g, "AUTOPILOT", r.maxX, y, 12, TRACK4[2], true);
        if (c.scene != null) {
            drawTextTop(g, c.scene === 0 ? "SCENE A EDIT" : "SCENE B EDIT", r.minX + 70, y + 2, 12, c.scene === 0 ? COLORS.sceneA : COLORS.sceneB);
        }
        if (c.problem) drawTextTop(g, c.problem, r.minX, y + 22, 12, S.red);
        // (the scene morph is the controller's crossfader, drawn at the bottom of the mixer: not repeated here)
    }

    /** The scenes: the crossfader's middle plays the pattern as written; towards A (left) or B (right) every
     *  parameter a scene holds morphs to the scene's value. One row per parameter: A's value, the written value (a
     *  thin line), B's value, and where the crossfader puts it now (the bright marker). */
    _scenes(r) {
        const g = this.g, rows = this.sceneRows ?? [], xf = this.center.crossfader ?? 0.5;
        const amtA = xf < 0.5 ? (0.5 - xf) * 2 : 0, amtB = xf > 0.5 ? (xf - 0.5) * 2 : 0;
        drawTextTop(g, "SCENE A", r.minX, r.minY, 12, COLORS.sceneA);
        drawTextTop(g, "SCENE B", r.maxX, r.minY, 12, COLORS.sceneB, true);
        const where = amtA > 0.005 ? `${Math.round(amtA * 100)}% TOWARDS A` : amtB > 0.005 ? `${Math.round(amtB * 100)}% TOWARDS B`
                                                                                         : "THE PATTERN AS WRITTEN";
        drawTextTop(g, where, r.midX - textWidth(where, 12) / 2, r.minY, 12, S.fg);
        if (!rows.length) {
            const lines = ["THIS PATTERN HAS NO SCENES.", "HOLD SCENE A OR SCENE B (THE CUE BUTTONS) AND TURN KNOBS:",
                           "THE CROSSFADER THEN MORPHS THE SOUND TO THEM."];
            lines.forEach((l, i) => drawTextTop(g, l, r.minX, r.minY + 30 + i * 16, 12, S.fg2));
            return;
        }
        // every row shown: up to four columns, the rows closer together when even that is not enough
        const top = r.minY + 22, h = r.maxY - top;
        let rowH = 16, perCol = Math.max(1, Math.floor(h / rowH));
        const cols = Math.min(4, Math.ceil(rows.length / perCol));
        if (rows.length > perCol * cols) { perCol = Math.ceil(rows.length / cols); rowH = Math.max(9, h / perCol); }
        perCol = Math.max(1, Math.min(perCol, Math.floor(h / rowH)));
        const colW = (r.w - (cols - 1) * 16) / cols;
        const n = rows.length > perCol * cols ? perCol * cols - 1 : rows.length;   // (only if it still does not fit)
        const labelW = Math.min(130, colW * 0.42);
        for (let i = 0; i < n; i++) {
            const row = rows[i], y = top + (i % perCol) * rowH;
            const x0 = r.minX + Math.floor(i / perCol) * (colW + 16), bx = x0 + labelW, bw = colW - labelW - 6;
            const px = (v) => bx + (row.max > row.min ? (v - row.min) / (row.max - row.min) : 0.5) * bw;
            const k = rowH / 16;                       // (the marks shrink with the rows)
            drawTextTop(g, row.label, x0, y, Math.min(11, rowH - 2), row.color);
            fillRect(g, new Rect(bx, y + 6 * k, bw, 1), S.s3);
            if (row.a != null) fillRect(g, new Rect(px(row.a) - 2, y + 2 * k, 4, 9 * k), COLORS.sceneA);
            if (row.b != null) fillRect(g, new Rect(px(row.b) - 2, y + 2 * k, 4, 9 * k), COLORS.sceneB);
            fillRect(g, new Rect(px(row.base) - 0.5, y + k, 1, 11 * k), S.fg2);
            let v = row.base;
            if (amtA > 0 && row.a != null) v += (row.a - row.base) * amtA;
            if (amtB > 0 && row.b != null) v += (row.b - row.base) * amtB;
            fillRect(g, new Rect(px(v) - 1.5, y, 3, 13 * k), S.fg);
        }
        if (rows.length > n) {
            drawTextTop(g, `+${rows.length - n} MORE`, r.minX + Math.floor(n / perCol) * (colW + 16), top + (n % perCol) * rowH, 11, S.fg2);
        }
    }

    // MARK: - The stage

    /** One little figure per track, in its colour and its machine's shape, jumping when it plays, blinking when it does
     *  not; the floor is the 16 steps with the playhead. */
    _stage(r) {
        const g = this.g;
        if (r.h < 24 || (this.stageRows ?? []).length !== 8) return;
        const t = this.now();
        const dt = this.lastDraw == null ? 1 / 30 : Math.min(0.5, Math.max(0, t - this.lastDraw));
        this.lastDraw = t;
        const floorY = r.maxY - 6;
        const sw = r.w / 16;
        for (let i = 0; i < 16; i++) {
            const lit = this.stageStep === i;
            fillRect(g, new Rect(r.minX + i * sw + 2, floorY + 1, sw - 4, lit ? 4 : 2), lit ? S.fg : (i % 4 === 0 ? S.s3 : S.s2));
        }
        // (a beat FX's name is on the page line above, not here: it covered the page's picture)
        // a hit lifts a figure at once; it falls back (0.82 a frame at 30 frames a second)
        const fall = Math.pow(0.82, dt * 30);
        for (let i = 0; i < 8; i++) {
            const lv = Math.min(1, (this.stageLevels[i] ?? 0) * 1.6);
            if (lv > this.jump[i] + 0.15) this.lastHit[i] = t;
            this.jump[i] = lv > this.jump[i] ? lv : this.jump[i] * fall;
        }
        const inner = new Rect(r.minX, r.minY, r.w, floorY - r.minY - 2);
        // the pack browser first (MASTER page, BROWSE turned or pushed), on the mapper's clock
        const pt = t + this.clockOffset, browsed = pt - this.packBrowsed;
        if (this.packs.length && browsed < SCOPE_SHOWN + 0.4) {
            const a = Math.min(1, (SCOPE_SHOWN + 0.4 - browsed) / 0.4);
            g.save(); g.globalAlpha = a; this._packs(inner, pt); g.restore();
            if (a >= 1) return;
            g.globalAlpha = 1 - a;
        }
        // the step pages, just after PAGE - / PAGE + (or a track with several)
        const paged = t - this.pageChanged;
        if (paged < PAGE_MAP_SHOWN + 0.4) {
            const a = Math.min(1, (PAGE_MAP_SHOWN + 0.4 - paged) / 0.4);
            g.save(); g.globalAlpha = a; this._pageMap(inner); g.restore();
            if (a >= 1) return;
            g.globalAlpha = 1 - a;
        }
        // (DJ decks) a blend: the Smart Fader's picture, all through it
        if (this.blend) { this._blend(inner); return; }
        // the scenes, while the crossfader moves or a scene is edited
        const sceneT = t - (this.sceneShown ?? -100);
        if (sceneT < SCOPE_SHOWN + 0.4) {
            const a = Math.min(1, (SCOPE_SHOWN + 0.4 - sceneT) / 0.4);
            g.save(); g.globalAlpha = a; this._scenes(inner); g.restore();
            if (a >= 1) return;
            g.globalAlpha = 1 - a;
        }
        // the page's picture, while editing; fading back to the figures after
        const shown = t - this.lastEdit;
        if (this.stageScope && shown < SCOPE_SHOWN + 0.4) {
            const a = Math.min(1, (SCOPE_SHOWN + 0.4 - shown) / 0.4);
            g.save(); g.globalAlpha = a;
            const since = t - (this.lastHit[this.stageScope.track] ?? -100);
            drawScope(g, this.stageScope, inner, since < 2 ? since : null);
            g.restore();
            if (a >= 1) return;
            g.globalAlpha = 1 - a;
        }
        const cell = r.w / 8;
        const base = Math.min(r.h * 0.42, cell * 0.5);
        for (let i = 0; i < 8; i++) {
            const row = this.stageRows[i];
            const j = row.muted ? 0 : this.jump[i];
            const breathe = 1 + 0.04 * Math.sin(t * 2.2 + i);
            const wobble = this.center.perf ? Math.sin(t * 11 + i * 0.8) * 3 : 0;
            const w = base * (1 - 0.12 * j), h = base * (1 + 0.25 * j) * breathe;
            const lift = j * (r.h - h - 10);
            const cx = r.minX + (i + 0.5) * cell + wobble;
            const b = new Rect(cx - w / 2, floorY - 2 - lift - h, w, h);
            const path = figure(row.name, b, j);
            if (row.muted) {
                g.strokeStyle = col(row.color, 0.45); g.lineWidth = 1.5; g.stroke(path);
            } else {
                g.fillStyle = col(row.color);
                g.fill(path, row.name === "SAMPLE" ? "evenodd" : "nonzero");   // (a ring)
            }
            // eyes: two dots (a blink now and then; closed when muted)
            const ey = b.minY + b.h * 0.38, ex = b.w * 0.18, er = Math.max(1.5, base * 0.07);
            const blink = row.muted || (t + i * 1.7) % 4.3 < 0.13;
            g.fillStyle = row.muted ? col(row.color, 0.45) : col(S.bg);
            for (const dx of [-ex, ex]) {
                if (blink) g.fillRect(b.midX + dx - er, ey - 0.75, 2 * er, 1.5);
                else { g.beginPath(); g.arc(b.midX + dx, ey, er, 0, 2 * Math.PI); g.fill(); }
            }
            // the active track: a marker above its figure
            if (row.active) {
                g.fillStyle = col(row.color);
                const my = Math.max(r.minY + 2, b.minY - 10);
                g.beginPath(); g.moveTo(cx - 5, my); g.lineTo(cx + 5, my); g.lineTo(cx, my + 6); g.closePath(); g.fill();
            }
        }
    }

    /** (DJ decks) The Smart Fader at work: each deck's volume and low / mid / high (the bass swapping in the middle),
     *  the fader's position from the old deck to the new one, the tempo gliding between the two. */
    _blend(r) {
        const g = this.g, b = this.blend, to = b.to, from = 1 - to;
        const name = (d) => (d === 0 ? "A" : "B");
        const deckCol = [COLORS.sceneA, COLORS.sceneB];
        const title = `BLEND ${name(from)} → ${name(to)}  ${Math.round(b.x * 100)}%`;
        drawTextTop(g, title, r.midX - textWidth(title, 14) / 2, r.minY, 14, S.fg);
        const tempo = Math.abs(b.bpmTo - b.bpmFrom) < 0.05 ? `${b.bpm.toFixed(0)} BPM`
                    : `${b.bpmFrom.toFixed(0)} → ${b.bpmTo.toFixed(0)} BPM: ${b.bpm.toFixed(1)}`;
        drawTextTop(g, tempo, r.midX - textWidth(tempo, 11) / 2, r.minY + 18, 11, S.fg2);
        // the bars: per deck VOL, LOW, MID, HIGH (1: as is; a band up to +6 dB reaches the top)
        const top = r.minY + 36, bh = Math.max(10, r.maxY - top - 26), half = r.w / 2;
        for (const d of [0, 1]) {
            const x0 = r.minX + d * half + 8, bw = (half - 16) / 4, gs = b.gains[d];
            drawTextTop(g, `DECK ${name(d)}` + (d === to ? " (IN)" : " (OUT)"), x0, top - 2, 11, deckCol[d]);
            const vals = [gs.gain, ...gs.bands];
            ["VOL", "LOW", "MID", "HIGH"].forEach((lab, i) => {
                const bx = x0 + i * bw + 2, v = Math.min(1, vals[i] / 1.5);
                const bar = new Rect(bx, top + 14, bw - 6, bh - 14);
                fillRect(g, bar, S.s2);
                fillRect(g, new Rect(bar.minX, bar.maxY - v * bar.h, bar.w, v * bar.h), col(deckCol[d], i === 0 ? 1 : 0.7));
                fillRect(g, new Rect(bar.minX, bar.maxY - bar.h / 1.5, bar.w, 1), S.fg2);   // (1: as is)
                drawTextTop(g, lab, bar.midX - textWidth(lab, 9) / 2, bar.maxY + 2, 9, S.dim);
            });
        }
        // the fader, the bass swap marked in the middle
        const fy = r.maxY - 8, fx0 = r.minX + 20, fw = r.w - 40;
        fillRect(g, new Rect(fx0, fy, fw, 3), S.s3);
        fillRect(g, new Rect(fx0 + fw * 0.45, fy - 3, fw * 0.1, 9), col(S.fg2, 0.4));
        const px = to === 1 ? fx0 + b.x * fw : fx0 + (1 - b.x) * fw;     // (the fader goes towards the new deck's side)
        fillRect(g, new Rect(px - 2, fy - 5, 4, 13), S.fg);
        drawTextTop(g, "A", fx0 - 14, fy - 5, 11, deckCol[0]);
        drawTextTop(g, "B", fx0 + fw + 6, fy - 5, 11, deckCol[1]);
    }

    /** The active track's step pages as blocks, the one on the pads lit, the playhead running through them. */
    _pageMap(r) {
        const g = this.g;
        const [page, pages, t] = this.stepPageSeen;
        const ri = (this.focusDeck ?? 0) * 8 + t;            // (DJ decks: the deck edited's row)
        if (ri >= this.rows.length || pages <= 0) return;
        const row = this.rows[ri], c = row.color, n = row.steps.length;
        const title = `STEPS ${page * 16 + 1}-${Math.min(n, page * 16 + 16)}  ·  PAGE ${page + 1}/${pages}`
                    + `  ·  LENGTH ${n}  (SHIFT + PAGE ±: A BAR)`;
        drawTextTop(g, title, r.midX - textWidth(title, 13) / 2, r.minY, 13, S.fg);
        const gap = 10, top = r.minY + 22;
        const bw = Math.min(130, (r.w - gap * (pages - 1)) / pages), bh = Math.max(16, r.h - 30);
        const x0 = r.midX - (bw * pages + gap * (pages - 1)) / 2;
        for (let p = 0; p < pages; p++) {
            const b = new Rect(x0 + p * (bw + gap), top, bw, bh);
            const on = p === page;
            fillRect(g, b, on ? col(c, 0.18) : col(S.s2));
            const cw = b.w / 16;
            for (let i = 0; i < 16; i++) {
                const s = p * 16 + i;
                if (s >= n) break;
                const v = row.steps[s];
                fillRect(g, new Rect(b.minX + i * cw + 1, b.midY - 4, cw - 2, 8), (v & 1) ? col(c, on ? 1 : 0.5) : col(S.s3));
                if (row.playhead === s) fillRect(g, new Rect(b.minX + i * cw + cw * 0.3, b.minY + 3, cw * 0.4, b.h - 6), S.fg);
            }
            g.strokeStyle = col(on ? S.fg : S.s3); g.lineWidth = on ? 2 : 1;
            g.strokeRect(b.x, b.y, b.w, b.h);
            const lbl = `${p * 16 + 1}-${Math.min(n, p * 16 + 16)}`;
            drawTextTop(g, lbl, b.midX - textWidth(lbl, 10) / 2, b.maxY - 13, 10, on ? S.fg : S.fg2);
        }
    }

    /** The packs as cartridges in a row: the chosen one in the middle, big and lit; loading pushes it into the slot. */
    _packs(r, now) {
        const g = this.g, n = this.packs.length;
        if (!n) return;
        const target = this.packCursor;
        if (Math.abs(target - this.cartPos) > n / 2) this.cartPos = target; else this.cartPos += (target - this.cartPos) * 0.35;
        g.save();
        g.beginPath(); g.rect(r.x, r.y - 2, r.w, r.h + 4); g.clip();
        const cw = Math.min(120, r.w / 4.2), chh = Math.min(r.h - 30, cw * 0.72);
        const loadAge = this.packLoaded ? (this.packLoaded.index === this.packCursor ? now - this.packLoaded.at : 99) : 99;
        for (let i = 0; i < n; i++) {
            const off = i - this.cartPos;
            if (Math.abs(off) >= 3.2) continue;
            const chosen = i === this.packCursor;
            const scale = 1 - Math.min(0.3, Math.abs(off) * 0.18);
            const w = cw * scale, h = chh * scale;
            let y = r.minY + 4 + (chh - h) / 2;
            if (chosen && loadAge < 0.5) y += Math.sin(Math.min(1, loadAge / 0.25) * Math.PI / 2) * 6;   // pushed in
            const b = new Rect(r.midX + off * cw * 1.08 - w / 2, y, w, h);
            const a = chosen ? 1 : 0.45;
            const body = () => { g.beginPath(); g.roundRect(b.x, b.y, b.w, b.h, 6); };
            body(); g.fillStyle = col(S.s2, a); g.fill();
            body(); g.strokeStyle = col(chosen ? S.fg : S.s3); g.lineWidth = chosen ? 2 : 1; g.stroke();
            fillRect(g, new Rect(b.minX + 6, b.minY + 6, b.w - 12, Math.max(4, h * 0.14)), col(COLORS.track[i % 8], a));
            const size = Math.max(11, Math.min(26, h * 0.32));
            const name = this.packs[i];
            drawTextTop(g, name, b.midX - textWidth(name, size) / 2, b.midY - size / 2 + h * 0.06, size, col(chosen ? S.fg : S.fg2, a));
            g.fillStyle = col(S.s3, a);
            for (let k = 0; k < 5; k++) g.fillRect(b.midX - 14 + k * 6, b.maxY - 9, 3, 5);
            if (chosen && loadAge < 0.35) { body(); g.fillStyle = col(S.fg, 0.35 * (1 - loadAge / 0.35)); g.fill(); }
        }
        let status, hint;
        if (this.packKind === "pattern") {                    // (DJ decks) the patterns
            status = this.packLoaded && this.packLoaded.index === this.packCursor ? "LOADED" : "LOAD A / LOAD B: PUT IT ON A DECK";
            hint = `PATTERN P${pad2(this.packCursor + 1)} · ` + status;
        } else {
            if (this.packReverted != null && now - this.packReverted < 2) status = "REVERTED: THE KIT BEFORE";
            else if (this.packLoaded && this.packLoaded.index === this.packCursor) status = "LOADED · SHIFT + PUSH: REVERT";
            else status = "PUSH: LOAD (THE BEAT STAYS)";
            hint = `PACK ${this.packCursor + 1}/${n} · ` + status;
        }
        drawTextTop(g, hint, r.midX - textWidth(hint, 10) / 2, r.minY + 4 + chh + 10, 10, S.fg2);
        g.restore();
    }
}

/** a track's figure, by its machine: KICK a circle, SNARE / CLAP a square (it turns as it jumps), HAT a triangle,
 *  TOM a diamond, SYNTH a rounded bar, FM a plus, SAMPLE a ring */
export function figure(machine, b, spin) {
    const p = new Path2D();
    const ellipse = (q) => { p.moveTo(q.maxX, q.midY); p.ellipse(q.midX, q.midY, q.w / 2, q.h / 2, 0, 0, 2 * Math.PI); };
    switch (machine) {
    case "KICK": ellipse(b); break;
    case "SNARE": case "CLAP": {
        const a = spin * Math.PI / 4, cs = Math.cos(a), sn = Math.sin(a);
        const pts = [[-b.w / 2, -b.h / 2], [b.w / 2, -b.h / 2], [b.w / 2, b.h / 2], [-b.w / 2, b.h / 2]]
            .map(([x, y]) => [b.midX + x * cs - y * sn, b.midY + x * sn + y * cs]);
        pts.forEach(([x, y], i) => (i ? p.lineTo(x, y) : p.moveTo(x, y)));
        p.closePath();
        break;
    }
    case "HAT":
        p.moveTo(b.midX, b.minY); p.lineTo(b.maxX, b.maxY); p.lineTo(b.minX, b.maxY); p.closePath();
        break;
    case "TOM":
        p.moveTo(b.midX, b.minY); p.lineTo(b.maxX, b.midY); p.lineTo(b.midX, b.maxY); p.lineTo(b.minX, b.midY); p.closePath();
        break;
    case "FM": {
        const a = b.w / 3, c = b.h / 3;
        p.rect(b.minX + a, b.minY, a, b.h);
        p.rect(b.minX, b.minY + c, b.w, c);
        break;
    }
    case "SAMPLE":
        ellipse(b);
        ellipse(b.inset(b.w * 0.3, b.h * 0.3));
        break;
    default: {   // SYNTH and anything else
        const rr = Math.min(b.w, b.h) * 0.3;
        p.roundRect(b.x, b.y, b.w, b.h, rr);
    }
    }
    return p;
}
