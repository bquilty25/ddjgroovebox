// SPDX-License-Identifier: GPL-3.0-only
// A picture of what the page being edited does to the active track's sound (Swift's PageScope): the wave of the
// machine (SRC), the filter's response (FLTR), the envelope (AMP), the sends (FX), the LFO's wave (LFO), the delay's
// echoes and the reverb (MASTER), the trig (TRIG). Drawn from the parameters' values, live as knobs turn.
//
// A scope is a plain object (built by the app from the engine, as Swift's PageScope.read does):
//   { page: index into TRIG_PAGES, machine: index into MACHINE, track: 0..7, color: ARGB,
//     raw: 8 ints (the page's values), normalised: 8 numbers 0..1 (by their ranges; 0 without a range),
//     shown: 8 strings (the values as the knobs show them; "" without a range) }

import { PAGE, MACHINE, isMelodic } from "../vocab.js";
import { S, col, drawTextTop, textWidth, fillRect, Rect } from "./style.js";

/** whether two scopes draw the same picture */
export function scopeEquals(a, b) {
    if (!a || !b) return a === b;
    if (a.page !== b.page || a.machine !== b.machine || a.track !== b.track || a.color !== b.color) return false;
    for (let i = 0; i < 8; i++) {
        if (a.raw[i] !== b.raw[i] || a.normalised[i] !== b.normalised[i] || a.shown[i] !== b.shown[i]) return false;
    }
    return true;
}

/** a scope from a model like vocab.js's TrigModel (info, globalInfo, value / liveValue / globalValue, format, machine) */
export function readScope(model, page, track, color) {
    const raw = [], normalised = [], shown = [];
    for (let slot = 0; slot < 8; slot++) {
        const info = page === PAGE.master ? model.globalInfo(slot) : model.info(track, page * 8 + slot);
        const v = page === PAGE.master ? model.globalValue(slot) : model.liveValue(track, page * 8 + slot);
        raw.push(v);
        if (info && info.max > info.min) {
            normalised.push((v - info.min) / (info.max - info.min));
            shown.push(model.format(info, v));
        } else { normalised.push(0); shown.push(""); }
    }
    return { page, machine: model.machine(track), track, color, raw, normalised, shown };
}

/** xorshift32, as the Swift code's UInt32 noise */
function xorshift(seed) {
    let rng = seed >>> 0;
    return () => {
        rng = (rng ^ (rng << 13)) >>> 0;
        rng = (rng ^ (rng >>> 17)) >>> 0;
        rng = (rng ^ (rng << 5)) >>> 0;
        return rng;
    };
}

const polyline = (g, pts) => { g.beginPath(); pts.forEach(([x, y], i) => (i ? g.lineTo(x, y) : g.moveTo(x, y))); };

/** Draws a scope into r. since: seconds since the track last played (a line runs along time-based pictures), or null. */
export function drawScope(g, sc, r, since = null) {
    const plot = r.inset(6, 4);
    g.strokeStyle = col(S.s3); g.lineWidth = 1;
    g.beginPath(); g.moveTo(plot.minX, plot.maxY); g.lineTo(plot.maxX, plot.maxY); g.stroke();
    const c = sc.color;
    switch (sc.page) {
    case PAGE.src: drawSource(g, sc, plot, c, since); break;
    case PAGE.fltr: drawFilter(g, sc, plot, c); break;
    case PAGE.amp: drawEnvelope(g, sc, plot, c, since); break;
    case PAGE.fx: drawSends(g, sc, plot, c); break;
    case PAGE.lfo: drawLFO(g, sc, plot, c); break;
    case PAGE.master: drawMaster(g, sc, plot, c); break;
    default: drawTrig(g, sc, plot, c); break;
    }
    // knobs that cannot be heard as the page is set, and why (top left, over the picture)
    scopeHints(sc).forEach((line, i) => drawTextTop(g, line, plot.minX, plot.minY + i * 14, 11, S.fg2));
}

/** Why some of the page's knobs do nothing right now (PageScope.hints says the same). */
export function scopeHints(sc) {
    const raw = sc.raw;
    if (sc.page === PAGE.fltr) {
        const cut = raw[0], type = raw[2], env = raw[3], key = raw[6];
        const open = (type === 0 && cut >= 124) || (type === 2 && cut <= 3);   // LP all the way up, HP all the way down
        const out = [];
        if (open && env === 0) out.push("FILTER WIDE OPEN: TURN CUTOFF TO HEAR RESO");
        if (open && ((type === 0 && env > 0) || (type === 2 && env < 0))) out.push("ENV OPENS IT FURTHER: NOTHING TO HEAR");
        if (env === 0) out.push("ENV AT 0: ATTACK AND DECAY WAIT FOR ENV");
        if (key !== 0 && !isMelodic(sc.machine)) out.push("KEYTRK: DRUMS PLAY ONE NOTE (LOCK NOTES)");
        return out;
    }
    if (sc.page === PAGE.lfo) {
        if (raw[3] === 0) return ["DEST OFF: THE LFO MOVES NOTHING"];
        if (raw[4] === 0) return ["DEPTH AT 0: THE LFO MOVES NOTHING"];
    }
    return [];
}

// SRC: the machine's wave
function drawSource(g, sc, r, c, since) {
    const N = 360, n = sc.normalised, raw = sc.raw, m = sc.machine;
    const ys = [];
    const next = xorshift(0x12345678);
    const noise = () => next() / 0xFFFFFFFF * 2 - 1;
    let phase = 0;
    for (let i = 0; i < N; i++) {
        const x = i / (N - 1);
        let y = 0;
        switch (m) {
        case MACHINE.kick: case MACHINE.tom: {
            const tune = raw[0] / 24, decay = 0.08 + n[1] * 0.9, sweep = n[2], swt = 0.02 + n[3] * 0.3;
            const f = (m === MACHINE.kick ? 5 : 9) * Math.pow(2, tune) * (1 + sweep * 5 * Math.exp(-x / swt));
            phase += f / N * 2 * Math.PI;
            y = Math.sin(phase) * Math.exp(-x / decay);
            if (m === MACHINE.kick && x < 0.015) y += n[4] * (1 - x / 0.015);
            y = Math.tanh(y * (1 + n[5] * 4)) / Math.tanh(1 + n[5] * 4);
            break;
        }
        case MACHINE.snare: {
            const body = Math.sin(i * 0.35 * Math.pow(2, raw[0] / 24)) * Math.exp(-x / (0.05 + n[1] * 0.5)) * (0.3 + n[5] * 0.7);
            y = body + noise() * n[2] * Math.exp(-x / (0.04 + n[3] * 0.5));
            break;
        }
        case MACHINE.hat: {
            const len = 0.02 + n[1] * 0.2 + n[2] * 0.6;
            const busy = noise() * (0.4 + n[4] * 0.6) + (i % 2 === 0 ? 0.2 : -0.2) * n[3];
            y = busy * Math.exp(-x / len);
            break;
        }
        case MACHINE.clap: {
            const bursts = Math.max(1, raw[3]), gap = 0.015 + n[2] * 0.05;
            const lastHit = (bursts - 1) * gap;
            let e = 0;
            if (x <= lastHit + gap) e = Math.exp(-(x % gap) / (gap * 0.3));
            if (x > lastHit) e = Math.max(e, Math.exp(-(x - lastHit) / (0.05 + n[1] * 0.5)) + n[5] * 0.25 * Math.exp(-(x - lastHit) / 0.6));
            y = noise() * Math.min(1, e);
            break;
        }
        case MACHINE.synth: {
            const p = (x * 2) % 1;
            y = morph(p, n[1]) * (1 - n[4] * 0.4) + Math.sin(x * Math.PI * 2) * n[4] * 0.6;
            y += noise() * n[5] * 0.3;
            break;
        }
        case MACHINE.fm: {
            const ratios = [0.5, 1, 1.5, 2, 2.5, 3, 3.5, 4, 4.5, 5, 6, 7, 8, 9, 11, 14];
            const ratio = ratios[Math.max(0, Math.min(15, raw[1]))];
            const idx = n[2] * 6 * (1 - n[5] + n[5] * Math.exp(-x / (0.05 + n[4] * 0.6)));
            y = Math.sin(2 * Math.PI * 3 * x + idx * Math.sin(2 * Math.PI * 3 * ratio * x));
            break;
        }
        default: {   // SAMPLE: the slot's span, START to END (dimmed outside)
            const inSpan = x >= n[2] && x <= Math.max(n[2], n[3]);
            y = noise() * (0.3 + 0.6 * Math.abs(Math.sin(x * 9))) * (inSpan ? 1 : 0.25);
        }
        }
        ys.push(Math.max(-1, Math.min(1, y)));
    }
    plotWave(g, r, ys, c);
    if (m === MACHINE.sample) {
        g.strokeStyle = col(c); g.lineWidth = 1.5;
        g.beginPath();
        for (const v of [n[2], n[3]]) { const x = r.minX + v * r.w; g.moveTo(x, r.minY); g.lineTo(x, r.maxY); }
        g.stroke();
    }
    if (since != null && m !== MACHINE.synth) runDot(g, r, since / 0.8);
}

/** sine (0) -> triangle -> saw -> square (1), one cycle, p in 0..<1 */
function morph(p, shape) {
    const sine = Math.sin(2 * Math.PI * p);
    const tri = p < 0.25 ? p * 4 : p < 0.75 ? 2 - p * 4 : p * 4 - 4;
    const saw = p < 0.5 ? p * 2 : p * 2 - 2;
    const sq = p < 0.5 ? 1 : -1;
    const waves = [sine, tri, saw, sq];
    const s = shape * 3, i = Math.min(2, Math.trunc(s)), f = s - i;
    return waves[i] * (1 - f) + waves[i + 1] * f;
}

function plotWave(g, r, ys, c) {
    const mid = r.midY, half = r.h / 2 - 2;
    g.strokeStyle = col(S.s3); g.lineWidth = 1;
    g.beginPath(); g.moveTo(r.minX, mid); g.lineTo(r.maxX, mid); g.stroke();
    g.strokeStyle = col(c); g.lineWidth = 1.5;
    polyline(g, ys.map((y, i) => [r.minX + i / (ys.length - 1) * r.w, mid - y * half]));
    g.stroke();
}

function runDot(g, r, x) {
    if (x < 0 || x > 1) return;
    fillRect(g, new Rect(r.minX + x * r.w - 1, r.minY, 2, r.h), col(S.fg, 0.8));
}

// FLTR: the response, cutoff and resonance; the envelope's reach as a ghost
function drawFilter(g, sc, r, c) {
    const n = sc.normalised, raw = sc.raw;
    const type = Math.max(0, Math.min(2, raw[2]));
    const q = 0.5 + n[1] * n[1] * 12;
    const curve = (cut) => {
        const pts = [];
        for (let i = 0; i <= 200; i++) {
            const x = i / 200;
            const ratio = Math.pow(1000, x - cut);
            const d = Math.sqrt(Math.pow(1 - ratio * ratio, 2) + Math.pow(ratio / q, 2));
            const mag = type === 0 ? 1 / d : type === 2 ? ratio * ratio / d : (ratio / q) / d;
            const db = Math.max(-36, Math.min(18, 20 * Math.log10(Math.max(1e-6, mag))));
            pts.push([r.minX + x * r.w, r.minY + (18 - db) / 54 * r.h]);
        }
        return pts;
    };
    const env = raw[3] / 64;
    if (Math.abs(env) > 0.02) {
        g.strokeStyle = col(c, 0.3); g.lineWidth = 1.2;
        polyline(g, curve(Math.max(0, Math.min(1, n[0] + env * 0.5)))); g.stroke();
    }
    g.strokeStyle = col(S.s3); g.lineWidth = 1;
    const zero = r.minY + 18 / 54 * r.h;
    g.beginPath(); g.moveTo(r.minX, zero); g.lineTo(r.maxX, zero); g.stroke();
    const pts = curve(n[0]);
    g.fillStyle = col(c, 0.18);
    g.beginPath(); g.moveTo(r.minX, r.maxY);
    for (const [x, y] of pts) g.lineTo(x, y);
    g.lineTo(r.maxX, r.maxY); g.closePath(); g.fill();
    g.strokeStyle = col(c); g.lineWidth = 2;
    polyline(g, pts); g.stroke();
}

// AMP: attack, hold, decay, sustain, release
function drawEnvelope(g, sc, r, c, since) {
    const n = sc.normalised;
    const a = 0.01 + n[0] * n[0] * 0.5, h = n[1] * n[1] * 0.3, d = 0.02 + n[2] * n[2] * 0.6, s = n[3];
    const hold = 0.25, rel = 0.02 + n[4] * n[4] * 0.6;
    const total = a + h + d + hold + rel;
    const top = r.minY + r.h * (1 - (0.35 + 0.65 * n[6]));
    const px = (t) => r.minX + t / total * r.w;
    const py = (v) => r.maxY - v * (r.maxY - top);
    const pts = [[px(0), py(0)], [px(a), py(1)], [px(a + h), py(1)]];
    for (let k = 1; k <= 20; k++) { const t = k / 20; pts.push([px(a + h + d * t), py(s + (1 - s) * Math.exp(-t * 4) * (1 - t))]); }
    pts.push([px(a + h + d + hold), py(s)]);
    for (let k = 1; k <= 20; k++) { const t = k / 20; pts.push([px(a + h + d + hold + rel * t), py(s * Math.exp(-t * 4) * (1 - t))]); }
    g.fillStyle = col(c, 0.18);
    polyline(g, pts); g.lineTo(px(total), r.maxY); g.closePath(); g.fill();
    g.strokeStyle = col(c); g.lineWidth = 2;
    polyline(g, pts); g.stroke();
    // PAN: a dot on a short bar, top right
    const bar = new Rect(r.maxX - 60, r.minY + 2, 56, 2);
    fillRect(g, bar, S.s3);
    g.fillStyle = col(S.fg);
    g.beginPath(); g.arc(bar.minX + n[5] * bar.w, bar.midY, 3, 0, 2 * Math.PI); g.fill();
    if (since != null) runDot(g, r, since / 1.2);
}

// FX: the sends and the rest, as bars
function drawSends(g, sc, r, c) {
    const names = ["DLY", "REV", "CHO", "BIT", "SRR", "LCT", "DCK", "WID"];
    const w = r.w / 8;
    for (let i = 0; i < 8; i++) {
        const bh = (r.h - 14) * sc.normalised[i];
        const x = r.minX + i * w + w * 0.2;
        fillRect(g, new Rect(x, r.minY, w * 0.6, r.h - 14), S.s2);
        fillRect(g, new Rect(x, r.maxY - 14 - bh, w * 0.6, bh), col(c, i < 3 ? 1 : 0.6));
        drawTextTop(g, names[i], x + w * 0.3 - textWidth(names[i], 10) / 2, r.maxY - 12, 10, S.fg2);
    }
}

// LFO: its wave, speed and depth
function drawLFO(g, sc, r, c) {
    const n = sc.normalised, raw = sc.raw;
    const cycles = Math.max(0.5, Math.min(8, 0.5 + n[0] * 3 * (1 + raw[1] / 4)));
    const depth = raw[4] / 64, fade = raw[5] / 64, phase = n[7];
    const next = xorshift(0xBEEF);
    let held = 0, lastStep = -1;
    const ys = [];
    for (let i = 0; i <= 300; i++) {
        const x = i / 300;
        const p = (x * cycles + phase) % 1;
        let y;
        switch (raw[2]) {
        case 1: y = p < 0.5 ? p * 4 - 1 : 3 - p * 4; break;
        case 2: y = 1 - p * 2; break;
        case 3: y = p < 0.5 ? 1 : -1; break;
        case 4: {
            const st = Math.trunc(x * cycles * 2);
            if (st !== lastStep) { lastStep = st; held = next() / 0xFFFFFFFF * 2 - 1; }
            y = held;
            break;
        }
        default: y = Math.sin(2 * Math.PI * p);
        }
        const f = fade > 0 ? Math.min(1, x / Math.max(0.01, fade)) : fade < 0 ? Math.max(0, 1 - x / Math.max(0.01, -fade)) : 1;
        ys.push(y * depth * f);
    }
    plotWave(g, r, ys, c);
    const dest = "→ " + (sc.shown[3] ?? "");
    drawTextTop(g, dest, r.maxX, r.minY, 11, S.fg2, true);
}

// MASTER: the delay's echoes, the reverb's tail
function drawMaster(g, sc, r, c) {
    const n = sc.normalised, raw = sc.raw;
    const rl = 0.15 + n[3] * 0.8;
    g.fillStyle = col(0xFF7A5CFF, 0.15 + 0.2 * (1 - n[4]));
    g.beginPath(); g.moveTo(r.minX, r.maxY);
    for (let k = 0; k <= 30; k++) { const t = k / 30; g.lineTo(r.minX + t * rl * r.w, r.maxY - Math.exp(-t * 3) * r.h * 0.6); }
    g.lineTo(r.minX + rl * r.w, r.maxY); g.closePath(); g.fill();
    const gap = 0.04 + raw[0] / 11 * 0.25;
    let t = 0, lv = 1;
    g.fillStyle = col(c);
    while (t <= 1 && lv > 0.03) {
        const h = lv * r.h;
        g.fillRect(r.minX + t * r.w, r.maxY - h, 4, h);
        t += gap;
        lv *= 0.15 + n[1] * 0.83;
    }
}

// TRIG: the note
function drawTrig(g, sc, r, c) {
    // the note: a bar as long as LENGTH, as high as VELOCITY, moved by MICRO from the step's line; RETRIG splits it
    // into hits whose heights follow RTRG VEL (RTRG RATE in the caption); COND and the numbers in the caption
    const n = sc.normalised, raw = sc.raw, shown = sc.shown;
    const top = r.minY + 34, h = r.maxY - top;               // (room for the note's name and the caption)
    const start = r.minX + r.w * 0.12, shift = raw[7] / 23 * r.w * 0.1;
    fillRect(g, new Rect(start, top, 1, h), S.s3);            // the step's line
    const len = (0.05 + n[2] * 0.8) * r.w, vel = n[1];
    const hits = raw[3] + 1, ramp = raw[5] / 64;
    for (let k = 0; k < hits; k++) {
        const f = hits > 1 ? k / (hits - 1) : 0;
        const v = Math.max(0.04, Math.min(1, vel * (1 + ramp * f)));
        const w = len / hits;
        const b = new Rect(start + shift + k * w, r.maxY - v * h, Math.max(2, w - (hits > 1 ? 2 : 0)), v * h);
        fillRect(g, b, col(c, 0.35));
        fillRect(g, new Rect(b.minX, b.minY, b.w, 3), c);
    }
    drawTextTop(g, shown[0] ?? "", r.maxX, r.minY, 22, S.fg, true);
    const parts = ["VEL " + shown[1], "LEN " + shown[2]];
    if (raw[3] > 0) parts.push(`x${raw[3] + 1} ${shown[4]}`);
    if (raw[6] !== 0) parts.push(shown[6]);
    if (raw[7] !== 0) parts.push("MICRO " + shown[7]);
    drawTextTop(g, parts.join("  "), r.minX, r.minY + 6, 11, S.fg2);
}
