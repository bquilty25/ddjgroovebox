// SPDX-License-Identifier: GPL-3.0-only
// TRIGDECK on the DDJ-400 (docs/VISION.md): DDJ events in, engine actions out, and for every control the face it shows
// on the augmented controller (its job, value, arc, colour, light). Pure logic over a TrigModel: unit tested.
import Foundation

public enum PadFunction: Int, Hashable, Sendable {
    case steps, keys, tracks, patterns
    public var name: String { ["STEPS", "KEYS", "TRACKS", "PATTERNS"][rawValue] }
    /// HOT CUE (and its SHIFT twin PAD FX1) = STEPS, BEAT LOOP / KEYBOARD = KEYS, BEAT JUMP / PAD FX2 = TRACKS,
    /// SAMPLER / KEY SHIFT = PATTERNS
    public init(_ mode: PadMode) {
        switch mode {
        case .hotCue, .padFX1: self = .steps
        case .beatLoop, .keyboard: self = .keys
        case .beatJump, .padFX2: self = .tracks
        case .sampler, .keyShift: self = .patterns
        }
    }
}

public struct TrigHeader: Hashable, Sendable {
    public var pattern: Int, queued: Int?, bpm: Double, swing: Int, playing: Bool, recording: Bool, fill: Bool
    public var track: Int, machine: String, page: TrigPage, stepPage: Int, pages: Int
    public var perf: String?, sceneEdit: Int?, heldSteps: [Int]
}

public final class TrigMapper {
    public let model: TrigModel
    public var now: () -> Double = { ProcessInfo.processInfo.systemUptime }

    public private(set) var track = 0
    public private(set) var page: TrigPage = .src
    private var pageBeforeMaster: TrigPage = .src
    public private(set) var stepPage = 0
    private var modes: [Deck: PadMode] = [.left: .hotCue, .right: .hotCue]
    public private(set) var fillHeld = false
    public private(set) var sceneEdit: Int?
    public private(set) var perfType = 0, perfDivision = 2, perfTarget = 2
    public private(set) var perfAmount = 0.6, perfOn = false

    private var held: [DDJControl: Int] = [:]           // a step pad held -> its step
    private var heldTrackPads: [DDJControl: Int] = [:]  // TRACKS mode: a track's pad held -> the track
    private var pressedPads: Set<DDJControl> = []
    private var euclidPulses: [Int: Int] = [:]
    private var rotateAcc: [Deck: Int] = [:]
    private var setOnPress: Set<Int> = []                // steps a press set (not cleared on its release)
    private var edited: Set<Int> = []                    // held steps changed by a knob / jog / BROWSE
    private var playing: [DDJControl: (track: Int, note: Int)] = [:]   // KEYS pads / audition held
    private var knobLast: [DDJControl: Double] = [:]
    private var knobAcc: [DDJControl: Double] = [:]
    private var touching: Set<Deck> = []
    private var jogLastTime: [Deck: Double] = [:]
    private var scratchRate: [Deck: Double] = [:]
    private var nudgeLast: Double?
    private var tempoPicked = false, swingPicked = false
    private var lastTempoSet: Double?, lastSwingSet: Int?

    public static let ticksPerSecondAtSpeed = 400.0     // 720 ticks a turn at 33 1/3 rpm (Mixxx's DDJ-400 setting)

    public init(model: TrigModel) { self.model = model }

    public func padMode(_ deck: Deck) -> PadMode { modes[deck] ?? .hotCue }
    public func padFunction(_ deck: Deck) -> PadFunction { PadFunction(padMode(deck)) }
    public func setPadMode(_ m: PadMode, deck: Deck) { modes[deck] = m }

    static func deckBase(_ d: Deck) -> Int { d == .left ? 0 : 4 }
    public static func slot(of c: DDJControl) -> Int? {
        switch c {
        case .eqHi(let d): return d == .left ? 0 : 4
        case .eqMid(let d): return d == .left ? 1 : 5
        case .eqLow(let d): return d == .left ? 2 : 6
        case .cfx(let d): return d == .left ? 3 : 7
        default: return nil
        }
    }
    func param(_ slot: Int) -> Int { page.rawValue * 8 + slot }
    public var heldSteps: [Int] { Array(Set(held.values)).sorted() }

    // MARK: - Events

    public func handle(_ e: DDJEvent) -> [TrigAction] {
        if case .pad(let deck, let i) = e.control, let m = e.padMode { modes[deck] = m; return pad(deck, i, e) }
        switch (e.control, e.value) {
        case (.padModeButton(let d, let m), .press(true)): modes[d] = m; return []
        case (.play, .press(true)): return [e.shifted ? .restart : .togglePlay]
        case (.cue(.left), .press(true)): return [.toggleRecord]
        case (.cue(.right), .press(true)): return [.tapTempo]
        case (.sync(.left), .press(true)) where e.shifted: return [.autopilot(!model.autopilot)]   // SHIFT + FILL
        case (.sync(.left), .press(let down)):
            if !down && !fillHeld { return [] }
            fillHeld = down
            return [.fill(down)]
        case (.sync(.right), .press(true)):
            if page == .master { page = pageBeforeMaster } else { pageBeforeMaster = page; page = .master }
            return []
        case (.loopIn(let d), .press(true)): return setPage(d == .left ? .trig : .amp)
        case (.loopOut(let d), .press(true)): return setPage(d == .left ? .src : .fx)
        case (.reloop(let d), .press(true)): return setPage(d == .left ? .fltr : .lfo)
        case (.callLeft(.left), .press(true)): return selectTrack(track - 1)
        case (.callRight(.left), .press(true)): return selectTrack(track + 1)
        case (.callLeft(.right), .press(true)): stepPage = max(0, stepPage - 1); return []
        case (.callRight(.right), .press(true)): stepPage = min(pageCount - 1, stepPage + 1); return []
        case (.load(let d), .press(true)):
            if !held.isEmpty {                           // a step held: LOAD left = slide, right = accent (acid)
                let first = model.step(track: track, step: heldSteps[0])
                return heldSteps.map { st in
                    edited.insert(st)
                    return d == .left ? .stepSlide(track: track, step: st, on: !first.slide)
                                      : .stepAccent(track: track, step: st, on: !first.accent)
                }
            }
            let p = (model.queuedPattern ?? model.pattern) + (d == .left ? -1 : 1)
            return [.queuePattern(min(15, max(0, p)))]
        case (.browse, .relative(let n)): return browse(n)
        case (.browsePress, .press(let down)):
            if let t = heldTrackPads.values.first {       // a track held: roll the dice on it
                return down ? [.dice(track: t, amount: 25)] : []
            }
            return audition(.browsePress, down)
        case (.headphoneCue(let d), .press(let down)):
            let s = d == .left ? 0 : 1
            if down && e.shifted { return [.sceneClear(s)] }
            sceneEdit = down ? s : (sceneEdit == s ? nil : sceneEdit)
            return []
        case (.fxSelect, .press(true)):
            perfType = (perfType + (e.shifted ? PerfFX.allCases.count - 1 : 1)) % PerfFX.allCases.count
            return perfOn ? [perfAction(on: true)] : []
        case (.beatLeft, .press(true)): perfDivision = max(0, perfDivision - 1); return perfOn ? [perfAction(on: true)] : []
        case (.beatRight, .press(true)): perfDivision = min(5, perfDivision + 1); return perfOn ? [perfAction(on: true)] : []
        case (.fxOnOff, .press(true)): perfOn.toggle(); return [perfAction(on: perfOn)]   // a toggle: press on, press off
        case (.fxOnOff, .press(false)): return []
        case (.fxChannel(let n), .press(true)): setPerfTarget(n); return perfOn ? [perfAction(on: true)] : []
        case (.fxLevel, .absolute(let v)): perfAmount = v; return perfOn ? [perfAction(on: true)] : []
        case (.trim(let d), .absolute(let v)): return [.deckDrive(deck: d.rawValue, drive: v)]
        case (.channelFader(let d), .absolute(let v)): return [.deckLevel(deck: d.rawValue, level: v)]
        case (.crossfader, .absolute(let v)): return [.crossfader(v)]
        case (.tempo(.left), .absolute(let v)): return tempo(v)
        case (.tempo(.right), .absolute(let v)): return swing(v)
        case (.jogTouch(let d), .press(true)) where e.shifted:
            return [.capture(deck: d.rawValue, bars: 1)]  // SHIFT + touch: resample the deck's last bar
        case (.jogTouch(let d), .press(let down)):
            if !down && !touching.contains(d) { return [] }
            if down { touching.insert(d) } else { touching.remove(d) }
            scratchRate[d] = 0
            return [.scratch(deck: d.rawValue, touching: down, rate: 0)]
        case (.jog(let d), .relative(let n)): return jog(d, n, shifted: e.shifted)
        default:
            if let s = Self.slot(of: e.control), case .absolute(let v) = e.value { return knob(e.control, s, v) }
            return []
        }
    }

    /// Time-based releases (call ~60 times a second): a scratched jog that stops moving holds still; a nudge ends.
    public func tick() -> [TrigAction] {
        var out: [TrigAction] = []
        let t = now()
        for d in touching where (scratchRate[d] ?? 0) != 0 && t - (jogLastTime[d] ?? t) > 0.05 {
            scratchRate[d] = 0
            out.append(.scratch(deck: d.rawValue, touching: true, rate: 0))
        }
        if let last = nudgeLast, t - last > 0.08 { nudgeLast = nil; out.append(.nudge(0)) }
        return out
    }

    /// Let everything go (focus lost, controller unplugged).
    public func releaseAll() -> [TrigAction] {
        var out: [TrigAction] = playing.values.map { .release(track: $0.track, note: $0.note) }
        if fillHeld { out.append(.fill(false)) }
        if perfOn { out.append(perfAction(on: false)) }
        for d in touching { out.append(.scratch(deck: d.rawValue, touching: false, rate: 0)) }
        if nudgeLast != nil { out.append(.nudge(0)) }
        playing = [:]; held = [:]; setOnPress = []; edited = []; touching = []; fillHeld = false; perfOn = false
        heldTrackPads = [:]; rotateAcc = [:]; euclidPulses = [:]; pressedPads = []
        sceneEdit = nil; nudgeLast = nil; knobLast = [:]; knobAcc = [:]; tempoPicked = false; swingPicked = false
        return out
    }

    var pageCount: Int { max(1, (model.length(track) + 15) / 16) }

    private func setPage(_ p: TrigPage) -> [TrigAction] { page = p; return [] }

    private func selectTrack(_ t: Int) -> [TrigAction] {
        let n = min(7, max(0, t))
        guard n != track else { return [] }
        track = n
        let d: Deck = n < 4 ? .left : .right             // its deck's dial points at it (re-centred unless the jog chose it)
        if jogDial[d]?.anchor != n - Self.deckBase(d) { jogDial[d] = (n - Self.deckBase(d), 0) }
        stepPage = min(stepPage, pageCount - 1)
        return [.selectTrack(n)]
    }

    private func perfAction(on: Bool) -> TrigAction {
        .perf(fx: perfType, division: perfDivision, target: perfTarget, on: on, amount: perfAmount)
    }

    /// Select a track from elsewhere (a click on its row in the strip above the controller).
    public func select(track t: Int) -> [TrigAction] { selectTrack(t) }

    /// The beat FX channel switch (1, 2, MASTER).
    public func setPerfTarget(_ t: Int) { perfTarget = min(2, max(0, t)) }

    // MARK: pads

    private func pad(_ deck: Deck, _ i: Int, _ e: DDJEvent) -> [TrigAction] {
        let c = e.control
        guard case .press(let down) = e.value else { return [] }
        if down { pressedPads.insert(c) } else { pressedPads.remove(c) }   // (lit while held, any mode)
        switch PadFunction(padMode(deck)) {
        case .steps:
            let step = stepPage * 16 + (deck == .left ? 0 : 8) + i
            if down {
                held[c] = step
                if !model.step(track: track, step: step).trig {
                    setOnPress.insert(step)
                    return [.stepSet(track: track, step: step, on: true)]
                }
                return []
            }
            guard let s = held.removeValue(forKey: c) else { return [] }
            defer { if !held.values.contains(s) { setOnPress.remove(s); edited.remove(s) } }
            if !setOnPress.contains(s) && !edited.contains(s) && model.step(track: track, step: s).trig {
                return [.stepSet(track: track, step: s, on: false)]
            }
            return []
        case .keys:
            if down {
                let (t, note, vel) = keyNote(deck, i)
                playing[c] = (t, note)
                return [.trigger(track: t, note: note, velocity: vel)]
            }
            guard let p = playing.removeValue(forKey: c) else { return [] }
            return [.release(track: p.track, note: p.note)]
        case .tracks:
            let t = Self.deckBase(deck) + i % 4
            if i < 4 {                                   // select; held: the jog rotates it, BROWSE makes it Euclidean
                if !down { heldTrackPads.removeValue(forKey: c); return [] }
                heldTrackPads[c] = t
                return selectTrack(t)
            }
            return down ? [.mute(track: t, on: !model.muted(t))] : []
        case .patterns:
            guard down else { return [] }
            let p = (deck == .left ? 0 : 8) + i
            return e.shifted ? [.copyPattern(from: model.pattern, to: p)] : [.queuePattern(p)]
        }
    }

    /// KEYS: a melodic track plays 16 semitones up from its TRIG note; a drum deck plays its own 4 tracks
    /// (top row full, bottom row soft)
    func keyNote(_ deck: Deck, _ i: Int) -> (track: Int, note: Int, velocity: Int) {
        if model.machine(track).isMelodic {
            let base = model.value(track: track, param: 0)
            let k = (deck == .left ? 0 : 8) + (i < 4 ? i + 4 : i - 4)   // bottom row lower, as a keyboard reads
            return (track, min(127, base + k), 100)
        }
        return (Self.deckBase(deck) + i % 4, 60, i < 4 ? 110 : 55)
    }

    private func audition(_ c: DDJControl, _ down: Bool) -> [TrigAction] {
        if down {
            let note = model.machine(track).isMelodic ? model.value(track: track, param: 0) : 60
            playing[c] = (track, note)
            return [.trigger(track: track, note: note, velocity: 100)]
        }
        guard let p = playing.removeValue(forKey: c) else { return [] }
        return [.release(track: p.track, note: p.note)]
    }

    // MARK: knobs, BROWSE, jogs, sliders

    private func knob(_ c: DDJControl, _ slot: Int, _ v: Double) -> [TrigAction] {
        guard let last = knobLast[c] else { knobLast[c] = v; return [] }
        knobLast[c] = v
        let info = page == .master ? model.globalInfo(slot: slot) : model.info(track: track, param: param(slot))
        guard let info, info.max > info.min else { return [] }
        // a full turn sweeps the whole range; the fraction left over waits for the next movement
        let acc = (knobAcc[c] ?? 0) + (v - last) * Double(info.max - info.min)
        let n = Int((acc + (acc >= 0 ? 1e-9 : -1e-9)).rounded(.towardZero))   // (0.6 - 0.5 is 0.0999...: not a step short)
        knobAcc[c] = acc - Double(n)
        guard n != 0 else { return [] }
        func clamped(_ x: Int) -> Int { min(info.max, max(info.min, x)) }
        if page == .master { return [.globalSet(slot: slot, value: clamped(model.globalValue(slot: slot) + n))] }
        let p = param(slot)
        if let s = sceneEdit {
            let base = model.scene(s, track: track, param: p) ?? model.value(track: track, param: p)
            return [.sceneSet(scene: s, track: track, param: p, value: clamped(base + n))]
        }
        if !held.isEmpty && info.lockable {
            return heldSteps.map { st in
                edited.insert(st)
                let base = model.lock(track: track, step: st, param: p) ?? model.value(track: track, param: p)
                return .stepLock(track: track, step: st, param: p, value: clamped(base + n))
            }
        }
        return [.paramSet(track: track, param: p, value: clamped(model.value(track: track, param: p) + n))]
    }

    /// how many trigs a track has now (the start of a Euclidean edit)
    func trigCount(_ t: Int) -> Int { (0..<model.length(t)).filter { model.step(track: t, step: $0).trig }.count }

    private func browse(_ n: Int) -> [TrigAction] {
        if let t = heldTrackPads.values.first {          // Euclid: n pulses spread over the track's length
            let len = model.length(t)
            let p = min(len, max(0, (euclidPulses[t] ?? trigCount(t)) + n))
            euclidPulses[t] = p
            return [.euclid(track: t, pulses: p, steps: len)]
        }
        if !held.isEmpty {
            let k = max(1, model.condCount)
            return heldSteps.map { st in
                edited.insert(st)
                let c = ((model.step(track: track, step: st).cond + n) % k + k) % k
                return .stepCond(track: track, step: st, cond: c)
            }
        }
        if page == .src {
            let k = Machine.allCases.count
            return [.machine(track: track, machine: ((model.machine(track).rawValue + n) % k + k) % k)]
        }
        return selectTrack(track + n)
    }

    /// ticks of a jog's side per track: a quarter turn of the platter (720 ticks a turn), so the marker on the drawn jog
    /// turns with the real one and the quadrant it points into is always the active track
    public var jogTicksPerTrack = 180
    /// ticks past a quadrant's edge before the next track (no flicker on the edge; inside the gap between quadrants)
    public var jogHysteresis = 6
    /// ticks of a jog whose deck does not hold the active track before it brings it to its deck
    public var jogFocusTicks = 24

    /// Where a deck's track-ring marker points, in quadrants: 0 is the centre of the deck's first track, 1 the second's
    /// (0..<4, then around). On the active track's quadrant when the active track is on this deck (re-centred when it
    /// was chosen another way), else where the deck's marker was left.
    public func jogDialPosition(_ d: Deck) -> Double {
        let dial = currentDial(d)
        return Double(dial.anchor) + Double(dial.pos) / Double(jogTicksPerTrack)
    }

    private func currentDial(_ d: Deck) -> (anchor: Int, pos: Int) {
        let base = Self.deckBase(d)
        let dial = jogDial[d] ?? (anchor: 0, pos: 0)
        if (base..<base + 4).contains(track), track - base != dial.anchor { return (track - base, 0) }
        return dial
    }
    private var jogDial: [Deck: (anchor: Int, pos: Int)] = [:]
    private var jogFocusAcc: [Deck: Int] = [:]

    private func jog(_ d: Deck, _ n: Int, shifted: Bool) -> [TrigAction] {
        let t = now()
        defer { jogLastTime[d] = t }
        if touching.contains(d) {                        // scratch: the platter's speed is the playback rate
            let dt = max(0.002, t - (jogLastTime[d] ?? t - 0.01))
            let r = max(-4, min(4, Double(n) / dt / Self.ticksPerSecondAtSpeed))
            let rate = 0.5 * (scratchRate[d] ?? 0) + 0.5 * r
            scratchRate[d] = rate
            return [.scratch(deck: d.rawValue, touching: true, rate: rate)]
        }
        if let tr = heldTrackPads.values.first {        // a track held: rotate it, one step per 8 ticks
            let acc = (rotateAcc[d] ?? 0) + n
            let k = acc / 8
            rotateAcc[d] = acc - k * 8
            return k != 0 ? [.rotate(track: tr, amount: k)] : []
        }
        if !held.isEmpty {                               // micro timing of the held steps: 1 per 4 ticks
            let delta = n / 4 == 0 ? (n > 0 ? 1 : -1) : n / 4
            return heldSteps.map { st in
                edited.insert(st)
                let m = model.step(track: track, step: st).micro + delta
                return .stepMicro(track: track, step: st, micro: min(23, max(-23, m)))
            }
        }
        if shifted {                                     // SHIFT + jog: a DJ nudge of the tempo
            nudgeLast = t
            return [.nudge(max(-1, min(1, Double(n) / 6)))]
        }
        // the jog's side: a dial around the deck's ring of four tracks (left: 1-4, right: 5-8). Its marker turns with
        // the platter, a quarter turn a track; the quadrant it points into is the active track.
        let base = Self.deckBase(d)
        var dial = currentDial(d)
        dial.pos += n
        let edge = jogTicksPerTrack / 2 + jogHysteresis
        while dial.pos > edge { dial.pos -= jogTicksPerTrack; dial.anchor = (dial.anchor + 1) % 4 }
        while dial.pos < -edge { dial.pos += jogTicksPerTrack; dial.anchor = (dial.anchor + 3) % 4 }
        jogDial[d] = dial
        if (base..<base + 4).contains(track) { return selectTrack(base + dial.anchor) }   // (the same track: nothing)
        // the active track is on the other deck: a short turn of this one brings it here, to its marker's quadrant
        let idle = t - (jogLastTime[d] ?? 0) > 0.3
        let acc = (idle ? 0 : jogFocusAcc[d] ?? 0) + abs(n)
        jogFocusAcc[d] = acc >= jogFocusTicks ? 0 : acc
        return acc >= jogFocusTicks ? selectTrack(base + dial.anchor) : []
    }

    private func tempo(_ v: Double) -> [TrigAction] {
        let bpm = (60 + v * 140).rounded()               // towards you (down, the "+" end) = faster
        if let last = lastTempoSet, abs(model.bpm - last) > 1.5 { tempoPicked = false }
        if !tempoPicked && abs(bpm - model.bpm) > 2 { return [] }   // soft takeover
        tempoPicked = true
        lastTempoSet = bpm
        return [.tempo(bpm)]
    }

    private func swing(_ v: Double) -> [TrigAction] {
        let s = Int((50 + v * 30).rounded())
        if let last = lastSwingSet, model.swing != last { swingPicked = false }
        if !swingPicked && abs(s - model.swing) > 1 { return [] }
        swingPicked = true
        lastSwingSet = s
        return [.swing(s)]
    }

    // MARK: - Faces

    public var header: TrigHeader {
        TrigHeader(pattern: model.pattern, queued: model.queuedPattern, bpm: model.bpm, swing: model.swing,
                   playing: model.playing, recording: model.recording, fill: fillHeld, track: track,
                   machine: model.machineName(model.machine(track).rawValue), page: page, stepPage: stepPage,
                   pages: pageCount, perf: perfOn ? PerfFX(rawValue: perfType)?.name : nil, sceneEdit: sceneEdit,
                   heldSteps: heldSteps)
    }

    static let divisions = ["1/32", "1/16", "1/8", "1/4", "1/2", "1 BAR"]

    public func face(_ c: DDJControl) -> ControlFace? {
        let trackColor = TrigColors.track[track]
        switch c {
        case .pad(let d, let i): return padFace(d, i, mode: padMode(d))
        case .padModeButton(let d, let m):
            return ControlFace(title: PadFunction(m).name, light: padMode(d) == m ? .on : .off)
        case .play: return ControlFace(title: model.playing ? "STOP" : "PLAY", color: 0xFF1ECC70,
                                       light: model.playing ? .on : .off)
        case .cue(.left): return ControlFace(title: "REC", color: 0xFFFF2C34, light: model.recording ? .on : .off)
        case .cue(.right): return ControlFace(title: "TAP", value: "\(Int(model.bpm.rounded()))")
        case .sync(.left): return ControlFace(title: "FILL", value: model.autopilot ? "AUTOPILOT" : nil,
                                              light: fillHeld || model.autopilot ? .on : .off)
        case .sync(.right): return ControlFace(title: "MASTER", color: TrigColors.page[6], light: page == .master ? .on : .off)
        case .loopIn(let d): return pageFace(d == .left ? .trig : .amp)
        case .loopOut(let d): return pageFace(d == .left ? .src : .fx)
        case .reloop(let d): return pageFace(d == .left ? .fltr : .lfo)
        case .callLeft(.left): return ControlFace(title: "TRK -")
        case .callRight(.left): return ControlFace(title: "TRK +")
        case .callLeft(.right): return ControlFace(title: "PAGE -", light: stepPage > 0 ? .dim : .off)
        case .callRight(.right): return ControlFace(title: "PAGE +", light: stepPage < pageCount - 1 ? .dim : .off)
        case .load(let d):
            if let st = heldSteps.first {
                let sv = model.step(track: track, step: st)
                return d == .left ? ControlFace(title: "SLIDE", light: sv.slide ? .on : .off)
                                  : ControlFace(title: "ACCENT", light: sv.accent ? .on : .off)
            }
            return ControlFace(title: d == .left ? "PAT -" : "PAT +")
        case .browse:
            if let t = heldTrackPads.values.first {
                return ControlFace(title: "EUCLID", value: "\(euclidPulses[t] ?? trigCount(t))/\(model.length(t))",
                                   color: TrigColors.track[t])
            }
            if !held.isEmpty, let st = heldSteps.first {
                return ControlFace(title: "COND", value: model.condName(model.step(track: track, step: st).cond))
            }
            if page == .src { return ControlFace(title: "MACHINE", value: model.machineName(model.machine(track).rawValue), color: trackColor) }
            return ControlFace(title: "TRACK", value: "\(track + 1)", color: trackColor)
        case .browsePress: return ControlFace(title: heldTrackPads.isEmpty ? "AUDITION" : "DICE")
        case .headphoneCue(let d):
            let s = d == .left ? 0 : 1
            return ControlFace(title: s == 0 ? "SCENE A" : "SCENE B", color: s == 0 ? TrigColors.sceneA : TrigColors.sceneB,
                               light: sceneEdit == s ? .on : .off)
        case .fxSelect: return ControlFace(title: "FX", value: PerfFX(rawValue: perfType)?.name)
        case .beatLeft: return ControlFace(title: "DIV-", value: Self.divisions[perfDivision])
        case .beatRight: return ControlFace(title: "DIV+", value: Self.divisions[perfDivision])
        case .fxOnOff: return ControlFace(title: PerfFX(rawValue: perfType)?.name ?? "FX", color: 0xFF2E7CFF,
                                          light: perfOn ? .on : .off)
        case .fxLevel: return ControlFace(title: "AMOUNT", value: "\(Int(perfAmount * 100))%", arc: perfAmount)
        case .fxChannel(let n):
            return ControlFace(title: ["DECK A", "DECK B", "MASTER"][min(2, max(0, n))], light: perfTarget == n ? .on : .off)
        case .trim(let d): return ControlFace(title: d == .left ? "DRIVE A" : "DRIVE B")
        case .channelFader(let d): return ControlFace(title: d == .left ? "DECK A" : "DECK B")
        case .crossfader: return ControlFace(title: "A ◀ SCENE ▶ B")
        case .tempo(.left): return ControlFace(title: "TEMPO", value: String(format: "%.0f", model.bpm))
        case .tempo(.right): return ControlFace(title: "SWING", value: "\(model.swing)%")
        case .jog(let d), .jogTouch(let d):
            if let t = heldTrackPads.values.first {
                return ControlFace(title: "ROTATE", value: model.machineName(model.machine(t).rawValue), color: TrigColors.track[t])
            }
            if !held.isEmpty { return ControlFace(title: "MICRO", value: "\(model.step(track: track, step: heldSteps[0]).micro)") }
            let base = Self.deckBase(d)
            let inDeck = (base..<base + 4).contains(track)
            return ControlFace(title: d == .left ? "DECK A" : "DECK B",
                               value: inDeck ? "\(track + 1) " + model.machineName(model.machine(track).rawValue) : "TURN: TRACK",
                               color: inDeck ? trackColor : nil, light: touching.contains(d) ? .on : .off)
        case .shift: return ControlFace(title: "SHIFT")
        default:
            if let s = Self.slot(of: c) { return knobFace(s) }
            return nil
        }
    }

    private func pageFace(_ p: TrigPage) -> ControlFace {
        ControlFace(title: p.name, color: TrigColors.page[p.rawValue], light: page == p ? .on : .off)
    }

    func knobFace(_ slot: Int) -> ControlFace {
        let color = TrigColors.knobRow[slot % 4]
        if page == .master {
            guard let info = model.globalInfo(slot: slot) else { return ControlFace(title: "—", color: color) }
            let v = model.globalValue(slot: slot)
            return ControlFace(title: info.name, value: model.format(info, v), arc: arc(info, v), color: color)
        }
        let p = param(slot)
        guard let info = model.info(track: track, param: p) else { return ControlFace(title: "—", color: color) }
        var v = model.liveValue(track: track, param: p)
        var badge: String?
        if let s = sceneEdit {
            v = model.scene(s, track: track, param: p) ?? model.value(track: track, param: p)
            badge = s == 0 ? "A" : "B"
        } else if let st = heldSteps.first {
            if let l = model.lock(track: track, step: st, param: p) { v = l; badge = "LOCK" } else { v = model.value(track: track, param: p) }
        }
        // the full name: the mixer has room for it (CUTOFF, not CUT)
        return ControlFace(title: info.name, value: model.format(info, v), arc: arc(info, v), color: color, badge: badge)
    }

    private func arc(_ info: ParamInfo, _ v: Int) -> Double {
        info.max > info.min ? Double(v - info.min) / Double(info.max - info.min) : 0
    }

    func padFace(_ d: Deck, _ i: Int, mode: PadMode) -> ControlFace {
        var f = padFaceUnpressed(d, i, mode: mode)
        if pressedPads.contains(.pad(d, i)) && mode == padMode(d) { f.light = .on }   // a pad lights while it is held
        return f
    }

    private func padFaceUnpressed(_ d: Deck, _ i: Int, mode: PadMode) -> ControlFace {
        switch PadFunction(mode) {
        case .steps:
            let step = stepPage * 16 + (d == .left ? 0 : 8) + i
            guard step < model.length(track) else { return ControlFace(title: "") }
            let sv = model.step(track: track, step: step)
            var badges: [String] = []
            if sv.locks > 0 { badges.append("•") }
            if sv.cond != 0 { badges.append(model.condName(sv.cond)) }
            if sv.retrig > 0 { badges.append("x\(sv.retrig + 1)") }
            if sv.slide { badges.append("~") }
            if sv.accent { badges.append("!") }
            let isHeld = held.values.contains(step)
            return ControlFace(title: "\(step + 1)", color: TrigColors.track[track],
                               light: sv.trig ? .on : (sv.locks > 0 ? .dim : .off),
                               badge: badges.isEmpty ? nil : badges.joined(separator: " "),
                               outlined: isHeld || model.playingStep(track) == step)
        case .keys:
            let (t, note, vel) = keyNote(d, i)
            let title = model.machine(track).isMelodic ? FM1Keyboard.midiNoteName(note)
                                                         : model.machineName(model.machine(t).rawValue) + (vel < 100 ? "·" : "")
            return ControlFace(title: title, color: TrigColors.track[t], light: .dim)
        case .tracks:
            let t = Self.deckBase(d) + i % 4
            if i < 4 {
                return ControlFace(title: model.machineName(model.machine(t).rawValue), value: "\(t + 1)",
                                   color: TrigColors.track[t], light: t == track ? .on : .dim)
            }
            let muted = model.muted(t)
            return ControlFace(title: muted ? "MUTED" : "ON", color: TrigColors.track[t], light: muted ? .off : .dim)
        case .patterns:
            let p = (d == .left ? 0 : 8) + i
            return ControlFace(title: String(format: "P%02d", p + 1), light: p == model.pattern ? .on : .off,
                               outlined: p == model.queuedPattern)
        }
    }

    /// Every control's light, for the DDJ-400's LEDs.
    public func lights() -> [DDJControl: LightLevel] {
        var out: [DDJControl: LightLevel] = [:]
        for c in Self.litControls { if let f = face(c) { out[c] = f.light } }
        return out
    }

    /// The pads' lights in a given mode (the hardware shows each mode's own LEDs when switched to it). In STEPS, the
    /// playing step's pad flips (Elektron's running light): the hardware cannot draw the screen's outline.
    public func padLights(for mode: PadMode) -> [[LightLevel]] {
        Deck.allCases.map { d in
            (0..<8).map { i in
                let f = padFace(d, i, mode: mode)
                let step = stepPage * 16 + (d == .left ? 0 : 8) + i
                if PadFunction(mode) == .steps, model.playingStep(track) == step, !pressedPads.contains(.pad(d, i)) {
                    return f.light == .on ? .off : .on
                }
                return f.light
            }
        }
    }

    static let litControls: [DDJControl] = Deck.allCases.flatMap { d -> [DDJControl] in
        [.play(d), .cue(d), .sync(d), .loopIn(d), .loopOut(d), .reloop(d), .callLeft(d), .callRight(d), .headphoneCue(d)]
            + (0..<8).map { DDJControl.pad(d, $0) } + PadMode.allCases.map { DDJControl.padModeButton(d, $0) }
    } + [.fxOnOff]
}
