// SPDX-License-Identifier: GPL-3.0-only
import XCTest
@testable import DDJMapping

final class DDJ400LEDWriterTests: XCTestCase {
    /// 8 modes x 2 decks x 8 pads x (unshifted + shifted), 2 decks x 9 buttons x 2 notes, FX ON/OFF x 2
    private let ledCount = 8 * 2 * 8 * 2 + 2 * 9 * 2 + 2

    func testFullSendThenDiff() {
        let m = GrooveboxMapper()
        var w = DDJ400LEDWriter.unlimited()
        let first = w.messages(mapper: m, meters: (0, 0))
        XCTAssertEqual(first.count, ledCount + 2)
        XCTAssertTrue(first.contains(MIDIMessage(0xB0, 0x02, 0)))
        XCTAssertTrue(first.contains(MIDIMessage(0xB1, 0x02, 0)))
        XCTAssertEqual(w.messages(mapper: m, meters: (0, 0)), [])

        var s = GrooveState()
        s.keysLit = 1 << 0                                      // F3
        s.keysDim = 1 << 2                                      // G3: dim is sent as off
        s.buttonsLit = 1 << UInt32(FM1Button.rec.rawValue)
        m.update(state: s)
        let diff = Set(w.messages(mapper: m, meters: (0, 0)))
        let expected: Set<MIDIMessage> = [
            // F3: HOT CUE / PAD FX1 left pad 1, BEAT LOOP / KEYBOARD left pad 5, unshifted and shifted
            MIDIMessage(0x97, 0x00, 0x7F), MIDIMessage(0x98, 0x00, 0x7F),
            MIDIMessage(0x97, 0x10, 0x7F), MIDIMessage(0x98, 0x10, 0x7F),
            MIDIMessage(0x97, 0x64, 0x7F), MIDIMessage(0x98, 0x64, 0x7F),
            MIDIMessage(0x97, 0x44, 0x7F), MIDIMessage(0x98, 0x44, 0x7F),
            // CUE (REC) both decks, unshifted 0x0C and shifted 0x48
            MIDIMessage(0x90, 0x0C, 0x7F), MIDIMessage(0x90, 0x48, 0x7F),
            MIDIMessage(0x91, 0x0C, 0x7F), MIDIMessage(0x91, 0x48, 0x7F),
        ]
        XCTAssertEqual(diff, expected)

        w.invalidate()
        XCTAssertEqual(w.messages(mapper: m, meters: (0, 0)).count, ledCount + 2)
    }

    func testButtonNotes() {
        let m = GrooveboxMapper()
        var s = GrooveState()
        s.buttonsLit = FM1Button.allCases.reduce(0) { $0 | 1 << UInt32($1.rawValue) }
        m.update(state: s)
        var w = DDJ400LEDWriter.unlimited()
        let all = Set(w.messages(mapper: m, meters: (0, 0)))
        for status: UInt8 in [0x90, 0x91] {
            for note: UInt8 in [0x0B, 0x0C, 0x58, 0x10, 0x11, 0x4D, 0x51, 0x53, 0x54] {
                XCTAssertTrue(all.contains(MIDIMessage(status, note, 0x7F)), "\(status) \(note)")
            }
        }
        XCTAssertTrue(all.contains(MIDIMessage(0x94, 0x47, 0x7F)))
    }

    func testSamplerTrackPads() {
        let m = GrooveboxMapper()
        var s = GrooveState()
        s.selectedTrack = 2
        m.update(state: s)
        var w = DDJ400LEDWriter.unlimited()
        let all = Set(w.messages(mapper: m, meters: (0, 0)))
        XCTAssertTrue(all.contains(MIDIMessage(0x99, 0x36, 0x7F)))   // right pad 7: track 3
        XCTAssertTrue(all.contains(MIDIMessage(0x99, 0x35, 0x00)))   // dim: off
        XCTAssertTrue(all.contains(MIDIMessage(0x9A, 0x36, 0x7F)))
    }

    private func amp(_ db: Double) -> Double { pow(10, db / 20) }

    func testMeterScaleInDB() {
        XCTAssertEqual(DDJ400LEDWriter.meterValue(dB: 0, floor: -48), 127)
        XCTAssertEqual(DDJ400LEDWriter.meterValue(dB: -24, floor: -48), 64)   // 63.5
        XCTAssertEqual(DDJ400LEDWriter.meterValue(dB: -12, floor: -48), 95)   // 95.25
        XCTAssertEqual(DDJ400LEDWriter.meterValue(dB: -48, floor: -48), 0)
        XCTAssertEqual(DDJ400LEDWriter.meterValue(dB: -60, floor: -48), 0)
        XCTAssertEqual(DDJ400LEDWriter.meterValue(dB: -.infinity, floor: -48), 0)
    }

    func testMetersRiseAtOnceAndDecay() {
        let m = GrooveboxMapper()
        var w = DDJ400LEDWriter.unlimited()
        var t = 10.0
        w.now = { t }
        _ = w.messages(mapper: m, meters: (0, 0))
        XCTAssertEqual(w.messages(mapper: m, meters: (1, amp(-12))),
                       [MIDIMessage(0xB0, 0x02, 127), MIDIMessage(0xB1, 0x02, 95)], "a peak shows at once")
        t += 0.4    // silence: falls 25 dB/s, 0 -> -10 dB, -12 -> -22 dB
        XCTAssertEqual(w.messages(mapper: m, meters: (0, 0)),
                       [MIDIMessage(0xB0, 0x02, 101), MIDIMessage(0xB1, 0x02, 69)])
        t += 0.01   // -0.25 dB: less than 2 steps, nothing sent
        XCTAssertEqual(w.messages(mapper: m, meters: (0, 0)), [])
        t += 0.04   // -11.25 dB -> 97 (4 down from the 101 sent)
        XCTAssertEqual(w.messages(mapper: m, meters: (0, 0)).first, MIDIMessage(0xB0, 0x02, 97))
        t += 2      // below the floor: 0 is always sent
        XCTAssertEqual(w.messages(mapper: m, meters: (0, 0)), [MIDIMessage(0xB0, 0x02, 0), MIDIMessage(0xB1, 0x02, 0)])
        XCTAssertEqual(w.messages(mapper: m, meters: (0, 0)), [])
        // a level above what is shown wins over the decay
        t += 0.1
        XCTAssertEqual(w.messages(mapper: m, meters: (amp(-12), 0)), [MIDIMessage(0xB0, 0x02, 95)])
    }

    func testAllOff() {
        let m = GrooveboxMapper()
        var s = GrooveState()
        s.keysLit = 0x7FF_FFFF
        s.buttonsLit = 0x3FFF
        m.update(state: s)
        var w = DDJ400LEDWriter.unlimited()
        _ = w.messages(mapper: m, meters: (1, 1))
        let off = w.allOff()
        XCTAssertEqual(off.count, ledCount + 2)
        XCTAssertTrue(off.allSatisfy { $0.data2 == 0 })
        XCTAssertEqual(Set(off.map { UInt16($0.status) << 8 | UInt16($0.data1) }).count, ledCount + 2)
        XCTAssertTrue(off.contains(MIDIMessage(0xB0, 0x02, 0)))
        XCTAssertTrue(off.contains(MIDIMessage(0xB1, 0x02, 0)))
        XCTAssertTrue(off.contains(MIDIMessage(0x9A, 0x77, 0)))
        // the sent state is off: a dark state needs only the selected-track pad (SAMPLER right pad 5), a lit one more
        m.update(state: GrooveState())
        XCTAssertEqual(Set(w.messages(mapper: m, meters: (0, 0))),
                       [MIDIMessage(0x99, 0x34, 0x7F), MIDIMessage(0x9A, 0x34, 0x7F)])
        m.update(state: s)
        XCTAssertFalse(w.messages(mapper: m, meters: (0, 0)).isEmpty)
    }
}

extension DDJ400LEDWriter {
    /// (these tests check what is sent, not the pacing: TrigMapperTests.testLEDWriterBudgetAndOrder checks that)
    static func unlimited() -> DDJ400LEDWriter {
        var w = DDJ400LEDWriter()
        w.maxPerCall = .max
        return w
    }
}
