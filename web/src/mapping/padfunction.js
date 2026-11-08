// SPDX-License-Identifier: GPL-3.0-only
// What each DDJ-400 pad does in each pad mode for an FM-1 cartridge (a port of Sources/DDJMapping/DDJPadFunction.swift,
// with the FM1Keyboard helpers it needs from DDJTypes.swift). A pad function is { type: "none" },
// { type: "key", key }, { type: "punch", key } or { type: "selectTrack", track }.

import { midiNoteName } from "../vocab.js";

/** The FM-1 keyboard: 27 keys, F3 (key 0) to G5 (key 26), chromatic. 16 white keys, 11 black keys. */
export const FM1Keyboard = (() => {
    const keyCount = 27;
    const blackInOctave = [false, true, false, true, false, true, false, false, true, false, true, false];
    const isBlack = (k) => blackInOctave[((k % 12) + 12) % 12];
    const all = Array.from({ length: keyCount }, (_, k) => k);
    const whiteKeys = all.filter((k) => !isBlack(k));     // key of white key 0...15
    const blackKeys = all.filter((k) => isBlack(k));      // key of black key 0...10
    /** the black key just above white key w (between w and w + 1), null after B and E and after the last key */
    const blackAbove = (w) => {
        if (w < 0 || w >= whiteKeys.length) return null;
        const k = whiteKeys[w] + 1;
        return k < keyCount && isBlack(k) ? k : null;
    };
    const noteName = (key) => midiNoteName(53 + key);  // F3 = MIDI 53
    return Object.freeze({ keyCount, isBlack, whiteKeys, blackKeys, blackAbove, noteName, midiNoteName });
})();

const NONE = Object.freeze({ type: "none" });

/** The two decks as one 2 x 8 grid: bottom row white keys, top row the black key above each (none above B, E). */
function piano(firstWhite, deck, pad) {
    const w = firstWhite + (deck === 0 ? 0 : 4) + (pad % 4);
    if (pad >= 4) return { type: "key", key: FM1Keyboard.whiteKeys[w] };
    const k = FM1Keyboard.blackAbove(w);
    return k === null ? NONE : { type: "key", key: k };
}

export const DDJPadFunction = Object.freeze({
    none: NONE,

    /** The key played, if any (null otherwise). */
    key(f) {
        return f.type === "key" || f.type === "punch" ? f.key : null;
    },

    /** The pad `pad` (0...7: top row 0-3, bottom row 4-7) of `deck` in `mode`, with or without SHIFT
     *  (SHIFT + a pad: its key with FX held, a punch-in effect). */
    of(mode, deck, pad, shifted = false) {
        if (!(pad >= 0 && pad < 8)) return NONE;
        const white = FM1Keyboard.whiteKeys, black = FM1Keyboard.blackKeys;
        let base;
        switch (mode) {
        case "hotCue": case "padFX1":
            base = { type: "key", key: white[deck === 0 ? pad : 8 + pad] };
            break;
        case "beatLoop": case "keyboard":
            base = piano(0, deck, pad);
            break;
        case "beatJump": case "keyShift":
            base = piano(8, deck, pad);
            break;
        case "sampler": case "padFX2":
            if (deck === 0) base = { type: "key", key: black[pad] };
            else if (pad < 3) base = { type: "key", key: black[8 + pad] };
            else if (pad >= 4 && mode === "sampler") base = { type: "selectTrack", track: pad - 4 };
            else base = NONE;
            break;
        default:
            return NONE;
        }
        const punchMode = mode === "padFX1" || mode === "padFX2";
        if (base.type === "key" && (punchMode || shifted)) return { type: "punch", key: base.key };
        return base;
    },
});
