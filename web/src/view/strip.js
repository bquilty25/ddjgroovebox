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

        canvas.addEventListener("pointerdown", (e) => {
            if (e.button !== 0) return;
            for (let t = 0; t < 8; t++) if (this.rowRect(t).contains(e.offsetX, e.offsetY)) { this.onSelectTrack?.(t); return; }
        });
        canvas.addEventListener("pointermove", (e) => {
            let over = false;
            for (let t = 0; t < 8; t++) if (this.rowRect(t).contains(e.offsetX, e.offsetY)) over = true;
            canvas.style.cursor = over ? "pointer" : "";
        });
        document.fonts?.addEventListener?.("loadingdone", () => this.requestRedraw());
    }

    preferredHeight(w) { return Math.round(w * 0.17); }

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
        const pat = `P${pad2(h.pattern + 1)}` + (h.queued != null ? ` → P${pad2(h.queued + 1)}` : "");
        this.center = {
            pattern: pat, bpm: h.bpm.toFixed(0),
            transport: h.recording ? "● REC" : h.playing ? "▶ PLAY" : "■ STOP",
            page: TRIG_PAGES[h.page] ?? "", pageColor: COLORS.page[h.page] ?? S.fg,
            crossfader: input.crossfader ?? 0.5, autopilot: !!input.autopilot, perf: h.perf ?? null,
            connected: !!input.connected, scene: h.sceneEdit ?? null, problem: input.problem ?? null,
        };
        // the stage: each track's level (its figure jumps), the playhead, the transport
        this.stageLevels = (input.rows ?? []).map((r) => r.level ?? 0);
        const p0 = input.rows?.[0]?.playhead;
        this.stageStep = h.playing && p0 != null ? p0 % 16 : null;
        // the page's picture: shown while the page, the track or a value on it changes, and a few seconds after
        const scope = input.scope ?? null;
        if (!scopeEquals(scope, this.stageScope)) {
            if (this.stageScope) this.lastEdit = now;
            this.stageScope = scope;
        }
        // the pack browser (MASTER page, BROWSE): shown while browsing and a few seconds after
        const pk = input.packs ?? {};
        this.packs = pk.names ?? [];
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
    rowRect(t) {
        const rowH = (this.H - 16) / 4;
        const x0 = t < 4 ? 8 : this.rightX + 8;
        const colW = (t < 4 ? this.mixerX : this.W - this.rightX) - 16;
        return new Rect(x0, 8 + (t % 4) * rowH, colW, rowH - 4);
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
        this.rows.forEach((r, t) => { if (t < 8) this._row(r, t + 1, this.rowRect(t)); });
        if (this.center) this._center(this.centerRect);
        g.save();
        this._stage(this.stageRect);
        g.restore();
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
            let fill;
            if (s === 1 || s === 3) fill = r.muted ? col(c, 0.3) : col(c);
            else if (s === 2) fill = col(c, 0.25);
            else fill = col(i % 4 === 0 ? S.s3 : S.s2);
            fillRect(g, cr, fill);
            if (s >= 2 && cell > 6) fillRect(g, new Rect(cr.midX - 1.5, cr.maxY - 5, 3, 3), S.fg);
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

    // MARK: - The stage

    /** One little figure per track, in its colour and its machine's shape, jumping when it plays, blinking when it does
     *  not; the floor is the 16 steps with the playhead. */
    _stage(r) {
        const g = this.g;
        if (r.h < 24 || this.rows.length !== 8) return;
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
            const row = this.rows[i];
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

    /** The active track's step pages as blocks, the one on the pads lit, the playhead running through them. */
    _pageMap(r) {
        const g = this.g;
        const [page, pages, t] = this.stepPageSeen;
        if (t >= this.rows.length || pages <= 0) return;
        const row = this.rows[t], c = row.color, n = row.steps.length;
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
                fillRect(g, new Rect(b.minX + i * cw + 1, b.midY - 4, cw - 2, 8), v === 1 || v === 3 ? col(c, on ? 1 : 0.5) : col(S.s3));
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
        let status;
        if (this.packReverted != null && now - this.packReverted < 2) status = "REVERTED: THE KIT BEFORE";
        else if (this.packLoaded && this.packLoaded.index === this.packCursor) status = "LOADED · SHIFT + PUSH: REVERT";
        else status = "PUSH: LOAD (THE BEAT STAYS)";
        const hint = `PACK ${this.packCursor + 1}/${n} · ` + status;
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
