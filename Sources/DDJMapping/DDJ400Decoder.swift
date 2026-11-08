// SPDX-License-Identifier: GPL-3.0-only
// The DDJ-400's MIDI in, decoded into DDJEvents. MIDI codes from Mixxx's verified mapping
// (reference/mixxx/Pioneer-DDJ-400.midi.xml and Pioneer-DDJ-400-script.js).

/// Turns raw DDJ-400 MIDI into `DDJEvent`s. Keeps the MSB/LSB of the 14-bit faders and knobs.
///
/// Tempo slider orientation, measured on a DDJ-400: fully towards the user (the "+" end) sends 16383, fully away 0.
/// The decoder does not flip anything: `.absolute` is always raw / 16383, so 1 is the "+" end (faster).
public struct DDJ400Decoder {
    /// last MSB / LSB seen, keyed by status << 8 | MSB controller number
    private var msb: [UInt16: UInt8] = [:]
    private var lsb: [UInt16: UInt8] = [:]

    public init() {}

    /// Forgets the 14-bit halves (on a reconnect; a new decoder does the same).
    public mutating func reset() {
        msb = [:]
        lsb = [:]
    }

    public mutating func decode(_ m: MIDIMessage) -> DDJEvent? {
        let kind = m.status & 0xF0
        let channel = m.status & 0x0F
        switch kind {
        case 0x80, 0x90:
            let down = kind == 0x90 && m.data2 > 0
            return decodeNote(channel: channel, note: m.data1, down: down)
        case 0xB0:
            return decodeCC(status: m.status, cc: m.data1, value: m.data2)
        default:
            return nil
        }
    }

    // MARK: notes

    private func decodeNote(channel: UInt8, note: UInt8, down: Bool) -> DDJEvent? {
        let press = DDJValue.press(down)
        switch channel {
        case 0, 1:
            let deck: Deck = channel == 0 ? .left : .right
            guard let (control, shifted) = Self.deckNote(note, deck) else { return nil }
            return DDJEvent(control, press, shifted: shifted)
        case 4:
            switch note {
            case 0x4A: return DDJEvent(.beatLeft, press)
            case 0x4B: return DDJEvent(.beatRight, press)
            case 0x63: return DDJEvent(.fxSelect, press)
            case 0x64: return DDJEvent(.fxSelect, press, shifted: true)
            case 0x47: return DDJEvent(.fxOnOff, press)
            case 0x43: return DDJEvent(.fxOnOff, press, shifted: true)
            // the beat FX channel switch (Mixxx: 0x10 CH1, 0x11 CH2, 0x14 MASTER): a note-on when moved there
            case 0x10: return down ? DDJEvent(.fxChannel(0), press) : nil
            case 0x11: return down ? DDJEvent(.fxChannel(1), press) : nil
            case 0x14: return down ? DDJEvent(.fxChannel(2), press) : nil
            default: return nil
            }
        case 6:
            switch note {
            case 0x41: return DDJEvent(.browsePress, press)
            case 0x42: return DDJEvent(.browsePress, press, shifted: true)
            case 0x46: return DDJEvent(.load(.left), press)
            case 0x47: return DDJEvent(.load(.right), press)
            default: return nil
            }
        case 7, 8, 9, 10:
            let deck: Deck = channel <= 8 ? .left : .right
            let shifted = channel == 8 || channel == 10
            guard let mode = PadMode.allCases.first(where: { $0.noteOffset == note & 0xF0 }) else { return nil }
            let pad = Int(note & 0x0F)
            guard pad <= 7 else { return nil }
            return DDJEvent(.pad(deck, pad), press, padMode: mode, shifted: shifted)
        default:
            return nil
        }
    }

    private static func deckNote(_ note: UInt8, _ d: Deck) -> (DDJControl, Bool)? {
        switch note {
        case 0x0B: return (.play(d), false)
        case 0x47: return (.play(d), true)
        case 0x0C: return (.cue(d), false)
        case 0x48: return (.cue(d), true)
        case 0x58: return (.sync(d), false)
        case 0x60: return (.sync(d), true)
        case 0x5C: return (.sync(d), false)   // BEAT SYNC long press: the same button (its release must not be lost)
        case 0x10: return (.loopIn(d), false)
        case 0x4C: return (.loopIn(d), true)
        case 0x11: return (.loopOut(d), false)
        case 0x4E: return (.loopOut(d), true)
        case 0x4D: return (.reloop(d), false)
        case 0x50: return (.reloop(d), true)
        case 0x51: return (.callLeft(d), false)
        case 0x3E: return (.callLeft(d), true)
        case 0x53: return (.callRight(d), false)
        case 0x3D: return (.callRight(d), true)
        case 0x54: return (.headphoneCue(d), false)
        case 0x68: return (.headphoneCue(d), true)   // SHIFT + mixer CUE ("Shift + Left/Right CUE" in Mixxx)
        case 0x36: return (.jogTouch(d), false)
        case 0x67: return (.jogTouch(d), true)
        case 0x3F: return (.shift(d), false)
        default: return nil
        }
    }

    // MARK: control changes

    /// the MSB controller numbers of the 14-bit controls (LSB = MSB + 0x20)
    private static func absoluteControl(status: UInt8, msb cc: UInt8) -> DDJControl? {
        switch status {
        case 0xB0, 0xB1:
            let d: Deck = status == 0xB0 ? .left : .right
            switch cc {
            case 0x13: return .channelFader(d)
            case 0x00: return .tempo(d)
            case 0x04: return .trim(d)
            case 0x07: return .eqHi(d)
            case 0x0B: return .eqMid(d)
            case 0x0F: return .eqLow(d)
            default: return nil
            }
        case 0xB6:
            switch cc {
            case 0x1F: return .crossfader
            case 0x17: return .cfx(.left)
            case 0x18: return .cfx(.right)
            case 0x0D: return .headphoneLevel
            default: return nil
            }
        case 0xB4:
            return cc == 0x02 ? .fxLevel : nil
        default:
            return nil
        }
    }

    private mutating func decodeCC(status: UInt8, cc: UInt8, value: UInt8) -> DDJEvent? {
        // jogs: centre 64, < 64 backward, > 64 forward
        if status == 0xB0 || status == 0xB1 {
            let d: Deck = status == 0xB0 ? .left : .right
            switch cc {
            case 0x21, 0x22, 0x23: return DDJEvent(.jog(d), .relative(Int(value) - 64))
            case 0x29: return DDJEvent(.jog(d), .relative(Int(value) - 64), shifted: true)
            default: break
            }
        }
        // BROWSE: two's complement, 1...63 clockwise, 65...127 counter-clockwise
        if status == 0xB6 && cc == 0x40 {
            let ticks = value < 64 ? Int(value) : Int(value) - 128
            return ticks == 0 ? nil : DDJEvent(.browse, .relative(ticks))
        }
        if let control = Self.absoluteControl(status: status, msb: cc) {
            let key = UInt16(status) << 8 | UInt16(cc)
            msb[key] = value
            // The DDJ-400 sends MSB then LSB. Once a control has shown it sends its LSB, wait for it: the new MSB with the
            // old LSB would jump back for a moment (seen on the hardware: 0.0703, 0.0644, 0.0684 on a crossfader moving
            // one way), and the knobs turned into encoder steps would wobble.
            if lsb[key] != nil { return nil }
            let full = Int(value) << 7
            return DDJEvent(control, .absolute(Double(full) / 16383))
        }
        if cc >= 0x20, let control = Self.absoluteControl(status: status, msb: cc - 0x20) {
            let key = UInt16(status) << 8 | UInt16(cc - 0x20)
            lsb[key] = value
            guard let high = msb[key] else { return nil }
            let full = Int(high) << 7 | Int(value)
            return DDJEvent(control, .absolute(Double(full) / 16383))
        }
        return nil
    }
}
