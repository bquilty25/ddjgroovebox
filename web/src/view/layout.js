// SPDX-License-Identifier: GPL-3.0-only
// The window's content (Swift's GrooveboxLayoutView): the strip directly on top of the drawn DDJ-400, the same width,
// one device. The drawing is stretched wider than the controller's shape when the window is wide (up to the design's
// widest), never narrower. Also the computer keyboard as a controller (KeyboardMap), so everything can be played with
// no DDJ-400 attached.
//
// new GrooveboxLayout(container): makes two canvases in the container (which should have a size; it is made
//   position: relative), a StripView (layout.strip) and a DDJView (layout.ddj), and lays them out on every resize.
//   layout.onEvent(cb): the drawing's mouse events and the keyboard's, as vocab events (returns an unsubscribe function).
//   layout.layout(): lay out again now. layout.releaseKeys(): let every key go (also on window blur).

import { C, ev, press, relative } from "../vocab.js";
import { DDJView, MIN_DESIGN_W, MAX_DESIGN_W, DESIGN_H } from "./ddjview.js";
import { StripView } from "./strip.js";

/**
 * The Mac keyboard laid out like the DDJ-400 (by key position, KeyboardEvent.code):
 *   1-8 and Q-I       the left and right deck's 8 pads (in each deck's pad mode)
 *   A S D F / J K L ; LOOP IN, LOOP OUT, RELOOP, BEAT SYNC of the left / right deck
 *   Z X / N M         CUE/LOOP CALL left, right of the left / right deck
 *   C / V             CUE left / right       space: PLAY        , .: LOAD left / right
 *   arrows / return   BROWSE turn / press    tab: BEAT FX ON/OFF   G H: headphone CUE 1 / 2
 *   F1-F4 / F5-F8     the left / right deck's pad mode (HOT CUE, BEAT LOOP, BEAT JUMP, SAMPLER)
 *   shift             SHIFT (left deck)
 */
export const KeyboardMap = {
    pads: {
        Digit1: [0, 0], Digit2: [0, 1], Digit3: [0, 2], Digit4: [0, 3], Digit5: [0, 4], Digit6: [0, 5], Digit7: [0, 6], Digit8: [0, 7],
        KeyQ: [1, 0], KeyW: [1, 1], KeyE: [1, 2], KeyR: [1, 3], KeyT: [1, 4], KeyY: [1, 5], KeyU: [1, 6], KeyI: [1, 7],
    },
    buttons: {
        KeyA: C.loopIn(0), KeyS: C.loopOut(0), KeyD: C.reloop(0), KeyF: C.sync(0),
        KeyJ: C.loopIn(1), KeyK: C.loopOut(1), KeyL: C.reloop(1), Semicolon: C.sync(1),
        KeyZ: C.callLeft(0), KeyX: C.callRight(0), KeyN: C.callLeft(1), KeyM: C.callRight(1),
        KeyC: C.cue(0), KeyV: C.cue(1), Space: C.play(0), Comma: C.load(0), Period: C.load(1),
        Enter: C.browsePress, Tab: C.fxOnOff, KeyG: C.headphoneCue(0), KeyH: C.headphoneCue(1),
    },
    modes: {
        F1: [0, "hotCue"], F2: [0, "beatLoop"], F3: [0, "beatJump"], F4: [0, "sampler"],
        F5: [1, "hotCue"], F6: [1, "beatLoop"], F7: [1, "beatJump"], F8: [1, "sampler"],
    },
    /** the event a key makes (a press, or BROWSE's turn), or null */
    event(code, shifted, padModes) {
        const p = this.pads[code];
        if (p) return ev(C.pad(p[0], p[1]), press(true), { padMode: padModes[p[0]] ?? "hotCue", shifted });
        const b = this.buttons[code];
        if (b) return ev(b, press(true), { shifted });
        const m = this.modes[code];
        if (m) return ev(C.padModeButton(m[0], m[1]), press(true), { shifted });
        if (code === "ArrowLeft") return ev(C.browse, relative(-1));
        if (code === "ArrowRight") return ev(C.browse, relative(1));
        return null;
    },
};

export class GrooveboxLayout {
    constructor(container, { margin = 12, keyboard = true } = {}) {
        this.container = container;
        this.margin = margin;
        if (getComputedStyle(container).position === "static") container.style.position = "relative";
        const make = () => {
            const c = document.createElement("canvas");
            c.style.position = "absolute";
            c.style.display = "block";
            container.appendChild(c);
            return c;
        };
        this.strip = new StripView(make());
        this.ddj = new DDJView(make());
        this._listeners = [];
        this.keysDown = new Map();
        this.shiftDown = false;
        this.ddj.onEvent((e) => this._emit(e));
        new ResizeObserver(() => this.layout()).observe(container);
        if (keyboard) {
            window.addEventListener("keydown", (e) => this._keyDown(e));
            window.addEventListener("keyup", (e) => this._keyUp(e));
            window.addEventListener("blur", () => this.releaseKeys());
        }
        this.layout();
    }

    onEvent(cb) { this._listeners.push(cb); return () => { this._listeners = this._listeners.filter((f) => f !== cb); }; }
    _emit(e) { for (const f of this._listeners) f(e); }

    /** the widest layout that fits: the strip asks for its height, the drawing takes the rest */
    layout() {
        const W = this.container.clientWidth, H = this.container.clientHeight;
        const availW = W - 2 * this.margin, availH = H - 2 * this.margin;
        const minAspect = MIN_DESIGN_W / DESIGN_H, maxAspect = MAX_DESIGN_W / DESIGN_H;
        let w = availW, dh = 0;
        while (w > 200) {
            const room = availH - this.strip.preferredHeight(w);
            if (room > 0 && w / room <= maxAspect) { dh = Math.floor(Math.min(room, w / minAspect)); break; }
            w -= 4;
        }
        if (w <= 200 && dh === 0) w = Math.max(200, availW);
        if (dh === 0) dh = Math.floor(w / minAspect);
        const sh = Math.round(this.strip.preferredHeight(w));
        this.ddj.setDesignWidth(DESIGN_H * w / dh);
        this.strip.columns = this.ddj.columnFractions;
        const x = Math.round((W - w) / 2);
        const y = Math.max(this.margin, Math.round((H - sh - dh) / 2));
        const place = (view, top, height) => {
            view.canvas.style.left = `${x}px`;
            view.canvas.style.top = `${top}px`;
            view.resize(w, height);
        };
        place(this.strip, y, sh);
        place(this.ddj, y + sh, dh);
    }

    // MARK: the computer keyboard

    _typing(e) {
        const t = e.target;
        return t && (t.isContentEditable || ["INPUT", "TEXTAREA", "SELECT"].includes(t.tagName));
    }

    _keyDown(e) {
        if (this._typing(e)) return;
        if (e.key === "Shift") {
            if (!this.shiftDown) { this.shiftDown = true; this._emit(ev(C.shift(0), press(true))); }
            return;
        }
        if (e.metaKey || e.ctrlKey || e.altKey) return;
        if (e.repeat) { if (KeyboardMap.event(e.code, false, this.ddj.padModes)) e.preventDefault(); return; }
        const event = KeyboardMap.event(e.code, this.shiftDown, this.ddj.padModes);
        if (!event) return;
        e.preventDefault();
        this.keysDown.set(e.code, event);
        this._emit(event);
    }

    _keyUp(e) {
        if (e.key === "Shift") {
            if (this.shiftDown) { this.shiftDown = false; this._emit(ev(C.shift(0), press(false))); }
            return;
        }
        const event = this.keysDown.get(e.code);
        if (!event) return;
        this.keysDown.delete(e.code);
        if ("press" in event.value) this._emit(ev(event.control, press(false), { padMode: event.padMode, shifted: event.shifted }));
    }

    /** let every key go (the window lost focus) */
    releaseKeys() {
        for (const event of this.keysDown.values()) {
            if ("press" in event.value) this._emit(ev(event.control, press(false), { padMode: event.padMode, shifted: event.shifted }));
        }
        this.keysDown.clear();
        if (this.shiftDown) { this.shiftDown = false; this._emit(ev(C.shift(0), press(false))); }
    }
}
