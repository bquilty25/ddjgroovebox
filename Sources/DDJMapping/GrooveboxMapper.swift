// SPDX-License-Identifier: GPL-3.0-only
// How the DDJ-400 plays SLOOP: DDJEvents in, GrooveActions (the FM-1 panel) out. docs/MAPPING.md is the spec.

import Foundation   // ProcessInfo (the default clock)

/// Maps DDJ-400 events to groovebox actions, and the groovebox state back to the DDJ's lights and captions.
public final class GrooveboxMapper {
    /// the pad mode of the deck played (or set) last, for status text; each deck has its own (`padMode(_:)`)
    public private(set) var padMode: PadMode = .hotCue
    /// left jog ticks per ALGORITHM detent (track)
    public var jogTicksPerTrackStep = 32
    /// right jog ticks per PRESETS detent (sound)
    public var jogTicksPerPresetStep = 8
    /// seconds: a jog idle this long starts counting again from zero
    public var jogIdleReset = 0.3
    /// seconds: how long a track asked for (pads, LOAD) is assumed selected before SLOOP's state confirms it
    public var trackAssumeTimeout = 0.5
    /// the clock, in seconds (injectable for tests)
    public var now: () -> Double = { ProcessInfo.processInfo.systemUptime }
    /// the last state given to `update(state:)`
    public private(set) var state = GrooveState()

    private var padModes: [Deck: PadMode] = [.left: .hotCue, .right: .hotCue]
    private var assumedTrack: Int?
    private var assumedAt = 0.0

    private var keyRefs: [Int: Int] = [:]
    private var buttonRefs: [FM1Button: Int] = [:]
    private var heldButtons: [DDJControl: FM1Button] = [:]
    private struct PadHold { var key: Int; var fx: Bool }
    private var heldPads: [DDJControl: PadHold] = [:]
    private var jogAcc: [Deck: Int] = [:]
    private var jogLast: [Deck: Double] = [:]
    private var knobPosition: [DDJControl: Double] = [:]
    private var knobAcc: [DDJControl: Double] = [:]

    enum SoftFader: CaseIterable { case selectedLevel, drumLevel, bpm, swing, filter }
    struct Takeover {
        var engaged = false
        var lastFader: Int?
        var lastSent: Int?
        /// selectedLevel: the track it was engaged on
        var track: Int?
    }
    var takeover: [SoftFader: Takeover] = [:]
    private var lastVolume: Int?

    public init() {}

    // MARK: pad modes

    /// The pad mode of a deck (the DDJ has one per deck).
    public func padMode(_ deck: Deck) -> PadMode { padModes[deck] ?? .hotCue }

    /// Sets a deck's pad mode (the on-screen mode buttons; the DDJ's own come with each pad).
    public func setPadMode(_ m: PadMode, deck: Deck) {
        padModes[deck] = m
        padMode = m
    }

    // MARK: state

    /// the selected track: the one the pads or LOAD just asked for, until SLOOP confirms it (or a while passes),
    /// else the last known one
    public var selectedTrack: Int {
        if let a = assumedTrack, now() - assumedAt < trackAssumeTimeout { return a }
        return state.selectedTrack
    }

    public func update(state s: GrooveState) {
        state = s
        if let a = assumedTrack, s.selectedTrack == a || now() - assumedAt >= trackAssumeTimeout { assumedTrack = nil }
        for f in SoftFader.allCases {
            guard let t = takeover[f], t.engaged, let sent = t.lastSent else { continue }
            if abs(rawTarget(f) - sent) > 2 { disengage(f) }
        }
    }

    /// Forgets every physical position (knobs, faders, jogs): call on every (re)connect, so the first message of each
    /// control only sets its reference and the faders need pickup again.
    public func resetPhysicalState() {
        knobPosition = [:]
        knobAcc = [:]
        jogAcc = [:]
        jogLast = [:]
        takeover = [:]
        lastVolume = nil
    }

    // MARK: events

    public func handle(_ e: DDJEvent) -> [GrooveAction] {
        let c = e.control
        switch (c, e.value) {
        case (.pad(let d, _), .press(let down)):
            if let m = e.padMode { setPadMode(m, deck: d) }
            return down ? padDown(c, shifted: e.shifted) : padUp(c)
        case (.padModeButton(let d, let m), .press(true)):
            setPadMode(m, deck: d)
            return []
        case (.load(let d), .press(true)):
            return selectTrack(max(0, min(3, selectedTrack + (d == .left ? -1 : 1))))
        case (.beatLeft, .press(true)):
            return [.encoder(.select, -1)]
        case (.beatRight, .press(true)):
            return [.encoder(.select, 1)]
        case (.fxSelect, .press(true)):
            return [.encoder(.presets, e.shifted ? -1 : 1)]
        case (.browse, .relative(let n)):
            return n == 0 ? [] : [.encoder(.select, n)]
        case (.jog(let d), .relative(let n)):
            return jog(d, ticks: e.shifted ? 4 * n : n)
        case (.jogTouch(let d), .press(false)):
            jogAcc[d] = 0
            return []
        case (_, .absolute(let x)):
            if let i = Self.knobIndex(c) { return knob(c, FM1Encoder(rawValue: FM1Encoder.k1.rawValue + i)!, x) }
            return absolute(c, x)
        case (_, .press(let down)):
            guard let b = Self.fm1Button(for: c) else { return [] }
            if down {
                guard heldButtons[c] == nil else { return [] }
                heldButtons[c] = b
                return press(b)
            }
            guard let held = heldButtons.removeValue(forKey: c) else { return [] }
            return release(held)
        default:
            return []
        }
    }

    /// Releases every key and button held (on disconnect or focus loss), and forgets the physical positions.
    public func releaseAll() -> [GrooveAction] {
        let keys = keyRefs.filter { $0.value > 0 }.keys.sorted().map { GrooveAction.key($0, false) }
        let buttons = buttonRefs.filter { $0.value > 0 }.keys.sorted { $0.rawValue < $1.rawValue }
            .map { GrooveAction.button($0, false) }
        keyRefs = [:]
        buttonRefs = [:]
        heldButtons = [:]
        heldPads = [:]
        resetPhysicalState()
        if keys.isEmpty || buttons.isEmpty { return keys + buttons }
        return keys + [.barrier] + buttons
    }

    // MARK: pads and keys

    private func padDown(_ c: DDJControl, shifted: Bool) -> [GrooveAction] {
        guard case .pad(let deck, let i) = c, heldPads[c] == nil else { return [] }
        switch DDJPadFunction.of(mode: padMode(deck), deck: deck, pad: i, shifted: shifted) {
        case .none:
            return []
        case .selectTrack(let t):
            return selectTrack(t)
        case .key(let k):
            heldPads[c] = PadHold(key: k, fx: false)
            return keyDown(k)
        case .punch(let k):
            heldPads[c] = PadHold(key: k, fx: true)
            var out = press(.fx)
            if !out.isEmpty { out.append(.barrier) }
            return out + keyDown(k)
        }
    }

    /// what the pad pressed, whatever the mode or SHIFT now
    private func padUp(_ c: DDJControl) -> [GrooveAction] {
        guard let h = heldPads.removeValue(forKey: c) else { return [] }
        var out = keyUp(h.key)
        if h.fx {
            let fx = release(.fx)
            if !fx.isEmpty { out += [.barrier] + fx }
        }
        return out
    }

    /// SLOOP selects the track directly; it is assumed selected until SLOOP confirms it.
    private func selectTrack(_ t: Int) -> [GrooveAction] {
        guard t != selectedTrack else { return [] }
        assumedTrack = t
        assumedAt = now()
        return [.selectTrack(t)]
    }

    /// A key already held by another control is struck again (up, then down), so every strike sounds.
    private func keyDown(_ k: Int) -> [GrooveAction] {
        keyRefs[k, default: 0] += 1
        return keyRefs[k] == 1 ? [.key(k, true)] : [.key(k, false), .barrier, .key(k, true)]
    }

    /// released when its last holder lets go
    private func keyUp(_ k: Int) -> [GrooveAction] {
        guard let n = keyRefs[k], n > 0 else { return [] }
        keyRefs[k] = n - 1
        return n == 1 ? [.key(k, false)] : []
    }

    private func press(_ b: FM1Button) -> [GrooveAction] {
        buttonRefs[b, default: 0] += 1
        return buttonRefs[b] == 1 ? [.button(b, true)] : []
    }

    private func release(_ b: FM1Button) -> [GrooveAction] {
        guard let n = buttonRefs[b], n > 0 else { return [] }
        buttonRefs[b] = n - 1
        return n == 1 ? [.button(b, false)] : []
    }

    // MARK: jogs and knobs

    private func jog(_ d: Deck, ticks: Int) -> [GrooveAction] {
        guard ticks != 0 else { return [] }
        let t = now()
        var acc = jogAcc[d] ?? 0
        if let last = jogLast[d], t - last > jogIdleReset { acc = 0 }
        jogLast[d] = t
        if acc != 0 && (acc > 0) != (ticks > 0) { acc = 0 }
        acc += ticks
        var out: [GrooveAction] = []
        if d == .left {
            // ALGORITHM moves one track per UI pass: one detent, then a barrier
            let per = max(1, jogTicksPerTrackStep)
            while abs(acc) >= per {
                let step = acc > 0 ? 1 : -1
                out += [.encoder(.algorithm, step), .barrier]
                acc -= step * per
            }
            if !out.isEmpty { assumedTrack = nil }   // SLOOP's state tells where it went
        } else {
            let per = max(1, jogTicksPerPresetStep)
            let n = acc / per
            acc -= n * per
            if n != 0 { out.append(.encoder(.presets, n)) }
        }
        jogAcc[d] = acc
        return out
    }

    /// EQ HI, MID, LOW, CFX (either channel): KNOB 1...4
    static func knobIndex(_ c: DDJControl) -> Int? {
        switch c {
        case .eqHi: return 0
        case .eqMid: return 1
        case .eqLow: return 2
        case .cfx: return 3
        default: return nil
        }
    }

    /// An absolute knob turned into the FM-1's relative encoder: a full turn is 64 detents.
    private func knob(_ c: DDJControl, _ enc: FM1Encoder, _ x: Double) -> [GrooveAction] {
        defer { knobPosition[c] = x }
        guard let previous = knobPosition[c] else { return [] }
        var acc = (knobAcc[c] ?? 0) + (x - previous) * 64
        let n = Int(acc + (acc > 0 ? 1e-9 : -1e-9))   // toward zero (floating-point slack), the remainder is kept
        acc -= Double(n)
        knobAcc[c] = acc
        return n == 0 ? [] : [.encoder(enc, n)]
    }

    // MARK: absolute targets

    private func absolute(_ c: DDJControl, _ x: Double) -> [GrooveAction] {
        switch c {
        case .channelFader(let d):
            return soft(d == .left ? .selectedLevel : .drumLevel, Int((x * 127).rounded()))
        case .tempo(let d):
            // 1 is the "+" end, towards the user: faster / more swing (see DDJ400Decoder)
            if d == .left { return soft(.bpm, 60 + Int((x * 120).rounded())) }
            return soft(.swing, Int((x * 100).rounded()))
        case .crossfader:
            return soft(.filter, Self.filterValue(x))
        case .trim(.left):
            let v = Int((x * 127).rounded())
            guard v != lastVolume else { return [] }
            lastVolume = v
            return [.masterVolume(v)]
        default:
            return []   // right TRIM, BEAT FX LEVEL/DEPTH, headphone level: unmapped
        }
    }

    /// what each soft fader's output can reach
    static func range(_ f: SoftFader) -> ClosedRange<Int> {
        switch f {
        case .selectedLevel, .drumLevel: return 0...127
        case .bpm: return 60...180
        case .swing: return 0...100
        case .filter: return -64...63
        }
    }

    static func fader(for c: DDJControl) -> SoftFader? {
        switch c {
        case .channelFader(let d): return d == .left ? .selectedLevel : .drumLevel
        case .tempo(let d): return d == .left ? .bpm : .swing
        case .crossfader: return .filter
        default: return nil
        }
    }

    /// the groovebox's current value of what a fader sets
    func rawTarget(_ f: SoftFader) -> Int {
        switch f {
        case .selectedLevel:
            let t = selectedTrack
            return state.trackLevel.indices.contains(t) ? state.trackLevel[t] : 0
        case .drumLevel: return state.trackLevel.indices.contains(3) ? state.trackLevel[3] : 0
        case .bpm: return state.bpm
        case .swing: return state.swing
        case .filter: return state.masterFilter
        }
    }

    /// the current value as the fader can reach it (a tempo above 180 BPM: picked up at the slider's 180 end)
    func target(_ f: SoftFader) -> Int {
        let r = Self.range(f)
        return max(r.lowerBound, min(r.upperBound, rawTarget(f)))
    }

    private func disengage(_ f: SoftFader) {
        takeover[f, default: Takeover()].engaged = false
        takeover[f, default: Takeover()].lastSent = nil
    }

    /// whether a fader is following (engaged, and channel 1 still on the track it picked up)
    func following(_ f: SoftFader) -> Bool {
        guard let t = takeover[f], t.engaged else { return false }
        return f != .selectedLevel || t.track == selectedTrack
    }

    /// Soft takeover: output only once the fader comes within 3 of, or crosses, the current value.
    private func soft(_ f: SoftFader, _ v: Int) -> [GrooveAction] {
        if takeover[f]?.engaged == true && !following(f) { disengage(f) }   // channel 1: another track selected
        var t = takeover[f] ?? Takeover()
        defer {
            t.lastFader = v
            takeover[f] = t
        }
        if !t.engaged {
            let current = target(f)
            let near = abs(v - current) <= 3
            let crossed = t.lastFader.map { ($0 - current) * (v - current) <= 0 } ?? false
            guard near || crossed else { return [] }
            t.engaged = true
            t.track = selectedTrack
        }
        guard v != t.lastSent else { return [] }
        t.lastSent = v
        switch f {
        case .selectedLevel: return [.trackLevel(track: selectedTrack, value: v)]
        case .drumLevel: return [.trackLevel(track: 3, value: v)]
        case .bpm: return [.bpm(v)]
        case .swing: return [.swing(v)]
        case .filter: return [.masterFilter(v)]
        }
    }

    /// the centre dead zone of the crossfader (off)
    static let filterDeadZone = 0.03

    /// Crossfader 0...1 to the master filter -64...63, centre (within 0.03) off.
    static func filterValue(_ x: Double) -> Int {
        let dead = filterDeadZone
        if abs(x - 0.5) < dead { return 0 }
        if x < 0.5 {
            let f = max(0, x) / (0.5 - dead)               // 0 ... 1
            return min(-1, -64 + Int((f * 63).rounded()))
        }
        let f = (min(1, x) - 0.5 - dead) / (0.5 - dead)    // 0 ... 1
        return max(1, 1 + Int((f * 62).rounded()))
    }

    /// The crossfader position of a filter value (the inverse of `filterValue`).
    static func filterPosition(_ v: Int) -> Double {
        let dead = filterDeadZone
        if v == 0 { return 0.5 }
        if v < 0 { return Double(max(-64, v) + 64) / 63 * (0.5 - dead) }
        return 0.5 + dead + Double(min(63, v) - 1) / 62 * (0.5 - dead)
    }

    // MARK: buttons

    /// The FM-1 button a DDJ button holds (docs/MAPPING.md, "Buttons").
    public static func fm1Button(for c: DDJControl) -> FM1Button? {
        switch c {
        case .play: return .play
        case .cue: return .rec
        case .sync(let d): return d == .left ? .seq : .fx
        case .loopIn(let d): return d == .left ? .edit : .env
        case .loopOut(let d): return d == .left ? .arp : .lfo
        case .reloop(let d): return d == .left ? .glo : .sel
        case .callLeft: return .octDown
        case .callRight: return .octUp
        case .headphoneCue(let d): return d == .left ? .home : .save
        case .browsePress: return .home
        case .fxOnOff: return .fx
        default: return nil
        }
    }

    // MARK: lights

    private func light(_ f: DDJPadFunction) -> LightLevel {
        switch f {
        case .none: return .off
        case .key(let k), .punch(let k): return state.keyLit(k) ? .on : state.keyDim(k) ? .dim : .off
        case .selectTrack(let t): return t == selectedTrack ? .on : .dim
        }
    }

    /// [deck][pad] (2 x 8) lights of the pads in `mode` (both decks).
    public func padLights(for mode: PadMode) -> [[LightLevel]] {
        Deck.allCases.map { d in (0..<8).map { light(DDJPadFunction.of(mode: mode, deck: d, pad: $0)) } }
    }

    /// The lights of the DDJ (each deck's pads in its own mode, buttons, pad mode buttons) from the last state.
    public func lights() -> [DDJControl: LightLevel] {
        var l: [DDJControl: LightLevel] = [:]
        for d in Deck.allCases {
            let pads = padLights(for: padMode(d))[d.rawValue]
            for p in 0..<8 { l[.pad(d, p)] = pads[p] }
            let buttons: [DDJControl] = [.play(d), .cue(d), .sync(d), .loopIn(d), .loopOut(d), .reloop(d),
                                         .callLeft(d), .callRight(d), .headphoneCue(d)]
            for c in buttons { l[c] = buttonLight(c) }
            for m in PadMode.allCases { l[.padModeButton(d, m)] = m == padMode(d) ? .on : .off }
        }
        l[.fxOnOff] = buttonLight(.fxOnOff)
        return l
    }

    private func buttonLight(_ c: DDJControl) -> LightLevel {
        guard let b = Self.fm1Button(for: c) else { return .off }
        return state.buttonLit(b) ? .on : .off
    }

    // MARK: takeover marks

    /// For a soft-takeover control (channel faders, tempo sliders, crossfader): where the control must go to take
    /// over, as its position 0...1 in the units of its `.absolute` value (the tempo sliders: 1 = the near, fast end),
    /// and whether it is following now. nil for other controls.
    public func takeover(for c: DDJControl) -> (target: Double, active: Bool)? {
        guard let f = Self.fader(for: c) else { return nil }
        let v = target(f)
        let x: Double
        switch f {
        case .selectedLevel, .drumLevel: x = Double(v) / 127
        case .bpm: x = Double(v - 60) / 120
        case .swing: x = Double(v) / 100
        case .filter: x = Self.filterPosition(v)
        }
        return (max(0, min(1, x)), following(f))
    }

    // MARK: captions

    /// The short SLOOP job printed on the on-screen DDJ (pads in their deck's mode), nil for a control with none.
    public func caption(for c: DDJControl) -> String? {
        switch c {
        case .pad(let d, let p):
            switch DDJPadFunction.of(mode: padMode(d), deck: d, pad: p) {
            case .none: return nil
            case .key(let k): return FM1Keyboard.noteName(k)
            case .punch(let k): return "FX " + FM1Keyboard.noteName(k)
            case .selectTrack(let t): return t == 3 ? "DRUM" : "TRK \(t + 1)"
            }
        case .jog(let d): return d == .left ? "TRACK" : "SOUND"
        case .eqHi: return "K1"
        case .eqMid: return "K2"
        case .eqLow: return "K3"
        case .cfx: return "K4"
        case .channelFader(let d): return d == .left ? "LVL SEL" : "LVL DRM"
        case .crossfader: return "FILTER"
        case .tempo(let d): return d == .left ? "BPM" : "SWING"
        case .trim(let d): return d == .left ? "MASTER" : nil
        case .browse: return "SELECT"
        case .load(let d): return d == .left ? "TRK -" : "TRK +"
        case .beatLeft: return "SEL -"
        case .beatRight: return "SEL +"
        case .fxSelect: return "SOUND +"
        case .shift, .jogTouch, .headphoneLevel, .fxLevel, .padModeButton: return nil
        default: return Self.fm1Button(for: c)?.label
        }
    }
}
