// SPDX-License-Identifier: GPL-3.0-only
// Shared vocabulary: the DDJ-400's physical controls, the events they produce, and the groovebox actions
// (the FM-1 panel that SLOOP reads) they map to. Pure value types, no CoreMIDI.

import Foundation   // String(format:)

/// One short MIDI message (status, data1, data2).
public struct MIDIMessage: Hashable, Sendable, CustomStringConvertible {
    public var status: UInt8
    public var data1: UInt8
    public var data2: UInt8
    public init(_ status: UInt8, _ data1: UInt8, _ data2: UInt8) {
        self.status = status
        self.data1 = data1
        self.data2 = data2
    }
    public var description: String {
        String(format: "%02X %02X %02X", status, data1, data2)
    }
}

public enum Deck: Int, CaseIterable, Hashable, Sendable {
    case left = 0, right = 1
}

/// The DDJ-400's pad modes. The pads send a different note range in each (Pioneer's MIDI spec, Mixxx's mapping):
/// HOT CUE 0x00, BEAT LOOP 0x60, BEAT JUMP 0x20, SAMPLER 0x30; with SHIFT on the mode button: KEYBOARD 0x40,
/// PAD FX1 0x10, PAD FX2 0x50, KEY SHIFT 0x70.
public enum PadMode: Int, CaseIterable, Hashable, Sendable {
    case hotCue, beatLoop, beatJump, sampler, keyboard, padFX1, padFX2, keyShift

    public var noteOffset: UInt8 {
        switch self {
        case .hotCue: return 0x00
        case .beatLoop: return 0x60
        case .beatJump: return 0x20
        case .sampler: return 0x30
        case .keyboard: return 0x40
        case .padFX1: return 0x10
        case .padFX2: return 0x50
        case .keyShift: return 0x70
        }
    }

    /// The label printed on the DDJ-400.
    public var ddjLabel: String {
        switch self {
        case .hotCue: return "HOT CUE"
        case .beatLoop: return "BEAT LOOP"
        case .beatJump: return "BEAT JUMP"
        case .sampler: return "SAMPLER"
        case .keyboard: return "KEYBOARD"
        case .padFX1: return "PAD FX1"
        case .padFX2: return "PAD FX2"
        case .keyShift: return "KEY SHIFT"
        }
    }
}

/// Every physical control of the DDJ-400 the app reads or lights.
public enum DDJControl: Hashable, Sendable {
    // deck transport and loop section
    case play(Deck), cue(Deck), sync(Deck), shift(Deck)
    case loopIn(Deck), loopOut(Deck), reloop(Deck), callLeft(Deck), callRight(Deck)
    /// pad 0...7: top row 0-3 left to right, bottom row 4-7 (the DDJ's PAD 1..8)
    case pad(Deck, Int)
    case padModeButton(Deck, PadMode)
    case jog(Deck), jogTouch(Deck)
    case tempo(Deck)
    // mixer
    case channelFader(Deck), trim(Deck), eqHi(Deck), eqMid(Deck), eqLow(Deck), cfx(Deck)
    case headphoneCue(Deck), load(Deck)
    case crossfader, browse, browsePress
    // beat FX section
    case beatLeft, beatRight, fxSelect, fxOnOff, fxLevel
    /// the beat FX channel switch: 0 = CH1, 1 = CH2, 2 = MASTER (a position, sent as a press when moved there)
    case fxChannel(Int)
    case headphoneLevel
}

/// What a control did.
public enum DDJValue: Hashable, Sendable {
    /// a button or pad: down / up
    case press(Bool)
    /// a fader or knob: its position, 0...1 (14-bit when the DDJ sends both bytes)
    case absolute(Double)
    /// an endless control (jog, BROWSE): signed ticks, + = clockwise
    case relative(Int)
}

public struct DDJEvent: Hashable, Sendable {
    public var control: DDJControl
    public var value: DDJValue
    /// pads only: the mode the pad was played in (from the note range the DDJ sent)
    public var padMode: PadMode?
    /// the DDJ's SHIFT was held (it sends shifted controls on other notes / channels)
    public var shifted: Bool
    public init(_ control: DDJControl, _ value: DDJValue, padMode: PadMode? = nil, shifted: Bool = false) {
        self.control = control
        self.value = value
        self.padMode = padMode
        self.shifted = shifted
    }
}

/// The FM-1's buttons, in SloopCore's order (sloop_button_t).
public enum FM1Button: Int, CaseIterable, Hashable, Sendable {
    case fx = 0, sel, env, lfo, edit, glo, home, save, arp, seq, play, rec, octDown, octUp

    public var label: String {
        ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"][rawValue]
    }
}

/// The FM-1's encoders, in SloopCore's order (sloop_encoder_t).
public enum FM1Encoder: Int, CaseIterable, Hashable, Sendable {
    case select = 0, algorithm, presets, k1, k2, k3, k4

    public var label: String {
        ["SELECT", "ALGORITHM", "PRESETS", "KNOB 1", "KNOB 2", "KNOB 3", "KNOB 4"][rawValue]
    }
}

/// The FM-1 keyboard: 27 keys, F3 (key 0) to G5 (key 26), chromatic. 16 white keys, 11 black keys.
public enum FM1Keyboard {
    public static let keyCount = 27
    private static let blackInOctave: [Bool] = [false, true, false, true, false, true, false, false, true, false, true, false]
    public static func isBlack(_ key: Int) -> Bool { blackInOctave[((key % 12) + 12) % 12] }
    /// key of white key 0...15
    public static let whiteKeys: [Int] = (0..<keyCount).filter { !isBlack($0) }
    /// key of black key 0...10
    public static let blackKeys: [Int] = (0..<keyCount).filter { isBlack($0) }
    /// the black key just above white key w (between w and w + 1), nil after B and E and after the last key
    public static func blackAbove(white w: Int) -> Int? {
        guard w >= 0, w < whiteKeys.count else { return nil }
        let k = whiteKeys[w] + 1
        return k < keyCount && isBlack(k) ? k : nil
    }
    public static func noteName(_ key: Int) -> String { midiNoteName(53 + key) }   // F3 = MIDI 53

    /// "C4" for MIDI 60, any MIDI note (0 = C-1)
    public static func midiNoteName(_ midi: Int) -> String {
        let names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
        let m = max(0, min(127, midi))
        return names[m % 12] + String(m / 12 - 1)
    }
}

/// What the groovebox is asked to do. Applied in order by the app (SloopCore).
public enum GrooveAction: Hashable, Sendable {
    case key(Int, Bool)                     // FM-1 key 0...26, down / up
    case button(FM1Button, Bool)
    case encoder(FM1Encoder, Int)           // detents, + = clockwise
    /// what came before is applied before what comes after (SloopCore sloop_input_barrier)
    case barrier
    case trackLevel(track: Int, value: Int) // 0...127; track 3 is the drums
    case masterFilter(Int)                  // -64 (low-pass) ... 0 (off) ... 63 (high-pass)
    case bpm(Int)                           // 40...240
    case swing(Int)                         // 0...100
    case masterVolume(Int)                  // 0...127
    /// select track 0...3 directly (SLOOP's track_select, as ALGORITHM does: no detents to count)
    case selectTrack(Int)
}

/// What the groovebox shows, read back from SloopCore for LEDs and soft takeover.
public struct GrooveState: Hashable, Sendable {
    public var keysLit: UInt32 = 0          // bit k: key k lit
    public var keysDim: UInt32 = 0          // bit k: key k glowing (a guide)
    public var buttonsLit: UInt32 = 0       // bit b: FM1Button(rawValue: b) lit
    public var selectedTrack: Int = 0       // 0...3 (3: drums)
    public var playing = false
    public var recording = false
    public var bpm = 90
    public var swing = 0
    public var step = -1
    public var trackLevel: [Int] = [104, 104, 104, 100]
    public var masterFilter = 0
    public var peakLeft: Double = 0         // 0...1
    public var peakRight: Double = 0
    public init() {}

    public func keyLit(_ k: Int) -> Bool { (keysLit >> UInt32(k)) & 1 == 1 }
    public func keyDim(_ k: Int) -> Bool { (keysDim >> UInt32(k)) & 1 == 1 }
    public func buttonLit(_ b: FM1Button) -> Bool { (buttonsLit >> UInt32(b.rawValue)) & 1 == 1 }
}

/// A light on the DDJ (or the on-screen DDJ): off, glowing, lit.
public enum LightLevel: Int, Hashable, Sendable, Comparable {
    case off = 0, dim = 1, on = 2
    public static func < (a: LightLevel, b: LightLevel) -> Bool { a.rawValue < b.rawValue }
}
