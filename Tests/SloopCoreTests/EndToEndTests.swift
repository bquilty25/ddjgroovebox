// SPDX-License-Identifier: GPL-3.0-only
// The whole chain without hardware: raw DDJ-400 MIDI bytes -> DDJ400Decoder -> GrooveboxMapper -> the SLOOP firmware
// (SloopCore) -> audio, state, and the LED messages that go back to the DDJ-400.
import DDJMapping
import Foundation
import SloopCore
import XCTest

final class EndToEndTests: XCTestCase {
    var decoder = DDJ400Decoder()
    let mapper = GrooveboxMapper()
    var leds = DDJ400LEDWriter()

    override class func setUp() {
        super.setUp()
        _ = sloop_init(nil)                    // (a no-op if SloopCoreTests booted it already)
        _ = sloop_run_frames(30)
    }

    override func tearDown() {
        apply(mapper.releaseAll())
        sloop_panic()
        _ = sloop_run_frames(250)
        super.tearDown()
    }

    private func apply(_ actions: [GrooveAction]) {
        for a in actions {
            switch a {
            case let .key(k, d): sloop_key(Int32(k), d ? 1 : 0)
            case let .button(b, d): sloop_button(Int32(b.rawValue), d ? 1 : 0)
            case let .encoder(e, n): sloop_encoder(Int32(e.rawValue), Int32(n))
            case .barrier: sloop_input_barrier()
            case let .trackLevel(t, v): sloop_set_track_level(Int32(t), Int32(v))
            case let .masterFilter(v): sloop_set_master_filter(Int32(v))
            case let .bpm(v): sloop_set_bpm(Int32(v))
            case let .swing(v): sloop_set_swing(Int32(v))
            case let .masterVolume(v): sloop_set_master_volume(Int32(v))
            case let .selectTrack(t): sloop_select_track(Int32(t))
            }
        }
    }

    private func grooveState() -> GrooveState {
        var s = sloop_state_t()
        sloop_get_state(&s)
        var g = GrooveState()
        g.keysLit = s.keys_lit; g.keysDim = s.keys_dim; g.buttonsLit = s.buttons_lit
        g.selectedTrack = Int(s.selected_track); g.playing = s.playing != 0; g.recording = s.recording != 0
        g.bpm = Int(s.bpm); g.swing = Int(s.swing); g.step = Int(s.step)
        g.trackLevel = [Int(s.track_level.0), Int(s.track_level.1), Int(s.track_level.2), Int(s.track_level.3)]
        g.masterFilter = Int(s.master_filter)
        return g
    }

    /// raw MIDI from the "controller", as the app handles it
    private func midi(_ status: UInt8, _ d1: UInt8, _ d2: UInt8, frames: UInt32 = 1) {
        mapper.update(state: grooveState())
        if let e = decoder.decode(MIDIMessage(status, d1, d2)) { apply(mapper.handle(e)) }
        _ = sloop_run_frames(frames)
    }

    private func selectTrack(_ t: Int) {
        // SAMPLER mode, right deck pads 5..8 (index 4..7) select tracks 1..4
        midi(0x99, 0x30 + UInt8(4 + t), 0x7F, frames: 2)
        midi(0x99, 0x30 + UInt8(4 + t), 0x00, frames: 8)
        XCTAssertEqual(grooveState().selectedTrack, t)
    }

    func testHotCuePadPlaysWhiteKeyAndLightsPad() {
        selectTrack(0)
        midi(0x97, 0x04, 0x7F)                  // left pad 5, HOT CUE: white key 5 (C4)
        XCTAssertGreaterThan(sloop_run_frames(15), 0.02, "the pad plays a note")
        mapper.update(state: grooveState())
        XCTAssertTrue(grooveState().keyLit(FM1Keyboard.whiteKeys[4]))
        XCTAssertEqual(mapper.lights()[.pad(.left, 4)], .on, "its pad lights while the note sounds")
        var out: [MIDIMessage] = []                     // (paced: a few calls, as the app's ticks send them)
        for _ in 0..<30 { out += leds.messages(mapper: mapper, meters: (0, 0)) }
        XCTAssertTrue(out.contains(MIDIMessage(0x97, 0x04, 0x7F)), "the LED message goes to the hardware: \(out)")
        midi(0x97, 0x04, 0x00, frames: 250)
        XCTAssertLessThan(sloop_run_frames(10), 0.01)
    }

    func testPlayButtonStartsTransportAndCrossfaderFilters() {
        midi(0x90, 0x0B, 0x7F)                  // left PLAY/PAUSE
        midi(0x90, 0x0B, 0x00, frames: 10)
        XCTAssertTrue(grooveState().playing)
        midi(0xB6, 0x1F, 0x40)                  // the crossfader picks up at SLOOP's value (centre: filter off) ...
        midi(0xB6, 0x1F, 0x00)                  // ... then hard left: low-pass
        XCTAssertLessThan(grooveState().masterFilter, -50)
        midi(0xB6, 0x1F, 0x40)                  // centre: off
        XCTAssertEqual(grooveState().masterFilter, 0)
        midi(0x91, 0x0B, 0x7F)                  // right PLAY/PAUSE stops
        midi(0x91, 0x0B, 0x00, frames: 10)
        XCTAssertFalse(grooveState().playing)
    }

    func testSeqLayerFromBeatSyncWritesSteps() {
        selectTrack(3)
        midi(0x97, 0x00, 0x7F); midi(0x97, 0x00, 0x00, frames: 20)    // pad 1: the kick (picks it for SEQ)
        midi(0x90, 0x58, 0x7F, frames: 2)                              // left BEAT SYNC = SEQ held
        for p: UInt8 in [0, 4] {                                        // steps 1 and 5
            midi(0x97, p, 0x7F, frames: 2); midi(0x97, p, 0x00, frames: 2)
        }
        midi(0x90, 0x58, 0x00, frames: 250)
        midi(0x90, 0x0B, 0x7F); midi(0x90, 0x0B, 0x00)                  // PLAY
        XCTAssertGreaterThan(sloop_run_frames(100), 0.05, "the steps written from the pads play")
        midi(0x90, 0x0B, 0x7F); midi(0x90, 0x0B, 0x00, frames: 10)
        XCTAssertFalse(grooveState().playing)
        // erase: LOOP IN (EDIT) held + the kick's pad
        midi(0x90, 0x10, 0x7F, frames: 2)
        midi(0x97, 0x00, 0x7F, frames: 2); midi(0x97, 0x00, 0x00, frames: 2)
        midi(0x90, 0x10, 0x00, frames: 30)
    }

    func testTempoSliderNeedsPickupThenSetsBPM() {
        let bpm0 = grooveState().bpm
        // far from the current tempo: no change (soft takeover)
        midi(0xB0, 0x00, 0x00); midi(0xB0, 0x20, 0x00)
        midi(0xB0, 0x00, 0x7F); midi(0xB0, 0x20, 0x7F)
        let after = grooveState().bpm
        // one end of the slider is 60, the other 180: sweeping end to end must cross bpm0 and pick it up
        XCTAssertTrue(after == 60 || after == 180, "after a full sweep the slider has taken over: \(bpm0) -> \(after)")
        sloop_set_bpm(90)
    }

    func testLeftJogSelectsTracks() {
        selectTrack(0)
        for _ in 0..<10 { midi(0xB0, 0x21, 0x40 + 8, frames: 1) }      // 80 ticks clockwise
        _ = sloop_run_frames(5)
        XCTAssertGreaterThan(grooveState().selectedTrack, 0, "the jog moved the selection")
        for _ in 0..<40 { midi(0xB0, 0x21, 0x40 - 8, frames: 1) }
        _ = sloop_run_frames(5)
        XCTAssertEqual(grooveState().selectedTrack, 0)
    }
}
