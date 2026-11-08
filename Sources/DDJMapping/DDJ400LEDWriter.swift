// SPDX-License-Identifier: GPL-3.0-only
// The DDJ-400's LEDs: MIDI out, diffed against what was last sent. LED codes from Mixxx's DDJ-400 mapping.

import Foundation   // log10, ProcessInfo

public struct DDJ400LEDWriter {
    /// last data2 sent, keyed by status << 8 | data1
    private var sent: [UInt16: UInt8] = [:]
    private var lastMeter: [Int?] = [nil, nil]
    /// the level each meter shows, dBFS (falls at `meterDecayDBPerSecond`)
    private var meterDB: [Double] = [-.infinity, -.infinity]
    private var meterTime: Double?

    /// the bottom of the meters: this level (dBFS) and below is dark, 0 dBFS is full (127)
    public var meterFloorDB = -48.0
    /// how fast a meter falls back after a peak
    public var meterDecayDBPerSecond = 25.0
    /// the clock, in seconds (injectable for tests)
    public var now: () -> Double = { ProcessInfo.processInfo.systemUptime }

    public init() {}

    /// Forgets what was sent: the next `messages` resends everything.
    public mutating func invalidate() {
        sent = [:]
        lastMeter = [nil, nil]
    }

    /// LED messages sent at most per call (the app calls ~60 times a second). A refresh of every LED is ~300 messages:
    /// sent at once, the controller can lose most of them, and the diff would never send them again. Over budget, the
    /// rest waits for the next call (it is still different from what was sent).
    public var maxPerCall = 24

    /// The LED messages that changed since the last call. `meters`: the output peaks, left and right, linear 0...1.
    /// Most visible first: the buttons, the pads of each deck's current mode, then the other modes' pads.
    public mutating func messages(mapper: some LEDSource, meters: (Double, Double)) -> [MIDIMessage] {
        var out: [MIDIMessage] = []
        let lights = mapper.lights()
        let current = Set(Deck.allCases.map { mapper.padMode($0) })
        let leds = Self.leds(padLights: { mapper.padLights(for: $0) }, button: { lights[$0] ?? .off })
        let ordered = leds.filter { $0.0 < 0x97 } +                                       // buttons, FX
            leds.filter { $0.0 >= 0x97 && Self.isMode($0.1, in: current) } +            // pads, current modes
            leds.filter { $0.0 >= 0x97 && !Self.isMode($0.1, in: current) }             // pads, the other modes
        for (s, n, v) in ordered where out.count < maxPerCall { put(s, n, v, into: &out) }

        let t = now()
        let dt = meterTime.map { max(0, t - $0) } ?? 0
        meterTime = t
        for (d, peak) in [meters.0, meters.1].enumerated() {
            let db = peak > 0 ? 20 * log10(min(1, peak)) : -Double.infinity
            meterDB[d] = db >= meterDB[d] ? db : max(db, meterDB[d] - meterDecayDBPerSecond * dt)
            let v = Self.meterValue(dB: meterDB[d], floor: meterFloorDB)
            if let last = lastMeter[d], last == v || (abs(v - last) < 2 && v != 0) { continue }
            lastMeter[d] = v
            out.append(MIDIMessage(UInt8(0xB0 + d), 0x02, UInt8(v)))
        }
        return out
    }

    static func isMode(_ note: UInt8, in modes: Set<PadMode>) -> Bool {
        modes.contains { note & 0xF8 == $0.noteOffset }
    }

    /// A meter's CC value 0...127 for a level in dBFS: `floor` and below 0, 0 dBFS 127, linear in dB between.
    static func meterValue(dB: Double, floor: Double) -> Int {
        guard dB.isFinite, dB > floor else { return 0 }
        return max(0, min(127, Int(((dB - floor) / -floor * 127).rounded())))
    }

    /// Every LED it manages off (on quit), the VU meters to 0.
    public mutating func allOff() -> [MIDIMessage] {
        var out: [MIDIMessage] = []
        let dark = Array(repeating: Array(repeating: LightLevel.off, count: 8), count: 2)
        for (s, n, _) in Self.leds(padLights: { _ in dark }, button: { _ in .off }) {
            sent[UInt16(s) << 8 | UInt16(n)] = 0
            out.append(MIDIMessage(s, n, 0))
        }
        for d in 0..<2 {
            lastMeter[d] = 0
            meterDB[d] = -.infinity
            out.append(MIDIMessage(UInt8(0xB0 + d), 0x02, 0))
        }
        return out
    }

    private mutating func put(_ s: UInt8, _ n: UInt8, _ v: UInt8, into out: inout [MIDIMessage]) {
        let key = UInt16(s) << 8 | UInt16(n)
        guard sent[key] != v else { return }
        sent[key] = v
        out.append(MIDIMessage(s, n, v))
    }

    /// the deck buttons with LEDs, and their (unshifted, shifted) notes on 0x90 / 0x91
    private static let buttonNotes: [(Deck) -> DDJControl] = [
        { .play($0) }, { .cue($0) }, { .sync($0) }, { .loopIn($0) }, { .loopOut($0) }, { .reloop($0) },
        { .callLeft($0) }, { .callRight($0) }, { .headphoneCue($0) },
    ]
    private static let notes: [(UInt8, UInt8)] = [
        (0x0B, 0x47), (0x0C, 0x48), (0x58, 0x60), (0x10, 0x4C), (0x11, 0x4E), (0x4D, 0x50),
        (0x51, 0x3E), (0x53, 0x3D), (0x54, 0x68),
    ]

    /// Every LED in a fixed order: pads of every mode (unshifted 0x97 / 0x99 and shifted 0x98 / 0x9A, lit alike),
    /// deck buttons (their unshifted and shifted notes), BEAT FX ON/OFF.
    private static func leds(padLights: (PadMode) -> [[LightLevel]],
                             button: (DDJControl) -> LightLevel) -> [(UInt8, UInt8, UInt8)] {
        var list: [(UInt8, UInt8, UInt8)] = []
        func byte(_ l: LightLevel) -> UInt8 { l == .on ? 0x7F : 0x00 }
        for mode in PadMode.allCases {
            let pads = padLights(mode)
            for d in Deck.allCases {
                let status: UInt8 = d == .left ? 0x97 : 0x99
                for p in 0..<8 {
                    let v = byte(pads[d.rawValue][p])
                    list.append((status, mode.noteOffset + UInt8(p), v))
                    list.append((status + 1, mode.noteOffset + UInt8(p), v))
                }
            }
        }
        for d in Deck.allCases {
            let status: UInt8 = d == .left ? 0x90 : 0x91
            for (control, (plain, shifted)) in zip(buttonNotes, notes) {
                let v = byte(button(control(d)))
                list.append((status, plain, v))
                list.append((status, shifted, v))
            }
        }
        let fx = byte(button(.fxOnOff))
        list.append((0x94, 0x47, fx))
        list.append((0x94, 0x43, fx))
        return list
    }
}
