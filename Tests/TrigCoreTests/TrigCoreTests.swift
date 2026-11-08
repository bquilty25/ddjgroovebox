// SPDX-License-Identifier: GPL-3.0-only
// TRIGDECK engine tests through its C API, as the app uses it. The deeper engine tests (timing to the frame across
// tempos, every machine at every parameter's extremes, scratch, performance FX, DUB HOLD, AUTOPILOT, CPU, threads under
// ThreadSanitizer) are C programs in Tests/c (trigcore_test.c, trigcore_tsan.c).
import DDJMapping
import TrigCore
import XCTest

private func p(_ page: td_page_t, _ slot: Int32) -> Int32 { Int32(page.rawValue) * TD_SLOTS + slot }
private let nparams = Int32(TD_PAGES.rawValue) * TD_SLOTS     // TD_NPARAMS (a macro Swift cannot import)
private func m(_ machine: td_machine_t) -> Int32 { Int32(machine.rawValue) }
private func info(_ track: Int32, _ param: Int32) -> td_param_info_t {
    var i = td_param_info_t()
    XCTAssertEqual(td_param_info(track, param, &i), 1)
    return i
}
private func pname(_ i: td_param_info_t) -> String {
    withUnsafeBytes(of: i.name) { String(decoding: $0.prefix { $0 != 0 }, as: UTF8.self) }
}
private func format(_ i: td_param_info_t, _ v: Int32) -> String {
    var i = i
    var buf = [CChar](repeating: 0, count: 32)
    td_format_value(&i, v, &buf, 32)
    return String(cString: buf)
}
private func render(_ frames: Int, chunk: Int = 512) -> [Float] {
    var out = [Float](repeating: 0, count: frames * 2)
    var pos = 0
    while pos < frames {
        let n = min(chunk, frames - pos)
        out.withUnsafeMutableBufferPointer { td_render($0.baseAddress! + pos * 2, UInt32(n), 0) }
        pos += n
    }
    return out
}
private func emptyPattern(bpm: Float = 120) {
    td_init()
    for p: Int32 in [15, 14, 13, 12] {                         // P13..P16 made empty for the tests: P01's sounds,
        td_pattern_copy(0, p)                                  // no steps, the test's tempo
        td_pattern_clear(p)
        td_pattern_queue(p)
        td_set_tempo(bpm)
    }
    td_set_swing(50)
    td_midi_config(0, 1)
}

final class TrigCoreTests: XCTestCase {
    func testHeaderConstants() {
        XCTAssertEqual(TD_TRACKS, 8)
        XCTAssertEqual(TD_SLOTS, 8)
        XCTAssertEqual(nparams, 48)
        XCTAssertEqual(Int32(TD_PERFS.rawValue), 18)
        XCTAssertEqual(PerfFX.allCases.count, Int(TD_PERFS.rawValue), "the Swift list and the engine's agree")
        for p in PerfFX.allCases {                                // (the same order: the engine's names, unspaced)
            XCTAssertEqual(String(cString: td_perf_name(Int32(p.rawValue))), p.name.replacingOccurrences(of: " ", with: ""))
        }
        XCTAssertEqual(td_cond_ab.0.0, 1)
        XCTAssertEqual(td_cond_ab.0.1, 2)
    }

    func testDefaultProjectOpensOnHouseAndPlaysBounded() {
        td_init()
        var st = td_state_t()
        td_get_state(&st)
        XCTAssertEqual(st.pattern, 0)
        XCTAssertEqual(st.bpm, 122, accuracy: 0.01, "P01's own tempo")
        XCTAssertEqual(st.length.0, 16)
        XCTAssertEqual(st.length.6, 64, "the melody runs over the 4-bar progression")
        XCTAssertEqual(st.machine.0, m(TD_M_KICK))
        var steps = [td_step_view_t](repeating: td_step_view_t(), count: Int(TD_STEPS))
        td_steps_get(0, &steps)
        XCTAssertEqual((0..<16).filter { steps[$0].trig != 0 }, [0, 4, 8, 12, 14, 15], "four on the floor (+ a FILL)")
        td_play(1)
        let out = render(48000 * 2)
        XCTAssertTrue(out.allSatisfy { $0.isFinite && abs($0) <= 1 })
        XCTAssertGreaterThan(out.map { abs($0) }.max() ?? 0, 0.1)
        td_get_state(&st)
        XCTAssertEqual(st.playing, 1)
        XCTAssertGreaterThanOrEqual(st.step.0, 0)
        XCTAssertGreaterThan(st.peak_l, 0)
    }

    func testParameterDescriptionsAndFormatting() {
        td_init()
        for machine in 0..<Int32(TD_MACHINES.rawValue) {
            td_set_machine(0, machine)
            for param in 0..<nparams {
                let i = info(0, param)
                XCTAssertLessThanOrEqual(i.min, i.def)
                XCTAssertLessThanOrEqual(i.def, i.max)
                XCTAssertFalse(pname(i).isEmpty)
            }
        }
        td_init()
        XCTAssertEqual(pname(info(4, p(TD_PAGE_FLTR, 0))), "CUTOFF")
        XCTAssertEqual(format(info(4, p(TD_PAGE_FLTR, 0)), 127), "20 kHz")
        XCTAssertEqual(format(info(4, p(TD_PAGE_TRIG, 0)), 60), "C3")
        XCTAssertEqual(format(info(4, p(TD_PAGE_TRIG, 4)), 0), "1/16")
        XCTAssertEqual(format(info(4, p(TD_PAGE_FX, 1)), 127), "100 %")
        XCTAssertEqual(pname(info(4, p(TD_PAGE_SRC, 6))), "CHORD")
        XCTAssertEqual(format(info(4, p(TD_PAGE_SRC, 6)), 1), "MIN7")
        var g = td_param_info_t()
        XCTAssertEqual(td_global_info(0, &g), 1)
        XCTAssertEqual(format(g, 6), "3/16")
        XCTAssertEqual(String(cString: td_machine_name(m(TD_M_SAMPLE))), "SAMPLE")
        XCTAssertEqual(String(cString: td_page_name(TD_PAGE_MASTER)), "MASTER")
        XCTAssertEqual(String(cString: td_cond_name(Int32(TD_COND_P1.rawValue) + 3)), "50%")
        XCTAssertEqual(String(cString: td_perf_name(Int32(TD_PERF_DUBHOLD.rawValue))), "DUBHOLD")
        XCTAssertEqual(String(cString: td_sample_name(0)), "VINYL")
    }

    func testTrigTimingThroughMIDI() {
        emptyPattern()
        td_step_set(0, 4, 1)
        td_step_micro(0, 4, 6)                                 // 6000 frames a step: 4.25 steps = frame 25500
        td_play(1)
        var frames: [Int64] = []
        var rendered: Int64 = 0
        var buf = [Float](repeating: 0, count: 2 * 77)
        while rendered < 30000 {
            td_render(&buf, 77, 0)
            var b: [UInt8] = [0, 0, 0]
            var n: Int32 = 0
            var fa: Int64 = 0
            var host: UInt64 = 0
            while td_midi_out(&b, &n, &fa, &host) == 1 {
                if b[0] & 0xF0 == 0x90 { frames.append(rendered + fa) }
            }
            rendered += 77
        }
        XCTAssertEqual(frames.count, 1)
        XCTAssertLessThanOrEqual(abs((frames.first ?? 0) - 25500), 1)
    }

    func testScenesMorphWithTheCrossfader() {
        emptyPattern()
        let cut = p(TD_PAGE_FLTR, 0)
        td_param_set(4, cut, 100)
        td_scene_set(0, 4, cut, 20)
        td_scene_set(1, 4, cut, 120)
        for (x, want) in [(Float(0), Int32(20)), (0.25, 60), (0.5, 100), (1, 120)] {
            td_crossfader(x)
            XCTAssertEqual(td_param_live(4, cut), want, "x \(x)")
        }
        var v: Int32 = 0
        XCTAssertEqual(td_scene_get(1, 4, cut, &v), 1)
        XCTAssertEqual(v, 120)
    }

    func testStepsLocksAndAcid() {
        emptyPattern()
        td_step_note(4, 0, 45, 100)
        td_step_lock(4, 0, p(TD_PAGE_FLTR, 0), 33)
        td_step_slide(4, 0, 1)
        td_step_accent(4, 0, 1)
        td_step_cond(4, 0, Int32(TD_COND_FILL.rawValue))
        var steps = [td_step_view_t](repeating: td_step_view_t(), count: Int(TD_STEPS))
        td_steps_get(4, &steps)
        XCTAssertEqual(steps[0].trig, 1)
        XCTAssertEqual(steps[0].note, 45)
        XCTAssertEqual(steps[0].nlocks, 1)
        XCTAssertEqual(steps[0].slide, 1)
        XCTAssertEqual(steps[0].accent, 1)
        XCTAssertEqual(Int32(steps[0].cond), Int32(TD_COND_FILL.rawValue))
        var v: Int32 = 0
        XCTAssertEqual(td_step_lock_get(4, 0, p(TD_PAGE_FLTR, 0), &v), 1)
        XCTAssertEqual(v, 33)
    }

    func testPatternQueueSwitchesAtTheBar() {
        emptyPattern()
        td_play(1)
        _ = render(10000)
        td_pattern_queue(14)
        var st = td_state_t()
        td_get_state(&st)
        XCTAssertEqual(st.pattern, 12)
        XCTAssertEqual(st.queued_pattern, 14)
        _ = render(96000 - 10000 - 64, chunk: 64)
        td_get_state(&st)
        XCTAssertEqual(st.pattern, 12)
        _ = render(128, chunk: 64)
        td_get_state(&st)
        XCTAssertEqual(st.pattern, 14)
        XCTAssertEqual(st.queued_pattern, -1)
    }

    func testRenderSizesDoNotChangeTheSound() {
        td_init()
        td_play(1)
        let whole = render(48000, chunk: 48000)
        td_init()
        td_play(1)
        var out = [Float](repeating: 0, count: 2 * 48000)
        var pos = 0, k = 0
        let sizes = [1, 7, 256, 1024]
        while pos < 48000 {
            let n = min(sizes[k % 4], 48000 - pos)
            out.withUnsafeMutableBufferPointer { td_render($0.baseAddress! + pos * 2, UInt32(n), 0) }
            pos += n
            k += 1
        }
        XCTAssertEqual(whole, out)
    }

    func testProjectRoundTripAndRefusals() {
        td_init()
        let size = td_project_size()
        var a = [UInt8](repeating: 0, count: size), b = a
        let n = td_project_save(&a, size)
        XCTAssertGreaterThan(n, 1000)
        td_set_tempo(150)
        td_step_set(0, 1, 1)
        XCTAssertEqual(td_project_load(a, Int(n)), 0)
        XCTAssertEqual(td_project_save(&b, size), n)
        XCTAssertEqual(a[0..<Int(n)], b[0..<Int(n)])
        var damaged = Array(a[0..<Int(n)])
        damaged[Int(n) / 3] ^= 0x40
        XCTAssertEqual(td_project_load(damaged, Int(n)), -1)
        XCTAssertEqual(td_project_load(a, 10), -1)
    }

    func testGenerativeTools() {
        emptyPattern()
        td_euclid(1, 3, 8, 0)
        var steps = [td_step_view_t](repeating: td_step_view_t(), count: Int(TD_STEPS))
        td_steps_get(1, &steps)
        XCTAssertEqual((0..<16).filter { steps[$0].trig == 1 }, [0, 3, 6, 8, 11, 14])
        td_rotate(1, 2)
        td_steps_get(1, &steps)
        XCTAssertEqual((0..<16).filter { steps[$0].trig == 1 }, [0, 2, 5, 8, 10, 13])
        td_set_scale(9, 0)
        XCTAssertEqual(td_scale_note(57, 1), 59)              // A minor: A then B
        XCTAssertEqual(String(cString: td_scale_name(0)), "MINOR")
        td_dice(4, 100)
        td_steps_get(4, &steps)
        XCTAssertGreaterThan((0..<16).filter { steps[$0].trig == 1 }.count, 0)
    }

    func testAutopilotAndCapture() {
        td_init()
        td_autopilot(1)
        td_play(1)
        _ = render(48000 * 5)                                  // 2.5 bars
        var st = td_state_t()
        td_get_state(&st)
        XCTAssertEqual(st.autopilot, 1)
        XCTAssertTrue((1...8).contains(st.phrase_bar))
        XCTAssertEqual(td_capture(1, 1, 14), 0)
        XCTAssertEqual(String(cString: td_sample_name(14)), "DECK B 1")
        td_autopilot(0)
        td_get_state(&st)
        XCTAssertEqual(st.autopilot, 0)
    }

    func testScratchAndPerformanceFXStayBounded() {
        td_init()
        td_play(1)
        _ = render(24000)
        td_scratch(0, 1, -1)
        td_perf(Int32(TD_PERF_STUTTER.rawValue), 1, Int32(TD_TARGET_MASTER.rawValue), 1, 0.5)
        var out = render(24000)
        var st = td_state_t()
        td_get_state(&st)
        XCTAssertEqual(st.scratching.0, 1)
        XCTAssertEqual(st.perf_active, 1)
        td_scratch(0, 0, 0)
        td_perf(Int32(TD_PERF_STUTTER.rawValue), 1, Int32(TD_TARGET_MASTER.rawValue), 0, 0.5)
        out += render(24000)
        XCTAssertTrue(out.allSatisfy { $0.isFinite && abs($0) <= 1 })
        td_get_state(&st)
        XCTAssertEqual(st.scratching.0, 0)
    }

    func testSampleLoad() {
        td_init()
        let tone = (0..<4800).map { Float(sin(Double($0) * 0.06)) * 0.5 }
        XCTAssertEqual(td_sample_load(6, tone, 4800, 44100, "TONE"), 0)
        XCTAssertEqual(String(cString: td_sample_name(6)), "TONE")
        XCTAssertEqual(td_sample_load(16, tone, 4800, 44100, "NO"), -1)
        td_set_machine(7, m(TD_M_SAMPLE))
        td_set_sample(7, 6)
        td_trigger(7, 60, 120)
        XCTAssertGreaterThan(td_render_offline(4800), 0.01)
    }
}
