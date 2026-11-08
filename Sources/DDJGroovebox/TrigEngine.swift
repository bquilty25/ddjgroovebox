// SPDX-License-Identifier: GPL-3.0-only
// TrigCore (the TRIGDECK engine, C) for the app: the model the mapper reads (TrigModel), the actions it asks for
// (TrigAction -> C calls), MIDI and the project file.
import DDJMapping
import Foundation
import TrigCore

final class TrigEngine: TrigModel, InstrumentIO, @unchecked Sendable {
    private(set) var state = td_state_t()
    private var stepCache: [Int: [StepView]] = [:]
    private var infoCache: [Int: (ParamInfo, td_param_info_t)] = [:]          // (machine << 8 | param)
    private var globalCache: [Int: (ParamInfo, td_param_info_t)] = [:]
    private var nextCaptureSlot = 4
    let projectURL: URL
    private var dirty = false
    private var lastSave = Date()

    init(projectURL: URL) {
        self.projectURL = projectURL
        td_init()
        if let data = try? Data(contentsOf: projectURL) {
            let ok = data.withUnsafeBytes { td_project_load($0.baseAddress, data.count) } == 0
            if !ok {                                     // a damaged or foreign file: kept aside, never overwritten
                try? FileManager.default.moveItem(at: projectURL, to: projectURL.appendingPathExtension("damaged"))
            }
        }
        refresh()
    }

    static var defaultProjectURL: URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first!
        return base.appendingPathComponent("DDJGroovebox", isDirectory: true).appendingPathComponent("trigdeck.project")
    }

    /// read the engine's state (once per UI tick)
    func refresh() {
        td_get_state(&state)
        stepCache = [:]
    }

    // MARK: TrigModel

    var playing: Bool { state.playing != 0 }
    var recording: Bool { state.recording != 0 }
    var pattern: Int { Int(state.pattern) }
    func patternName(_ p: Int) -> String { String(cString: td_pattern_name(Int32(p))) }
    var queuedPattern: Int? { state.queued_pattern >= 0 && state.queued_pattern != state.pattern ? Int(state.queued_pattern) : nil }
    var bpm: Double { Double(state.bpm) }
    var swing: Int { Int(state.swing) }
    var autopilot: Bool { state.autopilot != 0 }
    let packNames: [String] = (0..<td_pack_count()).map { String(cString: td_pack_name($0)) }

    private func tuple<T>(_ t: T, _ i: Int) -> Int32 {
        withUnsafeBytes(of: t) { $0.bindMemory(to: Int32.self)[i] }
    }
    func machine(_ t: Int) -> Machine { Machine(rawValue: Int(tuple(state.machine, t))) ?? .kick }
    func muted(_ t: Int) -> Bool { tuple(state.muted, t) != 0 }
    func length(_ t: Int) -> Int { max(1, Int(tuple(state.length, t))) }
    func playingStep(_ t: Int) -> Int? { let s = Int(tuple(state.step, t)); return s >= 0 ? s : nil }
    func trackLevel(_ t: Int) -> Double { withUnsafeBytes(of: state.level) { Double($0.bindMemory(to: Float.self)[t]) } }
    func deckLevel(_ d: Int) -> Double { withUnsafeBytes(of: state.deck_level) { Double($0.bindMemory(to: Float.self)[d]) } }

    private func describe(_ c: td_param_info_t) -> ParamInfo {
        ParamInfo(name: Self.string(c.name), shortName: Self.string(c.short_name), min: Int(c.min), max: Int(c.max),
                  def: Int(c.def), lockable: c.lockable != 0)
    }
    static func string<T>(_ t: T) -> String {
        withUnsafeBytes(of: t) { String(cString: $0.bindMemory(to: CChar.self).baseAddress!) }
    }

    private func cInfo(track: Int, param: Int) -> (ParamInfo, td_param_info_t)? {
        let key = machine(track).rawValue << 8 | param
        if let c = infoCache[key] { return c }
        var c = td_param_info_t()
        guard td_param_info(Int32(track), Int32(param), &c) != 0 else { return nil }
        let r = (describe(c), c)
        infoCache[key] = r
        return r
    }
    func info(track: Int, param: Int) -> ParamInfo? { cInfo(track: track, param: param)?.0 }
    func globalInfo(slot: Int) -> ParamInfo? {
        if let c = globalCache[slot] { return c.0 }
        var c = td_param_info_t()
        guard td_global_info(Int32(slot), &c) != 0 else { return nil }
        globalCache[slot] = (describe(c), c)
        return globalCache[slot]?.0
    }
    func value(track: Int, param: Int) -> Int { Int(td_param_get(Int32(track), Int32(param))) }
    func liveValue(track: Int, param: Int) -> Int { Int(td_param_live(Int32(track), Int32(param))) }
    func globalValue(slot: Int) -> Int { Int(td_global_get(Int32(slot))) }

    func format(_ info: ParamInfo, _ value: Int) -> String {
        // the C description of this parameter (by its name: the app formats what the mapper shows)
        let c = infoCache.values.first { $0.0 == info }?.1 ?? globalCache.values.first { $0.0 == info }?.1
        guard var c else { return "\(value)" }
        var buf = [CChar](repeating: 0, count: 24)
        td_format_value(&c, Int32(value), &buf, 24)
        return String(cString: buf)
    }

    func step(track: Int, step: Int) -> StepView {
        if stepCache[track] == nil {
            var raw = [td_step_view_t](repeating: td_step_view_t(), count: Int(TD_STEPS))
            td_steps_get(Int32(track), &raw)
            stepCache[track] = raw.map {
                StepView(trig: $0.trig != 0, note: Int($0.note), velocity: Int($0.velocity), cond: Int($0.cond),
                         retrig: Int($0.retrig), micro: Int($0.micro), locks: Int($0.nlocks),
                         slide: $0.slide != 0, accent: $0.accent != 0)
            }
        }
        return stepCache[track]![min(Int(TD_STEPS) - 1, max(0, step))]
    }
    func lock(track: Int, step: Int, param: Int) -> Int? {
        var v: Int32 = 0
        return td_step_lock_get(Int32(track), Int32(step), Int32(param), &v) != 0 ? Int(v) : nil
    }
    func scene(_ s: Int, track: Int, param: Int) -> Int? {
        var v: Int32 = 0
        return td_scene_get(Int32(s), Int32(track), Int32(param), &v) != 0 ? Int(v) : nil
    }
    func condName(_ c: Int) -> String { String(cString: td_cond_name(Int32(c))) }
    var condCount: Int { Int(TD_CONDS.rawValue) }
    func machineName(_ m: Int) -> String { String(cString: td_machine_name(Int32(m))) }

    // MARK: actions

    func apply(_ actions: [TrigAction]) {
        guard !actions.isEmpty else { return }
        for a in actions { apply(a) }
        dirty = true
        stepCache = [:]
        td_get_state(&state)
    }

    private func apply(_ a: TrigAction) {
        func i(_ x: Int) -> Int32 { Int32(x) }
        switch a {
        case .togglePlay: td_play(state.playing != 0 ? 0 : 1)
        case .restart: td_restart()
        case .toggleRecord: td_record(state.recording != 0 ? 0 : 1)
        case .tapTempo: td_tap_tempo()
        case let .fill(on): td_fill(on ? 1 : 0)
        case let .tempo(b): td_set_tempo(Float(b))
        case let .swing(s): td_set_swing(i(s))
        case let .nudge(n): td_nudge(Float(n))
        case .selectTrack, .page, .stepPage: break       // (the mapper's own state)
        case let .stepSet(t, s, on): td_step_set(i(t), i(s), on ? 1 : 0)
        case let .stepLock(t, s, p, v): td_step_lock(i(t), i(s), i(p), i(v))
        case let .stepMicro(t, s, m): td_step_micro(i(t), i(s), i(m))
        case let .stepCond(t, s, c): td_step_cond(i(t), i(s), i(c))
        case let .paramSet(t, p, v): td_param_set(i(t), i(p), i(v))
        case let .globalSet(s, v): td_global_set(i(s), i(v))
        case let .machine(t, m): td_set_machine(i(t), i(m)); infoCache = [:]
        case let .trigger(t, n, v): td_trigger(i(t), i(n), i(v))
        case let .release(t, n): td_release(i(t), i(n))
        case let .mute(t, on): td_mute(i(t), on ? 1 : 0)
        case let .queuePattern(p): td_pattern_queue(i(p))
        case let .copyPattern(f, t): td_pattern_copy(i(f), i(t))
        case let .sceneSet(s, t, p, v): td_scene_set(i(s), i(t), i(p), i(v))
        case let .sceneClear(s): td_scene_clear(i(s))
        case let .crossfader(x): td_crossfader(Float(x))
        case let .deckLevel(d, l): td_deck_level(i(d), Float(l))
        case let .deckDrive(d, v): td_deck_drive(i(d), Float(v))
        case let .scratch(d, touching, rate): td_scratch(i(d), touching ? 1 : 0, Float(rate))
        case let .perf(fx, div, target, on, amount): td_perf(i(fx), i(div), i(target), on ? 1 : 0, Float(amount))
        case let .capture(d, bars):
            let slot = nextCaptureSlot
            nextCaptureSlot = nextCaptureSlot >= Int(TD_SAMPLE_SLOTS) - 1 ? 4 : nextCaptureSlot + 1
            if td_capture(i(d), i(bars), i(slot)) == 0 {
                lastCapture = (deck: d, slot: slot)
            }
        case let .euclid(t, p, s): td_euclid(i(t), i(p), i(s), 0)
        case let .rotate(t, n): td_rotate(i(t), i(n))
        case let .dice(t, n): td_dice(i(t), i(n))
        case let .stepSlide(t, s, on): td_step_slide(i(t), i(s), on ? 1 : 0)
        case let .stepAccent(t, s, on): td_step_accent(i(t), i(s), on ? 1 : 0)
        case let .autopilot(on): td_autopilot(on ? 1 : 0)
        case let .loadPack(p): td_pack_load(i(p)); infoCache = [:]       // (machines change: their parameters' names too)
        case .revertPack: _ = td_pack_revert(); infoCache = [:]
        case let .length(t, n): td_set_length_repeat(i(t), i(n))
        }
    }

    /// the last resample (the instrument points a SAMPLE track at it)
    var lastCapture: (deck: Int, slot: Int)?

    // MARK: InstrumentIO

    func midiIn(_ m: MIDIMessage) {}

    func drainMIDI(outputLatency: Double, _ send: ([UInt8], UInt64) -> Void) {
        var b: (UInt8, UInt8, UInt8) = (0, 0, 0)
        var n: Int32 = 0
        var frames: Int64 = 0
        var anchor: UInt64 = 0
        let ticksPerFrame = AudioOutput.ticksPerSecond / Double(TD_SAMPLE_RATE)
        let latency = UInt64(max(0, outputLatency) * AudioOutput.ticksPerSecond)
        while withUnsafeMutableBytes(of: &b, { td_midi_out($0.baseAddress!.assumingMemoryBound(to: UInt8.self), &n, &frames, &anchor) }) != 0 {
            let at = anchor == 0 ? 0 : UInt64(max(0, Double(anchor) + Double(frames) * ticksPerFrame)) &+ latency
            send(Array([b.0, b.1, b.2].prefix(Int(n))), at)
        }
    }

    // MARK: the project file

    /// A new project (the default patterns): the current one is first kept beside it, named by the date and time.
    /// Returns the backup's URL (nil if there was nothing to keep).
    @discardableResult
    func newProject() -> URL? {
        saveIfDirty(force: true)
        var backup: URL?
        if FileManager.default.fileExists(atPath: projectURL.path) {
            let f = DateFormatter()
            f.dateFormat = "yyyy-MM-dd HHmmss"
            let url = projectURL.deletingLastPathComponent()
                .appendingPathComponent("trigdeck \(f.string(from: Date())).project")
            if (try? FileManager.default.copyItem(at: projectURL, to: url)) != nil { backup = url }
        }
        td_project_new()
        infoCache = [:]
        saveIfDirty(force: true)
        return backup
    }

    func saveIfDirty(force: Bool = false) {
        guard dirty || force, force || Date().timeIntervalSince(lastSave) > 5 else { return }
        lastSave = Date()
        dirty = false
        var buf = [UInt8](repeating: 0, count: td_project_size())
        let n = buf.withUnsafeMutableBytes { td_project_save($0.baseAddress, $0.count) }
        guard n > 0 else { return }
        let data = Data(buf.prefix(Int(n)))
        let url = projectURL
        try? FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try? data.write(to: url, options: .atomic)
    }
}
