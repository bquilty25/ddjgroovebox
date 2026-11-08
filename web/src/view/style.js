// SPDX-License-Identifier: GPL-3.0-only
// SLOOP's look (Swift's SloopStyle): black, a few greys, the four track colours, red for REC, and Terminus Bold in
// capitals. Also the small drawing helpers the views share: rectangles, the font's metrics, text, boxes and discs.
// Colours are ARGB numbers, as in vocab.js; col(argb, alpha) gives the CSS colour.
//
// The font: the page loads SLOOPTerminus.ttf as the CSS family "Terminus", e.g.
//   @font-face { font-family: "Terminus"; src: url("../Sources/DDJGroovebox/Resources/SLOOPTerminus.ttf"); }
// Its metrics are fixed (1024 units per em: ascent 768, descent 256, every advance 512), so text is measured without
// the browser: a line is `size` high and a character `size / 2` wide.

import { css } from "../vocab.js";

export const S = {
    bg: 0xFF000000,
    s1: 0xFF0B0B0E,      // panels
    s2: 0xFF1A1A1E,      // controls
    s3: 0xFF36363C,      // raised / borders
    line: 0xFF232329,
    dim: 0xFF76767E,     // secondary labels
    fg2: 0xFFC4C4CC,
    fg: 0xFFF2F2F2,
    red: 0xFFFF2C34,     // REC
};
/** the four tracks of SLOOP: 1 blue, 2 green, 3 yellow, 4 (drums) orange */
export const TRACK4 = [0xFF287CFF, 0xFF1ECC70, 0xFFFFC618, 0xFFFF621A];

/** ARGB number (with an extra alpha factor) -> CSS colour */
export const col = (argb, alpha = 1) => css(argb, alpha);

export const FONT_FAMILY = '"Terminus", monospace';
const ASCENT = 0.75, DESCENT = 0.25, ADVANCE = 0.5;

/** the CSS font at a size (16 px is its native pixel size) */
export function font(size) {
    const k = Math.round(size * 64) / 64;
    return `${k}px ${FONT_FAMILY}`;
}
export const lineHeight = (size) => Math.ceil(size * (ASCENT + DESCENT));
export const charWidth = (size) => size * ADVANCE;
export const textWidth = (s, size) => [...s].length * size * ADVANCE;
export const maxChars = (width, size) => Math.max(1, Math.floor((width + 0.5) / charWidth(size)));

export function truncate(s, n) {
    const a = [...s];
    return a.length <= n ? s : a.slice(0, Math.max(0, n - 1)).join("") + "…";
}

/** greedy word wrap into at most maxLines lines of maxChars; the last line is truncated with an ellipsis */
export function wrap(s, maxCharsN, maxLines) {
    if (maxCharsN <= 0 || maxLines <= 0 || !s) return [];
    if ([...s].length <= maxCharsN) return [s];
    if (maxLines === 1) return [truncate(s, maxCharsN)];
    const lines = [];
    let cur = "";
    for (const word of s.split(" ").filter((w) => w.length > 0)) {
        if (!cur) cur = word;
        else if ([...cur].length + 1 + [...word].length <= maxCharsN) cur += " " + word;
        else { lines.push(cur); cur = word; }
        while ([...cur].length > maxCharsN) {
            const a = [...cur];
            lines.push(a.slice(0, maxCharsN).join(""));
            cur = a.slice(maxCharsN).join("");
        }
    }
    if (cur) lines.push(cur);
    if (lines.length > maxLines) {
        const kept = lines.slice(0, maxLines);
        kept[maxLines - 1] = truncate(lines.slice(maxLines - 1).join(" "), maxCharsN);
        return kept;
    }
    return lines;
}

/** perceived brightness of an ARGB colour, 0..1 */
export function luminance(argb) {
    const r = ((argb >>> 16) & 255) / 255, g = ((argb >>> 8) & 255) / 255, b = (argb & 255) / 255;
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}
/** black text on light colours, white on dark ones */
export const ink = (argb) => (luminance(argb) > 0.45 ? S.bg : S.fg);

/** A rectangle (CoreGraphics' CGRect, y down). */
export class Rect {
    constructor(x, y, w, h) { this.x = x; this.y = y; this.w = w; this.h = h; }
    get minX() { return this.x; } get maxX() { return this.x + this.w; } get midX() { return this.x + this.w / 2; }
    get minY() { return this.y; } get maxY() { return this.y + this.h; } get midY() { return this.y + this.h / 2; }
    get width() { return this.w; } get height() { return this.h; }
    inset(dx, dy) { return new Rect(this.x + dx, this.y + dy, this.w - 2 * dx, this.h - 2 * dy); }
    contains(px, py) { return px >= this.x && px < this.x + this.w && py >= this.y && py < this.y + this.h; }
    union(o) {
        const x = Math.min(this.x, o.x), y = Math.min(this.y, o.y);
        return new Rect(x, y, Math.max(this.maxX, o.maxX) - x, Math.max(this.maxY, o.maxY) - y);
    }
    static centered(x, y, w, h) { return new Rect(x - w / 2, y - h / 2, w, h); }
    static circle(x, y, r) { return new Rect(x - r, y - r, 2 * r, 2 * r); }
}

/**
 * One line of text with its vertical centre at y (Swift's drawText). align: "left" | "center" | "right".
 * shrink: scale it down to fit maxWidth (printed labels only); otherwise callers truncate first.
 */
export function drawText(g, s, size, color, x, y, align = "center", maxWidth = Infinity, shrink = false) {
    if (!s) return;
    let w = textWidth(s, size);
    if (shrink && w > maxWidth) { size = Math.max(5, size * maxWidth / w); w = textWidth(s, size); }
    const ox = align === "left" ? x : align === "right" ? x - w : x - w / 2;
    const h = size * (ASCENT + DESCENT);
    g.font = font(size);
    g.fillStyle = typeof color === "string" ? color : col(color);
    g.textBaseline = "alphabetic";
    g.textAlign = "left";
    g.fillText(s, ox, y - h / 2 + size * ASCENT);
}

/** text with its top-left (or top-right) corner at x, y (an NSAttributedString drawn at a point); returns its width */
export function drawTextTop(g, s, x, y, size, color, right = false) {
    const w = textWidth(s, size);
    g.font = font(size);
    g.fillStyle = typeof color === "string" ? color : col(color);
    g.textBaseline = "alphabetic";
    g.textAlign = "left";
    g.fillText(s, right ? x - w : x, y + size * ASCENT);
    return w;
}

/** a rounded box (radius 2), filled and / or stroked inside its edge */
export function box(g, r, fill, stroke, width = 1, dash = false, radius = 2) {
    if (fill != null) {
        g.beginPath();
        g.roundRect(r.x, r.y, r.w, r.h, radius);
        g.fillStyle = typeof fill === "string" ? fill : col(fill);
        g.fill();
    }
    if (stroke != null) {
        g.save();
        if (dash) g.setLineDash([2, 2]);
        g.beginPath();
        g.roundRect(r.x + width / 2, r.y + width / 2, r.w - width, r.h - width, radius);
        g.strokeStyle = typeof stroke === "string" ? stroke : col(stroke);
        g.lineWidth = width;
        g.stroke();
        g.restore();
    }
}

export function ellipsePath(g, r) {
    g.beginPath();
    g.ellipse(r.midX, r.midY, Math.max(0, r.w / 2), Math.max(0, r.h / 2), 0, 0, 2 * Math.PI);
}

/** a disc, filled and / or stroked inside its edge */
export function disc(g, r, fill, stroke, width = 1) {
    if (fill != null) {
        ellipsePath(g, r);
        g.fillStyle = typeof fill === "string" ? fill : col(fill);
        g.fill();
    }
    if (stroke != null) {
        ellipsePath(g, r.inset(width / 2, width / 2));
        g.strokeStyle = typeof stroke === "string" ? stroke : col(stroke);
        g.lineWidth = width;
        g.stroke();
    }
}

export function fillRect(g, r, color) {
    g.fillStyle = typeof color === "string" ? color : col(color);
    g.fillRect(r.x, r.y, r.w, r.h);
}

/** size a canvas to CSS w x h at the device's pixel ratio; returns the ratio */
export function sizeCanvas(canvas, w, h) {
    const dpr = window.devicePixelRatio || 1;
    canvas.style.width = `${w}px`;
    canvas.style.height = `${h}px`;
    const pw = Math.max(1, Math.round(w * dpr)), ph = Math.max(1, Math.round(h * dpr));
    if (canvas.width !== pw) canvas.width = pw;
    if (canvas.height !== ph) canvas.height = ph;
    return dpr;
}
