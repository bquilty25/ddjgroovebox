// SPDX-License-Identifier: GPL-3.0-only
// TRIGDECK's vocabulary on the Swift side: what the DDJ-400 asks the engine to do (TrigAction), what the mapper needs
// to know about the engine (TrigModel), and what each control shows on screen (ControlFace). See docs/VISION.md.

public enum TrigPage: Int, CaseIterable, Hashable, Sendable {
    case trig = 0, src, fltr, amp, fx, lfo, master
    public var name: String { ["TRIG", "SRC", "FLTR", "AMP", "FX", "LFO", "MASTER"][rawValue] }
}

public enum PerfFX: Int, CaseIterable, Hashable, Sendable {
    case repeat_ = 0, stutter, tapeStop, delayThrow, reverbThrow, filter, crush, dubHold
    case echo, flanger, phaser, trans, autopan, reverse, half, noise, distort, sweep
    public var name: String {
        ["REPEAT", "STUTTER", "TAPE STOP", "DLY THROW", "REV THROW", "FILTER", "CRUSH", "DUB HOLD",
         "ECHO", "FLANGER", "PHASER", "TRANS", "AUTOPAN", "REVERSE", "HALF", "NOISE", "DISTORT", "SWEEP"][rawValue]
    }
    /// FX SELECT's order (FILTER first; TAPE STOP left out). The raw values stay the engine's (td_perf_t).
    public static let menu: [PerfFX] = [.filter, .reverbThrow, .repeat_, .stutter, .delayThrow, .echo, .crush, .dubHold,
                                        .flanger, .phaser, .trans, .autopan, .reverse, .half, .noise, .distort, .sweep]
}

/// The engine's machines, in TrigCore's order (td_machine_t).
public enum Machine: Int, CaseIterable, Hashable, Sendable {
    case kick = 0, snare, hat, clap, tom, synth, fm, sample
    public var isMelodic: Bool { self == .synth || self == .fm || self == .sample }
}

public enum TrigAction: Hashable, Sendable {
    case togglePlay, restart, toggleRecord, tapTempo
    case fill(Bool)
    case tempo(Double), swing(Int), nudge(Double)
    case selectTrack(Int), page(TrigPage), stepPage(Int)
    case stepSet(track: Int, step: Int, on: Bool)
    case stepLock(track: Int, step: Int, param: Int, value: Int)
    case stepMicro(track: Int, step: Int, micro: Int)
    case stepCond(track: Int, step: Int, cond: Int)
    case paramSet(track: Int, param: Int, value: Int)
    case globalSet(slot: Int, value: Int)
    case machine(track: Int, machine: Int)
    case trigger(track: Int, note: Int, velocity: Int)
    case release(track: Int, note: Int)
    case mute(track: Int, on: Bool)
    case queuePattern(Int), copyPattern(from: Int, to: Int)
    case sceneSet(scene: Int, track: Int, param: Int, value: Int)
    case sceneClear(Int)
    case crossfader(Double)
    case deckLevel(deck: Int, level: Double), deckDrive(deck: Int, drive: Double)
    case scratch(deck: Int, touching: Bool, rate: Double)
    case perf(fx: Int, division: Int, target: Int, on: Bool, amount: Double)
    // after the FM-1 community firmwares: resampling (Veffbox), generative (Torso T-1 port), acid (X0X)
    case capture(deck: Int, bars: Int)
    case euclid(track: Int, pulses: Int, steps: Int)
    case rotate(track: Int, amount: Int)
    case dice(track: Int, amount: Int)
    case stepSlide(track: Int, step: Int, on: Bool)
    case stepAccent(track: Int, step: Int, on: Bool)
    // after DUB-1: the arranger that keeps the pattern evolving in 8-bar phrases
    case autopilot(Bool)
    /// a sound pack's kit into the current pattern (its steps stay); revert: the kit it replaced
    case loadPack(Int), revertPack
    /// a track's length (1 ... 64 steps); new steps repeat the old ones
    case length(track: Int, steps: Int)
}

/// A parameter as the engine describes it (td_param_info).
public struct ParamInfo: Hashable, Sendable {
    public var name: String, shortName: String
    public var min: Int, max: Int, def: Int
    public var lockable: Bool
    public init(name: String, shortName: String, min: Int, max: Int, def: Int, lockable: Bool = true) {
        self.name = name; self.shortName = shortName; self.min = min; self.max = max; self.def = def; self.lockable = lockable
    }
}

/// What the mapper reads from the engine. The app implements it over TrigCore; tests use a fake.
public protocol TrigModel: AnyObject {
    var playing: Bool { get }
    var recording: Bool { get }
    var pattern: Int { get }
    var queuedPattern: Int? { get }
    var bpm: Double { get }
    var swing: Int { get }
    var autopilot: Bool { get }
    func machine(_ track: Int) -> Machine
    func muted(_ track: Int) -> Bool
    func length(_ track: Int) -> Int
    func playingStep(_ track: Int) -> Int?                 // nil: stopped
    func info(track: Int, param: Int) -> ParamInfo?        // param = page * 8 + slot
    func globalInfo(slot: Int) -> ParamInfo?
    func value(track: Int, param: Int) -> Int              // base value
    func liveValue(track: Int, param: Int) -> Int          // after locks, scenes, LFO
    func globalValue(slot: Int) -> Int
    func format(_ info: ParamInfo, _ value: Int) -> String
    func step(track: Int, step: Int) -> StepView
    func lock(track: Int, step: Int, param: Int) -> Int?
    func scene(_ scene: Int, track: Int, param: Int) -> Int?
    func condName(_ cond: Int) -> String
    var condCount: Int { get }
    func machineName(_ m: Int) -> String
    func trackLevel(_ track: Int) -> Double                // output level 0...1
    /// the sound packs' names (kits for all 8 tracks), in the engine's order
    var packNames: [String] { get }
}

public extension TrigModel {
    var packNames: [String] { [] }
}

public struct StepView: Hashable, Sendable {
    public var trig: Bool, note: Int, velocity: Int, cond: Int, retrig: Int, micro: Int, locks: Int
    public var slide: Bool, accent: Bool
    public init(trig: Bool = false, note: Int = 60, velocity: Int = 100, cond: Int = 0, retrig: Int = 0, micro: Int = 0,
                locks: Int = 0, slide: Bool = false, accent: Bool = false) {
        self.trig = trig; self.note = note; self.velocity = velocity; self.cond = cond; self.retrig = retrig
        self.micro = micro; self.locks = locks; self.slide = slide; self.accent = accent
    }
}

/// What a control shows on the augmented controller: its job, its value, an arc (knobs, faders), its colour, its light.
public struct ControlFace: Hashable, Sendable {
    public var title: String                 // "CUTOFF", "SNARE", "TRIG", "13"
    public var value: String?                // "2.4 kHz", "50%"
    public var arc: Double?                  // 0...1: a knob's or fader's position to draw
    public var color: UInt32?                // ARGB: the knob row's colour, the track's colour
    public var light: LightLevel
    public var badge: String?                // a pad's lock dot / condition / retrig ("•", "1:2", "x3")
    public var outlined: Bool                // the playhead, the held step
    public init(title: String, value: String? = nil, arc: Double? = nil, color: UInt32? = nil, light: LightLevel = .off,
                badge: String? = nil, outlined: Bool = false) {
        self.title = title; self.value = value; self.arc = arc; self.color = color; self.light = light
        self.badge = badge; self.outlined = outlined
    }
}

/// Teenage Engineering-style fixed colours: the knob rows (HI blue, MID green, LOW white, FILTER orange), the tracks.
public enum TrigColors {
    public static let knobRow: [UInt32] = [0xFF2E7CFF, 0xFF1ECC70, 0xFFE8E8EC, 0xFFFF621A]
    public static let track: [UInt32] = [0xFFFF4A3D, 0xFFFF9F1C, 0xFFFFD60A, 0xFF7CDB3C,   // deck A: warm
                                         0xFF2EC4F2, 0xFF4D7CFF, 0xFFA66BFF, 0xFFFF5FB4]   // deck B: cool
    public static let page: [UInt32] = [0xFFF2F2F2, 0xFFFFD60A, 0xFF2EC4F2, 0xFF7CDB3C, 0xFFA66BFF, 0xFFFF9F1C, 0xFFFF4A3D]
    public static let sceneA: UInt32 = 0xFF2E7CFF
    public static let sceneB: UInt32 = 0xFFFF621A
}
