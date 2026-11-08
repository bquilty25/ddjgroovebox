// SPDX-License-Identifier: GPL-3.0-only
// SloopCore through its C API: the SLOOP firmware boots, draws, sounds, sequences and saves on the Mac.
// The firmware is one global instance per process, so the tests share it and leave it stopped and silent.
import Foundation
import SloopCore
import XCTest

final class SloopCoreTests: XCTestCase {
    static let flashURL = FileManager.default.temporaryDirectory.appendingPathComponent("sloopcore-test-\(getpid()).bin")

    override class func setUp() {
        super.setUp()
        try? FileManager.default.removeItem(at: flashURL)
        XCTAssertEqual(sloop_init(flashURL.path), 0)
        _ = sloop_run_frames(30)
    }

    override func tearDown() {
        sloop_panic()
        _ = sloop_run_frames(250)
        super.tearDown()
    }

    private func state() -> sloop_state_t {
        var s = sloop_state_t()
        sloop_get_state(&s)
        return s
    }

    private func tap(_ b: sloop_button_t) {
        sloop_button(Int32(b.rawValue), 1)
        sloop_button(Int32(b.rawValue), 0)
    }

    private func selectTrack(_ t: Int32) {
        for _ in 0..<20 where state().selected_track != t {   // (bounded: a menu left open would ignore ALGORITHM)
            sloop_encoder(Int32(SLOOP_E_ALGORITHM.rawValue), state().selected_track < t ? 1 : -1)
            _ = sloop_run_frames(2)
        }
        XCTAssertEqual(state().selected_track, t, "track selected")
    }

    func testScreenDraws() {
        var px = [UInt32](repeating: 0, count: 240 * 240)
        let frame = px.withUnsafeMutableBufferPointer { sloop_copy_screen($0.baseAddress) }
        XCTAssertGreaterThan(frame, 0)
        let lit = px.filter { $0 & 0xFFFFFF != 0 }.count
        XCTAssertGreaterThan(lit, 500, "the home page should be drawn")
    }

    func testKeyboardLayout() {
        XCTAssertEqual((0..<16).map { sloop_white_key($0) }, [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26])
        XCTAssertEqual((0..<11).map { sloop_black_key($0) }, [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25])
        XCTAssertEqual(sloop_white_key(16), -1)
        XCTAssertEqual(sloop_black_key(11), -1)
    }

    func testKeySoundsAndLights() {
        selectTrack(0)
        XCTAssertLessThan(sloop_run_frames(10), 0.01)
        let k = sloop_white_key(4)
        sloop_key(k, 1)
        XCTAssertGreaterThan(sloop_run_frames(20), 0.02)
        XCTAssertEqual((state().keys_lit >> UInt32(k)) & 1, 1)
        sloop_key(k, 0)
        _ = sloop_run_frames(250)
        XCTAssertLessThan(sloop_run_frames(10), 0.01)
    }

    func testRenderMatchesFrameCountAndStaysBounded() {
        var buf = [Float](repeating: 9, count: 2 * 777)
        sloop_key(sloop_white_key(0), 1)
        buf.withUnsafeMutableBufferPointer { sloop_render($0.baseAddress, 777) }
        sloop_key(sloop_white_key(0), 0)
        XCTAssertFalse(buf.contains { $0 > 1 || $0 < -1 || $0.isNaN }, "every sample written, in range")
    }

    func testStepEntryAndPlayback() {
        selectTrack(3)                                    // drums
        sloop_key(sloop_white_key(0), 1); sloop_key(sloop_white_key(0), 0)   // pick the kick
        _ = sloop_run_frames(30)
        sloop_button(Int32(SLOOP_B_SEQ.rawValue), 1)
        sloop_input_barrier()
        for s in stride(from: 0, to: 16, by: 4) {
            sloop_key(sloop_white_key(Int32(s)), 1); sloop_key(sloop_white_key(Int32(s)), 0)
        }
        _ = sloop_run_frames(10)
        sloop_button(Int32(SLOOP_B_SEQ.rawValue), 0)
        _ = sloop_run_frames(250)
        tap(SLOOP_B_PLAY)
        let peak = sloop_run_frames(125)
        XCTAssertEqual(state().playing, 1)
        XCTAssertGreaterThan(peak, 0.05, "the four kicks play")
        XCTAssertGreaterThanOrEqual(state().step, 0)
        tap(SLOOP_B_PLAY)
        _ = sloop_run_frames(20)
        XCTAssertEqual(state().playing, 0)
        // clear the pattern again: EDIT held + the kick's key erases its steps
        sloop_button(Int32(SLOOP_B_EDIT.rawValue), 1)
        sloop_input_barrier()
        sloop_key(sloop_white_key(0), 1); sloop_key(sloop_white_key(0), 0)
        _ = sloop_run_frames(10)
        sloop_button(Int32(SLOOP_B_EDIT.rawValue), 0)
        _ = sloop_run_frames(30)
    }

    func testAbsoluteControls() {
        sloop_set_bpm(133)
        sloop_set_swing(40)
        sloop_set_master_filter(-20)
        sloop_set_track_level(1, 64)
        sloop_set_track_level(3, 90)
        sloop_set_bpm(999)                                // clamped to 240
        let s = state()
        XCTAssertEqual(s.bpm, 240)
        XCTAssertEqual(s.swing, 40)
        XCTAssertEqual(s.master_filter, -20)
        XCTAssertEqual(s.track_level.1, 64)
        XCTAssertEqual(s.track_level.3, 90)
        sloop_set_bpm(90); sloop_set_swing(0); sloop_set_master_filter(0)
        sloop_set_track_level(1, 104); sloop_set_track_level(3, 100)
    }

    func testButtonOpensPageAndHomeReturns() {
        var before = [UInt32](repeating: 0, count: 240 * 240), after = before
        before.withUnsafeMutableBufferPointer { _ = sloop_copy_screen($0.baseAddress) }
        tap(SLOOP_B_ENV)
        _ = sloop_run_frames(10)
        after.withUnsafeMutableBufferPointer { _ = sloop_copy_screen($0.baseAddress) }
        XCTAssertNotEqual(before, after, "ENV opens its page")
        tap(SLOOP_B_HOME)
        _ = sloop_run_frames(10)
    }

    func testExternalMIDINotePlays() {
        selectTrack(0)
        sloop_midi_in(0x90, 60, 100)
        XCTAssertGreaterThan(sloop_run_frames(15), 0.02)
        sloop_midi_in(0x80, 60, 0)
        _ = sloop_run_frames(250)
    }

    /// docs/REVIEW.md E1: SLOOP's HARDWARE CALIBRATION (HOME menu > SYSTEM) waits for buttons or 30 s; it runs inside
    /// the render, so with a clock that only audio moves it hung the app. The render must keep coming back.
    func testCalibrationMenuDoesNotHangTheRender() {
        sloop_button(Int32(SLOOP_B_HOME.rawValue), 1)
        _ = sloop_run_frames(70)                         // held > 0.7 s: the menu
        sloop_button(Int32(SLOOP_B_HOME.rawValue), 0)
        _ = sloop_run_frames(5)
        for _ in 0..<4 { sloop_encoder(Int32(SLOOP_E_SELECT.rawValue), 1); _ = sloop_run_frames(4) }   // SECTION: SYSTEM
        sloop_encoder(Int32(SLOOP_E_PRESETS.rawValue), 1); _ = sloop_run_frames(4)                     // HARDWARE CALIBRATION
        let done = expectation(description: "renders return")
        let before = state().ui_frames
        DispatchQueue.global().async {
            sloop_button(Int32(SLOOP_B_OCTUP.rawValue), 1)
            sloop_button(Int32(SLOOP_B_OCTUP.rawValue), 0)
            _ = sloop_run_frames(60)                     // would never return before the fix
            done.fulfill()
        }
        wait(for: [done], timeout: 20)
        XCTAssertGreaterThan(state().ui_frames, before + 30, "the UI keeps running")
        // leave the menu as it was opened: HOME held
        sloop_button(Int32(SLOOP_B_HOME.rawValue), 1)
        _ = sloop_run_frames(70)
        sloop_button(Int32(SLOOP_B_HOME.rawValue), 0)
        _ = sloop_run_frames(10)
        selectTrack(0)
        XCTAssertEqual(state().selected_track, 0, "the panel answers again (the menu closed)")
    }

    func testFlashSaveWritesWholeImage() throws {
        _ = sloop_run_frames(400)
        XCTAssertEqual(sloop_save_flash(Self.flashURL.path), 0)
        let size = try FileManager.default.attributesOfItem(atPath: Self.flashURL.path)[.size] as? Int
        XCTAssertEqual(size, 0x100000)
        XCTAssertEqual(sloop_flash_dirty(), 0)
    }
}
