// SPDX-License-Identifier: GPL-3.0-only
// TrigMapper (TRIGDECK on the DDJ-400) against a fake engine model.
import DDJMapping
import XCTest

final class FakeTrig: TrigModel {
    var playing = false, recording = false, pattern = 0
    var queuedPattern: Int?
    var bpm = 120.0, swing = 50
    var autopilot = false
    var machines: [Machine] = [.kick, .snare, .hat, .clap, .synth, .fm, .sample, .tom]
    var mutes = Set<Int>()
    var lengths = Array(repeating: 16, count: 8)
    var playhead: Int?
    var values: [Int: Int] = [:]                 // track * 100 + param
    var globals: [Int: Int] = [:]
    var steps: [Int: StepView] = [:]             // track * 100 + step
    var locks: [Int: Int] = [:]                  // (track * 100 + step) * 100 + param
    var scenes: [Int: Int] = [:]                 // scene * 10000 + track * 100 + param

    func machine(_ t: Int) -> Machine { machines[t] }
    func muted(_ t: Int) -> Bool { mutes.contains(t) }
    func length(_ t: Int) -> Int { lengths[t] }
    func playingStep(_ t: Int) -> Int? { playhead }
    func info(track: Int, param: Int) -> ParamInfo? {
        param == 21 ? ParamInfo(name: "TYPE", shortName: "TYPE", min: 0, max: 2, def: 0)
                    : ParamInfo(name: "P\(param)", shortName: "P\(param)", min: 0, max: 127, def: 64, lockable: param != 9)
    }
    func globalInfo(slot: Int) -> ParamInfo? { ParamInfo(name: "G\(slot)", shortName: "G\(slot)", min: 0, max: 100, def: 50) }
    func value(track: Int, param: Int) -> Int { values[track * 100 + param] ?? 64 }
    func liveValue(track: Int, param: Int) -> Int { value(track: track, param: param) }
    func globalValue(slot: Int) -> Int { globals[slot] ?? 50 }
    var packNames: [String] { ["808", "909", "KEYS"] }
    func format(_ i: ParamInfo, _ v: Int) -> String { "\(v)" }
    func step(track: Int, step: Int) -> StepView { steps[track * 100 + step] ?? StepView() }
    func lock(track: Int, step: Int, param: Int) -> Int? { locks[(track * 100 + step) * 100 + param] }
    func scene(_ s: Int, track: Int, param: Int) -> Int? { scenes[s * 10000 + track * 100 + param] }
    func condName(_ c: Int) -> String { c == 0 ? "ALWAYS" : "C\(c)" }
    var condCount: Int { 17 }
    func machineName(_ m: Int) -> String { ["KICK", "SNARE", "HAT", "CLAP", "TOM", "SYNTH", "FM", "SAMPLE"][m] }
    func trackLevel(_ t: Int) -> Double { 0 }

    /// apply the actions the way the engine would, so a sequence of events sees its own effects
    func apply(_ actions: [TrigAction]) {
        for a in actions {
            switch a {
            case let .stepSet(t, s, on): var v = step(track: t, step: s); v.trig = on; steps[t * 100 + s] = v
            case let .stepLock(t, s, p, v):
                locks[(t * 100 + s) * 100 + p] = v
                var sv = step(track: t, step: s); sv.locks += 1; steps[t * 100 + s] = sv
            case let .stepMicro(t, s, m): var v = step(track: t, step: s); v.micro = m; steps[t * 100 + s] = v
            case let .stepCond(t, s, c): var v = step(track: t, step: s); v.cond = c; steps[t * 100 + s] = v
            case let .paramSet(t, p, v): values[t * 100 + p] = v
            case let .sceneSet(s, t, p, v): scenes[s * 10000 + t * 100 + p] = v
            case let .mute(t, on): if on { mutes.insert(t) } else { mutes.remove(t) }
            case let .globalSet(s, v): globals[s] = v
            case let .tempo(b): bpm = b
            case let .swing(s): swing = s
            case let .machine(t, m): machines[t] = Machine(rawValue: m)!
            case let .stepSlide(t, s, on): var v = step(track: t, step: s); v.slide = on; steps[t * 100 + s] = v
            case let .stepAccent(t, s, on): var v = step(track: t, step: s); v.accent = on; steps[t * 100 + s] = v
            case let .autopilot(on): autopilot = on
            default: break
            }
        }
    }
}

final class TrigMapperTests: XCTestCase {
    var model: FakeTrig!
    var m: TrigMapper!
    var clock = 100.0

    override func setUp() {
        model = FakeTrig()
        m = TrigMapper(model: model)
        clock = 100
        m.now = { [unowned self] in self.clock }
    }

    @discardableResult
    func send(_ c: DDJControl, _ v: DDJValue, mode: PadMode? = nil, shifted: Bool = false) -> [TrigAction] {
        let a = m.handle(DDJEvent(c, v, padMode: mode, shifted: shifted))
        model.apply(a)
        return a
    }
    func tap(_ c: DDJControl, mode: PadMode? = nil, shifted: Bool = false) -> [TrigAction] {
        send(c, .press(true), mode: mode, shifted: shifted) + send(c, .press(false), mode: mode, shifted: shifted)
    }

    // MARK: steps

    func testTapSetsThenClearsAStep() {
        XCTAssertEqual(tap(.pad(.left, 4), mode: .hotCue), [.stepSet(track: 0, step: 4, on: true)])
        XCTAssertEqual(tap(.pad(.left, 4), mode: .hotCue), [.stepSet(track: 0, step: 4, on: false)])
        XCTAssertEqual(tap(.pad(.right, 0), mode: .hotCue), [.stepSet(track: 0, step: 8, on: true)], "right deck: steps 9-16")
    }

    func testHoldStepAndTurnKnobLocksWithoutClearing() {
        _ = tap(.pad(.left, 2), mode: .hotCue)                        // a trig on step 3
        send(.pad(.left, 2), .press(true), mode: .hotCue)
        send(.eqHi(.left), .absolute(0.5))                            // (reference)
        let a = send(.eqHi(.left), .absolute(0.6))                    // +0.1 of 127
        XCTAssertEqual(a, [.stepLock(track: 0, step: 2, param: TrigPage.src.rawValue * 8, value: 64 + 12)])
        XCTAssertEqual(send(.pad(.left, 2), .press(false), mode: .hotCue), [], "an edited step is not cleared on release")
        XCTAssertTrue(model.step(track: 0, step: 2).trig)
    }

    func testKnobWithoutHeldStepSetsTheBaseValue() {
        send(.cfx(.right), .absolute(0.2))
        XCTAssertEqual(send(.cfx(.right), .absolute(0.0)), [.paramSet(track: 0, param: 8 + 7, value: 64 - 25)])
    }

    func testFullTurnCoversASmallRangeExactly() {
        send(.loopIn(.left), .press(true))                            // TRIG page
        send(.reloop(.left), .press(true))                            // FLTR page (2): slot 5 = param 21 (0...2 in the fake)
        send(.eqMid(.right), .absolute(0))
        let a = send(.eqMid(.right), .absolute(1))
        XCTAssertEqual(a, [.paramSet(track: 0, param: 21, value: 2)], "the value clamps to the range")
    }

    func testJogMicroAndBrowseConditionOnHeldStep() {
        send(.pad(.right, 3), .press(true), mode: .hotCue)            // step 12 (set on press)
        clock += 0.01
        XCTAssertEqual(send(.jog(.left), .relative(8)), [.stepMicro(track: 0, step: 11, micro: 2)])
        XCTAssertEqual(send(.browse, .relative(1)), [.stepCond(track: 0, step: 11, cond: 1)])
        XCTAssertEqual(send(.browse, .relative(-2)), [.stepCond(track: 0, step: 11, cond: 16)], "wraps")
        send(.pad(.right, 3), .press(false), mode: .hotCue)
        XCTAssertTrue(model.step(track: 0, step: 11).trig)
    }

    func testStepPagesAndLength() {
        model.lengths[0] = 64
        send(.callRight(.right), .press(true))
        XCTAssertEqual(tap(.pad(.left, 0), mode: .hotCue), [.stepSet(track: 0, step: 16, on: true)])
        for _ in 0..<5 { send(.callRight(.right), .press(true)) }
        XCTAssertEqual(m.stepPage, 3, "clamped to the last page")
        model.lengths[0] = 16
        XCTAssertNil(m.face(.pad(.left, 0))?.title.nilIfEmpty, "beyond the track's length: blank")
    }

    // MARK: modes

    func testTracksModeSelectsAndMutes() {
        XCTAssertEqual(tap(.pad(.right, 1), mode: .beatJump), [.selectTrack(5)])
        XCTAssertEqual(m.track, 5)
        XCTAssertEqual(tap(.pad(.right, 5), mode: .beatJump), [.mute(track: 5, on: true)])
        XCTAssertEqual(tap(.pad(.right, 5), mode: .beatJump), [.mute(track: 5, on: false)])
        XCTAssertEqual(m.face(.pad(.right, 1))?.light, .on)
        XCTAssertEqual(m.padFunction(.right), .tracks)
        XCTAssertEqual(m.padFunction(.left), .steps, "each deck keeps its own mode")
    }

    func testKeysModeMelodicAndDrums() {
        send(.callRight(.left), .press(true)); send(.callRight(.left), .press(true))
        send(.callRight(.left), .press(true)); send(.callRight(.left), .press(true))   // track 5: SYNTH
        model.values[4 * 100 + 0] = 48
        XCTAssertEqual(send(.pad(.left, 4), .press(true), mode: .beatLoop), [.trigger(track: 4, note: 48, velocity: 100)])
        XCTAssertEqual(send(.pad(.left, 4), .press(false), mode: .beatLoop), [.release(track: 4, note: 48)])
        XCTAssertEqual(send(.pad(.right, 0), .press(true), mode: .beatLoop), [.trigger(track: 4, note: 48 + 12, velocity: 100)])
        // a drum track active: a deck's pads play its own 4 tracks
        send(.callLeft(.left), .press(true)); send(.callLeft(.left), .press(true))
        send(.callLeft(.left), .press(true)); send(.callLeft(.left), .press(true))
        XCTAssertEqual(send(.pad(.right, 6), .press(true), mode: .beatLoop), [.trigger(track: 6, note: 60, velocity: 55)])
    }

    func testKeysFacesForLowNotes() {
        // (a crash found on the first run: a bass track's notes below the FM-1 keyboard's F3)
        model.machines[0] = .synth
        model.values[0] = 33                                        // A1
        m.setPadMode(.beatLoop, deck: .left)
        XCTAssertEqual(m.face(.pad(.left, 4))?.title, "A1")
        model.values[0] = 0
        _ = m.padLights(for: .beatLoop)                             // every pad, every note: no crash
        XCTAssertEqual(FM1Keyboard.midiNoteName(60), "C4")
        XCTAssertEqual(FM1Keyboard.noteName(0), "F3")
    }

    func testPatternsModeQueuesAndCopies() {
        XCTAssertEqual(tap(.pad(.right, 2), mode: .sampler), [.queuePattern(10)])
        XCTAssertEqual(tap(.pad(.left, 7), mode: .sampler, shifted: true), [.copyPattern(from: 0, to: 7)])
        XCTAssertEqual(send(.load(.right), .press(true)), [.queuePattern(1)])
    }

    // MARK: pages, scenes, master

    func testPagesFromLoopButtons() {
        send(.loopOut(.right), .press(true))
        XCTAssertEqual(m.page, .fx)
        XCTAssertEqual(m.face(.loopOut(.right))?.light, .on)
        send(.sync(.right), .press(true))
        XCTAssertEqual(m.page, .master)
        send(.eqHi(.left), .absolute(0.5))
        XCTAssertEqual(send(.eqHi(.left), .absolute(0.6)), [.globalSet(slot: 0, value: 60)])
        send(.sync(.right), .press(true))
        XCTAssertEqual(m.page, .fx, "MASTER again: back to the page before")
    }

    func testSceneEditWhileCueHeldAndClear() {
        send(.headphoneCue(.right), .press(true))
        send(.eqLow(.left), .absolute(0.5))
        XCTAssertEqual(send(.eqLow(.left), .absolute(0.4)), [.sceneSet(scene: 1, track: 0, param: 10, value: 64 - 12)],
                       "-12.7: 12 now, the 0.7 waits for the next movement")
        XCTAssertEqual(m.face(.eqLow(.left))?.badge, "B")
        send(.headphoneCue(.right), .press(false))
        XCTAssertNil(m.sceneEdit)
        XCTAssertEqual(send(.headphoneCue(.left), .press(true), shifted: true), [.sceneClear(0)])
        XCTAssertEqual(send(.crossfader, .absolute(0.25)), [.crossfader(0.25)])
    }

    // MARK: jogs, tempo, perf FX

    func testScratchFollowsPlatterAndHoldsStill() {
        XCTAssertEqual(send(.jogTouch(.left), .press(true)), [.scratch(deck: 0, touching: true, rate: 0)])
        clock += 0.01
        let a = send(.jog(.left), .relative(4))                       // 400 ticks/s = normal speed
        guard case let .scratch(0, true, rate)? = a.first else { return XCTFail("\(a)") }
        XCTAssertEqual(rate, 0.5, accuracy: 0.01, "smoothed towards 1.0")
        clock += 0.2
        XCTAssertEqual(m.tick(), [.scratch(deck: 0, touching: true, rate: 0)], "no movement: held still")
        XCTAssertEqual(send(.jogTouch(.left), .press(false)), [.scratch(deck: 0, touching: false, rate: 0)])
    }

    func testJogSideIsADialAroundTheDecksTracks() {
        clock += 0.01
        XCTAssertEqual(m.jogDialPosition(.left), 0, "the marker starts on track 1's centre")
        XCTAssertEqual(send(.jog(.left), .relative(90)), [], "a quarter turn is a track: to the quadrant's edge, nothing yet")
        XCTAssertEqual(m.jogDialPosition(.left), 0.5)
        XCTAssertEqual(send(.jog(.left), .relative(6)), [], "the edge (in the gap between quadrants): still nothing")
        XCTAssertEqual(send(.jog(.left), .relative(1)), [.selectTrack(1)], "past it: the next quadrant's track")
        XCTAssertEqual(m.jogDialPosition(.left), 1 + (97.0 - 180) / 180, accuracy: 1e-9, "the marker did not jump")
        XCTAssertEqual(send(.jog(.left), .relative(-5)), [], "back a little: no flicker at the edge")
        XCTAssertEqual(send(.jog(.left), .relative(-10)), [.selectTrack(0)], "back past the edge: track 1 again")
        XCTAssertEqual(send(.jog(.left), .relative(-180)), [.selectTrack(3)], "around the ring: 1 -> 4")
        XCTAssertEqual(m.jogDialPosition(.left), 3 + 82.0 / 180, accuracy: 1e-9, "a whole quarter turn back, same place")
        // a track chosen another way: the marker goes to its quadrant's centre
        _ = m.select(track: 2)
        XCTAssertEqual(m.jogDialPosition(.left), 2)
        // the right jog: deck B's tracks; while the active track is on deck A, a short turn brings it to deck B
        XCTAssertEqual(send(.jog(.right), .relative(10)), [])
        XCTAssertEqual(send(.jog(.right), .relative(14)), [.selectTrack(4)])
        XCTAssertEqual(m.face(.jog(.right))?.value, "5 SYNTH")
        XCTAssertEqual(m.face(.jog(.left))?.value, "TURN: TRACK")
        XCTAssertEqual(m.jogDialPosition(.left), 2, "deck A's marker stays where it was left")
    }

    func testPacksOnTheMasterPage() {
        send(.sync(.right), .press(true)); send(.sync(.right), .press(false))   // the MASTER page
        XCTAssertEqual(m.page, .master)
        XCTAssertEqual(m.face(.browse)?.title, "PACK")
        XCTAssertEqual(m.face(.browse)?.value, "808")
        XCTAssertEqual(send(.browse, .relative(1)), [], "turning only chooses")
        XCTAssertEqual(m.face(.browse)?.value, "909")
        XCTAssertEqual(send(.browse, .relative(-2)), [])
        XCTAssertEqual(m.face(.browse)?.value, "KEYS", "around the list")
        XCTAssertEqual(m.face(.browsePress)?.title, "LOAD PACK")
        XCTAssertEqual(send(.browsePress, .press(true)), [.loadPack(2)])
        XCTAssertEqual(send(.browsePress, .press(false)), [])
        XCTAssertEqual(m.packLoaded?.index, 2)
        XCTAssertEqual(send(.browsePress, .press(true), shifted: true), [.revertPack])
        XCTAssertNil(m.packLoaded)
        // other pages: BROWSE is the track again
        send(.sync(.right), .press(true)); send(.sync(.right), .press(false))
        XCTAssertNotEqual(m.page, .master)
        XCTAssertEqual(m.face(.browse)?.title == "PACK", false)
    }

    func testShiftJogNudgesAndReleases() {
        clock += 0.01
        XCTAssertEqual(send(.jog(.right), .relative(3), shifted: true), [.nudge(0.5)])
        clock += 0.2
        XCTAssertEqual(m.tick(), [.nudge(0)])
    }

    func testTempoSoftTakeover() {
        XCTAssertEqual(send(.tempo(.left), .absolute(1.0)), [], "200 BPM is far from 120: no jump")
        let v = (120.0 - 60) / 140
        XCTAssertEqual(send(.tempo(.left), .absolute(v)), [.tempo(120)])
        XCTAssertEqual(send(.tempo(.left), .absolute(1.0)), [.tempo(200)], "picked up: towards you is faster")
    }

    func testPerfFXToggleDivisionTarget() {
        send(.fxSelect, .press(true))                                // STUTTER
        send(.beatRight, .press(true))                               // 1/4
        send(.fxChannel(0), .press(true))                            // deck A
        XCTAssertEqual(send(.fxOnOff, .press(true)), [.perf(fx: 1, division: 3, target: 0, on: true, amount: 0.6)])
        XCTAssertEqual(send(.fxOnOff, .press(false)), [], "a toggle: letting go keeps it on")
        XCTAssertEqual(send(.fxLevel, .absolute(0.9)), [.perf(fx: 1, division: 3, target: 0, on: true, amount: 0.9)])
        XCTAssertEqual(send(.fxOnOff, .press(true)), [.perf(fx: 1, division: 3, target: 0, on: false, amount: 0.9)], "pressed again: off")
        XCTAssertEqual(send(.fxOnOff, .press(false)), [])
    }

    func testMachineFromBrowseOnSrcPage() {
        XCTAssertEqual(send(.browse, .relative(1)), [.machine(track: 0, machine: 1)])
        send(.loopIn(.left), .press(true))                           // TRIG page: BROWSE selects tracks
        XCTAssertEqual(send(.browse, .relative(2)), [.selectTrack(2)])
    }

    // MARK: faces and lights

    func testFacesShowWhatTheControlsDo() {
        model.steps[0 * 100 + 3] = StepView(trig: true, cond: 5, retrig: 2, locks: 1)
        model.playhead = 3
        let f = m.face(.pad(.left, 3))!
        XCTAssertEqual(f.title, "4")
        XCTAssertEqual(f.light, .on)
        XCTAssertEqual(f.badge, "• C5 x3")
        XCTAssertTrue(f.outlined)
        XCTAssertEqual(f.color, TrigColors.track[0])
        let k = m.face(.eqHi(.right))!
        XCTAssertEqual(k.title, "P12")
        XCTAssertEqual(k.color, TrigColors.knobRow[0], "HI is blue on both channels")
        XCTAssertEqual(k.arc!, 64.0 / 127, accuracy: 1e-9)
        XCTAssertEqual(m.face(.loopOut(.left))?.light, .on, "SRC is the page at start")
        XCTAssertEqual(m.lights()[.pad(.left, 3)], .on)
        XCTAssertEqual(m.padLights(for: .beatJump)[0][0], .on, "TRACKS mode: the active track lit")
    }

    func testHeldStepShowsLockOnKnobs() {
        model.locks[(0 * 100 + 6) * 100 + 8] = 99
        model.steps[6] = StepView(trig: true, locks: 1)
        send(.pad(.left, 6), .press(true), mode: .hotCue)
        let k = m.face(.eqHi(.left))!
        XCTAssertEqual(k.value, "99")
        XCTAssertEqual(k.badge, "LOCK")
        XCTAssertEqual(m.header.heldSteps, [6])
    }

    // MARK: after the FM-1 community firmwares

    func testShiftTouchCapturesTheDeck() {
        XCTAssertEqual(send(.jogTouch(.right), .press(true), shifted: true), [.capture(deck: 1, bars: 1)])
        XCTAssertEqual(send(.jogTouch(.right), .press(false), shifted: true), [], "no scratch was started")
    }

    func testHeldTrackPadRotatesAndMakesEuclid() {
        model.steps[100 + 0] = StepView(trig: true)
        model.steps[100 + 8] = StepView(trig: true)
        send(.pad(.left, 1), .press(true), mode: .beatJump)          // hold track 2
        XCTAssertEqual(m.face(.browse)?.title, "EUCLID")
        XCTAssertEqual(m.face(.browse)?.value, "2/16")
        XCTAssertEqual(send(.browse, .relative(3)), [.euclid(track: 1, pulses: 5, steps: 16)])
        XCTAssertEqual(send(.jog(.left), .relative(7)), [], "less than 8 ticks: nothing yet")
        XCTAssertEqual(send(.jog(.left), .relative(10)), [.rotate(track: 1, amount: 2)])
        XCTAssertEqual(send(.browsePress, .press(true)), [.dice(track: 1, amount: 25)])
        send(.pad(.left, 1), .press(false), mode: .beatJump)
        XCTAssertEqual(m.face(.browse)?.title, "MACHINE", "let go: BROWSE is back to its page job")
    }

    func testSlideAndAccentOnHeldSteps() {
        send(.pad(.left, 0), .press(true), mode: .hotCue)
        XCTAssertEqual(m.face(.load(.left))?.title, "SLIDE")
        XCTAssertEqual(send(.load(.left), .press(true)), [.stepSlide(track: 0, step: 0, on: true)])
        XCTAssertEqual(send(.load(.right), .press(true)), [.stepAccent(track: 0, step: 0, on: true)])
        send(.pad(.left, 0), .press(false), mode: .hotCue)
        XCTAssertEqual(m.face(.pad(.left, 0))?.badge, "~ !")
        XCTAssertTrue(model.step(track: 0, step: 0).trig, "edited while held: kept")
    }

    func testShiftFillTogglesAutopilot() {
        XCTAssertEqual(send(.sync(.left), .press(true), shifted: true), [.autopilot(true)])
        XCTAssertEqual(send(.sync(.left), .press(false), shifted: true), [], "a SHIFT press does not hold FILL")
        XCTAssertEqual(m.face(.sync(.left))?.value, "AUTOPILOT")
        XCTAssertEqual(send(.sync(.left), .press(true), shifted: true), [.autopilot(false)])
    }

    // MARK: lights (the user: "pads don't light up when active")

    func testAHeldPadLightsInEveryMode() {
        send(.pad(.right, 2), .press(true), mode: .beatLoop)         // KEYS: dim at rest ...
        XCTAssertEqual(m.face(.pad(.right, 2))?.light, .on, "... lit while held")
        send(.pad(.right, 2), .press(false), mode: .beatLoop)
        XCTAssertEqual(m.face(.pad(.right, 2))?.light, .dim)
        send(.pad(.left, 6), .press(true), mode: .beatJump)          // TRACKS: a mute pad
        XCTAssertEqual(m.padLights(for: .beatJump)[0][6], .on)
    }

    func testRunningLightOnTheHardware() {
        model.steps[0 * 100 + 2] = StepView(trig: true)
        model.playhead = 2
        XCTAssertEqual(m.padLights(for: .hotCue)[0][2], .off, "a trig under the playhead: flips dark")
        model.playhead = 3
        XCTAssertEqual(m.padLights(for: .hotCue)[0][3], .on, "an empty step under the playhead: lit")
        XCTAssertEqual(m.padLights(for: .hotCue)[0][2], .on)
        XCTAssertEqual(m.face(.pad(.left, 3))?.light, .off, "on screen: an outline, not a light")
        XCTAssertEqual(m.face(.pad(.left, 3))?.outlined, true)
    }

    func testLEDWriterBudgetAndOrder() {
        var w = DDJ400LEDWriter()
        w.maxPerCall = 24
        model.steps[0 * 100 + 0] = StepView(trig: true)
        let first = w.messages(mapper: m, meters: (0, 0)).filter { $0.status != 0xB0 && $0.status != 0xB1 }
        XCTAssertEqual(first.count, 24, "a refresh goes out 24 at a time")
        XCTAssertTrue(first.allSatisfy { $0.status < 0x97 }, "buttons first")
        var all = first
        for _ in 0..<40 { all += w.messages(mapper: m, meters: (0, 0)).filter { $0.status != 0xB0 && $0.status != 0xB1 } }
        XCTAssertTrue(all.contains(MIDIMessage(0x97, 0x00, 0x7F)), "the trig's pad (HOT CUE) is lit in the end")
        let hotCueAt = all.firstIndex { $0.status == 0x97 && $0.data1 < 0x08 }!
        let otherAt = all.firstIndex { $0.status == 0x97 && $0.data1 >= 0x60 }!
        XCTAssertLessThan(hotCueAt, otherAt, "the current mode's pads before the other modes'")
        XCTAssertEqual(w.messages(mapper: m, meters: (0, 0)).filter { $0.status >= 0x90 && $0.status != 0xB0 }, [], "nothing left")
    }

    func testReleaseAllLetsGo() {
        send(.sync(.left), .press(true))
        send(.fxOnOff, .press(true))
        send(.jogTouch(.right), .press(true))
        send(.pad(.left, 0), .press(true), mode: .beatLoop)
        let a = m.releaseAll()
        XCTAssertTrue(a.contains(.fill(false)))
        XCTAssertTrue(a.contains(.scratch(deck: 1, touching: false, rate: 0)))
        XCTAssertTrue(a.contains(.release(track: 0, note: 60)))
        XCTAssertTrue(a.contains { if case .perf(_, _, _, false, _) = $0 { return true }; return false })
    }
}

private extension String { var nilIfEmpty: String? { isEmpty ? nil : self } }
