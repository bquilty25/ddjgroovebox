// SPDX-License-Identifier: GPL-3.0-only
// ScreenReader on texts captured from SLOOP 2.8's screen (SloopCore's text tap): home, a page, the SEQ and FX layers.
import DDJMapping
import XCTest

final class ScreenReaderTests: XCTestCase {
    private func t(_ x: Int, _ y: Int, _ w: Int, _ h: Int, _ c: UInt32, _ s: String) -> ScreenText {
        ScreenText(x: x, y: y, w: w, h: h, argb: 0xFF00_0000 | c, text: s)
    }

    // the TRACKS screen (abridged): track rows, then the knob strip at the bottom
    lazy var home: [ScreenText] = [
        t(4, 4, 32, 32, 0xC5C6CD, "90"), t(40, 20, 24, 16, 0x73757B, "bpm"), t(188, 5, 48, 16, 0x73757B, "tracks"),
        t(34, 41, 64, 16, 0xFFFFFF, "808 BOOM"), t(104, 41, 48, 16, 0x297DFF, "analog"),
        t(34, 149, 24, 16, 0xC5C6CD, "808"), t(64, 149, 40, 16, 0x73757B, "drums"),
        t(10, 208, 40, 16, 0x73757B, "swing"), t(70, 208, 40, 16, 0x73757B, "level"),
        t(130, 208, 40, 16, 0x73757B, "steps"), t(198, 208, 24, 16, 0x73757B, "pan"),
        t(26, 224, 8, 16, 0xC5C6CD, "0"), t(82, 224, 16, 16, 0xC5C6CD, "81"),
        t(142, 224, 16, 16, 0xC5C6CD, "16"), t(206, 224, 8, 16, 0xC5C6CD, "0"),
    ]

    func testHomeKnobs() {
        let r = ScreenReader.read(home, layerShown: false, punch: false)
        XCTAssertEqual(r.knobs.map { $0?.caption }, ["SWING 0", "LEVEL 81", "STEPS 16", "PAN 0"])
        XCTAssertEqual(r.knobBand?.y, 204)
        XCTAssertEqual(r.whiteTiles.compactMap { $0 }, [], "no tiles off a layer (the track rows are not tiles)")
    }

    func testPageKnobsAtTheTopWithUnitsAndColours() {
        let env = [
            t(46, 1, 16, 16, 0xE6E2E6, "90"), t(189, 1, 24, 16, 0x525052, "USB"),
            t(18, 26, 24, 16, 0x838183, "ATK"), t(4, 44, 24, 16, 0x297DFF, "1.0"), t(31, 44, 16, 16, 0x525052, "ms"),
            t(78, 26, 24, 16, 0x838183, "DEC"), t(64, 44, 32, 16, 0x18CE73, "3.36"), t(99, 44, 8, 16, 0x525052, "s"),
            t(138, 26, 24, 16, 0x838183, "SUS"), t(124, 44, 8, 16, 0xFFC618, "0"), t(135, 44, 8, 16, 0x525052, "%"),
            t(198, 26, 24, 16, 0x838183, "REL"), t(184, 44, 16, 16, 0xFF6118, "43"), t(203, 44, 16, 16, 0x525052, "ms"),
            t(18, 222, 48, 16, 0xE6E2E6, "ANALOG"), t(76, 222, 64, 16, 0xBDBABD, "808 BOOM"),
        ]
        let r = ScreenReader.read(env, layerShown: false, punch: false)
        XCTAssertEqual(r.knobs.map { $0?.caption }, ["ATK 1.0 MS", "DEC 3.36 S", "SUS 0 %", "REL 43 MS"])
        XCTAssertEqual(r.knobs.map { $0?.argb }, [0x297DFF, 0x18CE73, 0xFFC618, 0xFF6118], "each knob's colour, as on the screen")
        XCTAssertEqual(r.knobBand?.y, 22)
    }

    func testSeqLayerTiles() {
        var texts = [t(4, 2, 80, 32, 0x297DFF, "steps"), t(94, 18, 56, 16, 0x73757B, "track 1")]
        for i in 0..<16 {
            let row = i / 4, col = i % 4
            let s = "\(i + 1)"
            texts.append(t(30 + 60 * col - 4 * s.count, 49 + 36 * row, 8 * s.count, 16, 0x73757B, s))
        }
        texts += [t(14, 208, 32, 16, 0x73757B, "note"), t(22, 224, 16, 16, 0xC5C6CD, "C4")]
        let r = ScreenReader.read(texts, layerShown: true, punch: false)
        XCTAssertEqual(r.title, "STEPS")
        XCTAssertEqual(r.whiteTiles, (1...16).map { String($0) })
        XCTAssertEqual(r.knobs[0]?.caption, "NOTE C4")
    }

    func testPunchLayerWhiteAndBlackTiles() {
        let texts = [
            t(4, 2, 80, 32, 0xFF6118, "punch"),
            t(6, 60, 48, 16, 0xC5C6CD, "loop 4"), t(66, 60, 48, 16, 0xC5C6CD, "loop 8"),
            t(126, 60, 48, 16, 0xC5C6CD, "loop16"), t(186, 60, 48, 16, 0xC5C6CD, "loop32"),
            t(40, 41, 40, 16, 0x73757B, "k low"), t(100, 41, 40, 16, 0x73757B, "k mid"), t(156, 41, 48, 16, 0x73757B, "k high"),
            t(10, 96, 40, 16, 0xC5C6CD, "stutt"), t(36, 77, 48, 16, 0x73757B, "rewind"),
        ]
        let r = ScreenReader.read(texts, layerShown: true, punch: true)
        XCTAssertEqual(Array(r.whiteTiles.prefix(5)), ["LOOP 4", "LOOP 8", "LOOP16", "LOOP32", "STUTT"])
        XCTAssertEqual(Array(r.blackTiles.prefix(4)), ["K LOW", "K MID", "K HIGH", "REWIND"])
    }
}
