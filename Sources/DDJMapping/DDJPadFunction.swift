// SPDX-License-Identifier: GPL-3.0-only
// What each DDJ-400 pad does in each pad mode (docs/MAPPING.md, "Pads").

/// The job of one pad.
public enum DDJPadFunction: Hashable, Sendable {
    /// unused
    case none
    /// plays FM-1 key 0...26
    case key(Int)
    /// plays FM-1 key 0...26 with FX held (a punch-in effect)
    case punch(Int)
    /// selects track 0...3 (3: drums)
    case selectTrack(Int)

    /// The key played, if any.
    public var key: Int? {
        switch self {
        case .key(let k), .punch(let k): return k
        default: return nil
        }
    }

    /// The pad `pad` (0...7: top row 0-3, bottom row 4-7) of `deck` in `mode`, with or without SHIFT
    /// (SHIFT + a pad: its key with FX held, a punch-in effect).
    public static func of(mode: PadMode, deck: Deck, pad: Int, shifted: Bool = false) -> DDJPadFunction {
        guard (0..<8).contains(pad) else { return .none }
        let white = FM1Keyboard.whiteKeys
        let black = FM1Keyboard.blackKeys
        let base: DDJPadFunction
        switch mode {
        case .hotCue, .padFX1:
            base = .key(white[deck == .left ? pad : 8 + pad])
        case .beatLoop, .keyboard:
            base = piano(firstWhite: 0, deck: deck, pad: pad)
        case .beatJump, .keyShift:
            base = piano(firstWhite: 8, deck: deck, pad: pad)
        case .sampler, .padFX2:
            if deck == .left {
                base = .key(black[pad])
            } else if pad < 3 {
                base = .key(black[8 + pad])
            } else if pad >= 4 && mode == .sampler {
                base = .selectTrack(pad - 4)
            } else {
                base = .none
            }
        }
        let punchMode = mode == .padFX1 || mode == .padFX2
        if case .key(let k) = base, punchMode || shifted { return .punch(k) }
        return base
    }

    /// The two decks as one 2 x 8 grid: bottom row white keys, top row the black key above each (none above B, E).
    private static func piano(firstWhite: Int, deck: Deck, pad: Int) -> DDJPadFunction {
        let w = firstWhite + (deck == .left ? 0 : 4) + pad % 4
        if pad >= 4 { return .key(FM1Keyboard.whiteKeys[w]) }
        return FM1Keyboard.blackAbove(white: w).map { .key($0) } ?? .none
    }
}
