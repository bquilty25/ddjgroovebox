// SPDX-License-Identifier: GPL-3.0-only
import XCTest
@testable import DDJMapping

final class DDJ400DecoderTests: XCTestCase {
    private func decode(_ s: UInt8, _ d1: UInt8, _ d2: UInt8) -> DDJEvent? {
        var d = DDJ400Decoder()
        return d.decode(MIDIMessage(s, d1, d2))
    }

    func testDeckButtonsAndShiftedVariants() {
        let cases: [(UInt8, DDJControl, Bool)] = [
            (0x0B, .play(.left), false), (0x47, .play(.left), true),
            (0x0C, .cue(.left), false), (0x48, .cue(.left), true),
            (0x58, .sync(.left), false), (0x60, .sync(.left), true),
            (0x10, .loopIn(.left), false), (0x4C, .loopIn(.left), true),
            (0x11, .loopOut(.left), false), (0x4E, .loopOut(.left), true),
            (0x4D, .reloop(.left), false), (0x50, .reloop(.left), true),
            (0x51, .callLeft(.left), false), (0x3E, .callLeft(.left), true),
            (0x53, .callRight(.left), false), (0x3D, .callRight(.left), true),
            (0x54, .headphoneCue(.left), false), (0x68, .headphoneCue(.left), true),
            (0x36, .jogTouch(.left), false), (0x67, .jogTouch(.left), true),
            (0x3F, .shift(.left), false),
        ]
        for (note, control, shifted) in cases {
            XCTAssertEqual(decode(0x90, note, 0x7F), DDJEvent(control, .press(true), shifted: shifted), "note \(note)")
        }
        XCTAssertEqual(decode(0x91, 0x0B, 0x7F), DDJEvent(.play(.right), .press(true)))
        XCTAssertEqual(decode(0x91, 0x54, 0x7F), DDJEvent(.headphoneCue(.right), .press(true)))
    }

    func testReleaseByVelocityZeroAndNoteOff() {
        XCTAssertEqual(decode(0x90, 0x0B, 0x00), DDJEvent(.play(.left), .press(false)))
        XCTAssertEqual(decode(0x81, 0x0B, 0x40), DDJEvent(.play(.right), .press(false)))
        XCTAssertEqual(decode(0x87, 0x05, 0x00), DDJEvent(.pad(.left, 5), .press(false), padMode: .hotCue))
    }

    func testPadModeButtons() {                                    // (recorded on a DDJ-400)
        XCTAssertEqual(decode(0x90, 0x1B, 0x7F), DDJEvent(.padModeButton(.left, .hotCue), .press(true)))
        XCTAssertEqual(decode(0x90, 0x6D, 0x7F), DDJEvent(.padModeButton(.left, .beatLoop), .press(true)))
        XCTAssertEqual(decode(0x91, 0x20, 0x7F), DDJEvent(.padModeButton(.right, .beatJump), .press(true)))
        XCTAssertEqual(decode(0x91, 0x22, 0x00), DDJEvent(.padModeButton(.right, .sampler), .press(false)))
        XCTAssertEqual(decode(0x90, 0x69, 0x7F), DDJEvent(.padModeButton(.left, .keyboard), .press(true), shifted: true))
        XCTAssertEqual(decode(0x90, 0x1E, 0x7F), DDJEvent(.padModeButton(.left, .padFX1), .press(true), shifted: true))
        XCTAssertEqual(decode(0x90, 0x6B, 0x7F), DDJEvent(.padModeButton(.left, .padFX2), .press(true), shifted: true))
        XCTAssertEqual(decode(0x90, 0x6F, 0x7F), DDJEvent(.padModeButton(.left, .keyShift), .press(true), shifted: true))
    }

    func testSyncLongPressIsSync() {
        XCTAssertEqual(decode(0x90, 0x5C, 0x7F), DDJEvent(.sync(.left), .press(true)))
        XCTAssertEqual(decode(0x91, 0x5C, 0x00), DDJEvent(.sync(.right), .press(false)))
        XCTAssertEqual(decode(0x81, 0x5C, 0x40), DDJEvent(.sync(.right), .press(false)))
    }

    func testReconnectForgetsHalves() {
        var d = DDJ400Decoder()
        _ = d.decode(MIDIMessage(0xB0, 0x13, 0x40))
        _ = d.decode(MIDIMessage(0xB0, 0x33, 0x7F))
        // a new decoder (what the app makes on a connect) and reset() start from nothing
        var fresh = DDJ400Decoder()
        XCTAssertEqual(fresh.decode(MIDIMessage(0xB0, 0x13, 0x20)),
                       DDJEvent(.channelFader(.left), .absolute(Double(0x20 << 7) / 16383)), "nothing of the old decoder")
        XCTAssertNil(fresh.decode(MIDIMessage(0xB1, 0x33, 0x10)), "an LSB before its MSB")
        XCTAssertNil(fresh.decode(MIDIMessage(0xB1, 0x13, 0x20)), "then the MSB waits for its LSB")
        XCTAssertEqual(fresh.decode(MIDIMessage(0xB1, 0x33, 0x11)),
                       DDJEvent(.channelFader(.right), .absolute(Double(0x20 << 7 | 0x11) / 16383)))
        // reset(): the MSB is not held back by an LSB from before the reconnect
        d.reset()
        XCTAssertEqual(d.decode(MIDIMessage(0xB0, 0x13, 0x20)),
                       DDJEvent(.channelFader(.left), .absolute(Double(0x20 << 7) / 16383)), "no stale LSB")
        d.reset()
        XCTAssertNil(d.decode(MIDIMessage(0xB0, 0x33, 0x01)), "no stale MSB")
    }

    func testBeatFXSection() {
        XCTAssertEqual(decode(0x94, 0x4A, 0x7F), DDJEvent(.beatLeft, .press(true)))
        XCTAssertEqual(decode(0x94, 0x4B, 0x7F), DDJEvent(.beatRight, .press(true)))
        XCTAssertEqual(decode(0x94, 0x63, 0x7F), DDJEvent(.fxSelect, .press(true)))
        XCTAssertEqual(decode(0x94, 0x64, 0x7F), DDJEvent(.fxSelect, .press(true), shifted: true))
        XCTAssertEqual(decode(0x94, 0x47, 0x7F), DDJEvent(.fxOnOff, .press(true)))
        XCTAssertEqual(decode(0x94, 0x43, 0x7F), DDJEvent(.fxOnOff, .press(true), shifted: true))
        // the channel switch: a note-on at each position (CH1, CH2, MASTER); its note-off says nothing
        XCTAssertEqual(decode(0x94, 0x10, 0x7F), DDJEvent(.fxChannel(0), .press(true)))
        XCTAssertEqual(decode(0x94, 0x11, 0x7F), DDJEvent(.fxChannel(1), .press(true)))
        XCTAssertEqual(decode(0x94, 0x14, 0x7F), DDJEvent(.fxChannel(2), .press(true)))
        XCTAssertNil(decode(0x94, 0x14, 0x00))
    }

    func testBrowseAndLoad() {
        XCTAssertEqual(decode(0x96, 0x41, 0x7F), DDJEvent(.browsePress, .press(true)))
        XCTAssertEqual(decode(0x96, 0x42, 0x7F), DDJEvent(.browsePress, .press(true), shifted: true))
        XCTAssertEqual(decode(0x96, 0x46, 0x7F), DDJEvent(.load(.left), .press(true)))
        XCTAssertEqual(decode(0x96, 0x47, 0x7F), DDJEvent(.load(.right), .press(true)))
        XCTAssertEqual(decode(0xB6, 0x40, 0x01), DDJEvent(.browse, .relative(1)))
        XCTAssertEqual(decode(0xB6, 0x40, 0x03), DDJEvent(.browse, .relative(3)))
        XCTAssertEqual(decode(0xB6, 0x40, 0x7F), DDJEvent(.browse, .relative(-1)))
        XCTAssertEqual(decode(0xB6, 0x40, 0x7D), DDJEvent(.browse, .relative(-3)))
    }

    func testPads() {
        for mode in PadMode.allCases {
            for pad in 0..<8 {
                let note = mode.noteOffset + UInt8(pad)
                XCTAssertEqual(decode(0x97, note, 0x7F), DDJEvent(.pad(.left, pad), .press(true), padMode: mode))
                XCTAssertEqual(decode(0x98, note, 0x7F),
                               DDJEvent(.pad(.left, pad), .press(true), padMode: mode, shifted: true))
                XCTAssertEqual(decode(0x99, note, 0x7F), DDJEvent(.pad(.right, pad), .press(true), padMode: mode))
                XCTAssertEqual(decode(0x9A, note, 0x7F),
                               DDJEvent(.pad(.right, pad), .press(true), padMode: mode, shifted: true))
            }
            XCTAssertNil(decode(0x97, mode.noteOffset + 8, 0x7F))
        }
    }

    func testJogs() {
        XCTAssertEqual(decode(0xB0, 0x22, 65), DDJEvent(.jog(.left), .relative(1)))
        XCTAssertEqual(decode(0xB0, 0x23, 60), DDJEvent(.jog(.left), .relative(-4)))
        XCTAssertEqual(decode(0xB1, 0x21, 66), DDJEvent(.jog(.right), .relative(2)))
        XCTAssertEqual(decode(0xB1, 0x29, 63), DDJEvent(.jog(.right), .relative(-1), shifted: true))
    }

    func testFourteenBitPairing() {
        var d = DDJ400Decoder()
        // MSB first, no LSB yet: msb << 7
        XCTAssertEqual(d.decode(MIDIMessage(0xB0, 0x13, 0x40)),
                       DDJEvent(.channelFader(.left), .absolute(Double(0x40 << 7) / 16383)))
        XCTAssertEqual(d.decode(MIDIMessage(0xB0, 0x33, 0x10)),
                       DDJEvent(.channelFader(.left), .absolute(Double(0x40 << 7 | 0x10) / 16383)))
        // once the control has sent an LSB, an MSB waits for its LSB (no jump back with a stale LSB, as seen on the
        // hardware), and the pair gives the value
        XCTAssertNil(d.decode(MIDIMessage(0xB0, 0x13, 0x41)))
        XCTAssertEqual(d.decode(MIDIMessage(0xB0, 0x33, 0x02)),
                       DDJEvent(.channelFader(.left), .absolute(Double(0x41 << 7 | 0x02) / 16383)))
        // a fader moving one way never reads backwards (MSB then LSB, the way the DDJ-400 sends them)
        var last = -1.0
        for v in stride(from: 0x41 << 7, through: 0x48 << 7, by: 37) {
            _ = d.decode(MIDIMessage(0xB0, 0x13, UInt8(v >> 7)))
            if case .absolute(let x)? = d.decode(MIDIMessage(0xB0, 0x33, UInt8(v & 0x7F)))?.value {
                XCTAssertGreaterThan(x, last)
                last = x
            } else { XCTFail("the LSB completes the pair") }
        }
        XCTAssertNil(d.decode(MIDIMessage(0xB0, 0x13, 0x7F)))
        XCTAssertEqual(d.decode(MIDIMessage(0xB0, 0x33, 0x10)).flatMap { e -> Double? in
            if case .absolute(let x) = e.value { return x } else { return nil } }, Double(0x7F << 7 | 0x10) / 16383)
        // an LSB without an MSB yet: nothing
        XCTAssertNil(d.decode(MIDIMessage(0xB1, 0x33, 0x10)))
        // full scale
        _ = d.decode(MIDIMessage(0xB6, 0x1F, 0x7F))
        XCTAssertEqual(d.decode(MIDIMessage(0xB6, 0x3F, 0x7F)), DDJEvent(.crossfader, .absolute(1)))
    }

    func testAbsoluteControlFamilies() {
        let cases: [(UInt8, UInt8, DDJControl)] = [
            (0xB0, 0x13, .channelFader(.left)), (0xB1, 0x13, .channelFader(.right)),
            (0xB0, 0x00, .tempo(.left)), (0xB1, 0x00, .tempo(.right)),
            (0xB0, 0x04, .trim(.left)), (0xB1, 0x04, .trim(.right)),
            (0xB0, 0x07, .eqHi(.left)), (0xB1, 0x0B, .eqMid(.right)), (0xB0, 0x0F, .eqLow(.left)),
            (0xB6, 0x1F, .crossfader), (0xB6, 0x17, .cfx(.left)), (0xB6, 0x18, .cfx(.right)),
            (0xB6, 0x0D, .headphoneLevel), (0xB4, 0x02, .fxLevel),
        ]
        for (s, cc, control) in cases {
            var d = DDJ400Decoder()
            XCTAssertEqual(d.decode(MIDIMessage(s, cc, 0)), DDJEvent(control, .absolute(0)))
            XCTAssertEqual(d.decode(MIDIMessage(s, cc + 0x20, 0x7F))?.control, control)
        }
    }

    func testUnknownMessages() {
        XCTAssertNil(decode(0x90, 0x01, 0x7F))
        XCTAssertNil(decode(0x92, 0x0B, 0x7F))
        XCTAssertNil(decode(0xB0, 0x02, 0x10))
        XCTAssertNil(decode(0xB6, 0x40, 0x00))
        XCTAssertNil(decode(0xE0, 0x00, 0x40))
        XCTAssertNil(decode(0x96, 0x10, 0x7F))
        XCTAssertNil(decode(0xB5, 0x13, 0x10))
    }
}
