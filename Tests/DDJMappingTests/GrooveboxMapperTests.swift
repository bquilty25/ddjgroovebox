// SPDX-License-Identifier: GPL-3.0-only
import XCTest
@testable import DDJMapping

/// Shared helpers: a mapper on a clock the test drives.
class MapperTestCase: XCTestCase {
    var m: GrooveboxMapper!
    var t = 0.0

    override func setUp() {
        m = GrooveboxMapper()
        t = 100
        m.now = { [unowned self] in self.t }
    }

    @discardableResult
    func press(_ c: DDJControl, _ down: Bool = true, mode: PadMode? = nil, shifted: Bool = false) -> [GrooveAction] {
        m.handle(DDJEvent(c, .press(down), padMode: mode, shifted: shifted))
    }

    @discardableResult
    func abs(_ c: DDJControl, _ x: Double) -> [GrooveAction] { m.handle(DDJEvent(c, .absolute(x))) }

    func rel(_ c: DDJControl, _ n: Int, shifted: Bool = false) -> [GrooveAction] {
        m.handle(DDJEvent(c, .relative(n), shifted: shifted))
    }

    func state(_ f: (inout GrooveState) -> Void) {
        var s = m.state
        f(&s)
        m.update(state: s)
    }

    func layout(_ mode: PadMode, shifted: Bool = false) -> [[DDJPadFunction]] {
        Deck.allCases.map { d in (0..<8).map { DDJPadFunction.of(mode: mode, deck: d, pad: $0, shifted: shifted) } }
    }
}

final class GrooveboxMapperTests: MapperTestCase {
    // MARK: pad layouts

    func testHotCueWhiteKeys() {
        let w = FM1Keyboard.whiteKeys
        XCTAssertEqual(layout(.hotCue)[0], (0..<8).map { .key(w[$0]) })
        XCTAssertEqual(layout(.hotCue)[1], (0..<8).map { .key(w[8 + $0]) })
        XCTAssertEqual(layout(.padFX1)[1], (0..<8).map { .punch(w[8 + $0]) })
    }

    func testShiftedPadsPunch() {
        // SHIFT + a pad in any mode: that pad's key with FX held; unused and track pads unchanged
        for mode in PadMode.allCases {
            let plain = layout(mode), shifted = layout(mode, shifted: true)
            for d in 0..<2 {
                for p in 0..<8 {
                    switch plain[d][p] {
                    case .key(let k), .punch(let k): XCTAssertEqual(shifted[d][p], .punch(k), "\(mode) \(d) \(p)")
                    default: XCTAssertEqual(shifted[d][p], plain[d][p], "\(mode) \(d) \(p)")
                    }
                }
            }
        }
    }

    func testPianoLow() {
        let l = layout(.beatLoop)
        XCTAssertEqual(l[0].suffix(4).compactMap(\.key).map(FM1Keyboard.noteName), ["F3", "G3", "A3", "B3"])
        XCTAssertEqual(l[1].suffix(4).compactMap(\.key).map(FM1Keyboard.noteName), ["C4", "D4", "E4", "F4"])
        XCTAssertEqual(l[0].prefix(4).map { $0.key.map(FM1Keyboard.noteName) }, ["F#3", "G#3", "A#3", nil])
        XCTAssertEqual(l[1].prefix(4).map { $0.key.map(FM1Keyboard.noteName) }, ["C#4", "D#4", nil, "F#4"])
        XCTAssertEqual(layout(.keyboard), l)
    }

    func testPianoHighHasFiveBlackKeys() {
        let l = layout(.beatJump)
        let top = l.flatMap { $0.prefix(4) }.compactMap(\.key)
        XCTAssertEqual(top.count, 5)
        XCTAssertTrue(top.allSatisfy(FM1Keyboard.isBlack))
        XCTAssertEqual(l[0].suffix(4).compactMap(\.key).map(FM1Keyboard.noteName), ["G4", "A4", "B4", "C5"])
        XCTAssertEqual(l[1].suffix(4).compactMap(\.key).map(FM1Keyboard.noteName), ["D5", "E5", "F5", "G5"])
        XCTAssertEqual(layout(.keyShift), l)
    }

    func testPianoModesCoverAllKeysOnce() {
        let keys = (layout(.beatLoop) + layout(.beatJump)).flatMap { $0 }.compactMap(\.key)
        XCTAssertEqual(keys.sorted(), Array(0..<FM1Keyboard.keyCount))
    }

    func testSamplerAndPadFX2() {
        let b = FM1Keyboard.blackKeys
        let s = layout(.sampler)
        XCTAssertEqual(s[0], (0..<8).map { .key(b[$0]) })
        XCTAssertEqual(s[1], [.key(b[8]), .key(b[9]), .key(b[10]), .none,
                              .selectTrack(0), .selectTrack(1), .selectTrack(2), .selectTrack(3)])
        let f = layout(.padFX2)
        XCTAssertEqual(f[0], (0..<8).map { .punch(b[$0]) })
        XCTAssertEqual(f[1], [.punch(b[8]), .punch(b[9]), .punch(b[10]), .none, .none, .none, .none, .none])
    }

    // MARK: pad modes per deck

    func testPadModePerDeck() {
        XCTAssertEqual(m.padMode, .hotCue)
        press(.pad(.left, 0), mode: .hotCue)
        press(.pad(.left, 0), false, mode: .hotCue)
        press(.pad(.right, 5), mode: .sampler)
        press(.pad(.right, 5), false, mode: .sampler)
        XCTAssertEqual(m.padMode(.left), .hotCue)
        XCTAssertEqual(m.padMode(.right), .sampler)
        XCTAssertEqual(m.padMode, .sampler, "the deck played last")
        XCTAssertEqual(m.caption(for: .pad(.left, 0)), "F3", "the left deck keeps its own mode")
        press(.padModeButton(.left, .beatJump))
        XCTAssertEqual(m.padMode(.left), .beatJump)
        XCTAssertEqual(m.padMode(.right), .sampler)
        m.setPadMode(.keyboard, deck: .right)
        XCTAssertEqual(m.padMode(.right), .keyboard)
        XCTAssertEqual(m.padMode, .keyboard)
        // a pad with no mode (the on-screen DDJ) plays in its deck's mode
        XCTAssertEqual(press(.pad(.left, 4)), [.key(FM1Keyboard.whiteKeys[8], true)])   // BEAT JUMP: G4
    }

    // MARK: pads

    func testPadPlaysKeyAndReleasesAfterModeChange() {
        XCTAssertEqual(press(.pad(.left, 1), mode: .hotCue), [.key(2, true)])   // G3 = key 2
        m.setPadMode(.sampler, deck: .left)
        XCTAssertEqual(press(.pad(.left, 1), false, mode: .sampler), [.key(2, false)])
        XCTAssertEqual(press(.pad(.left, 1), false), [])
    }

    func testUnusedPadDoesNothing() {
        XCTAssertEqual(press(.pad(.left, 3), mode: .beatLoop), [])
        XCTAssertEqual(press(.pad(.left, 3), false, mode: .beatLoop), [])
        XCTAssertEqual(press(.pad(.right, 3), mode: .sampler), [])
    }

    func testShiftedPadPunchesInSequence() {
        XCTAssertEqual(press(.pad(.right, 0), mode: .sampler, shifted: true),
                       [.button(.fx, true), .barrier, .key(FM1Keyboard.blackKeys[8], true)])
        XCTAssertEqual(press(.pad(.right, 0), false, mode: .sampler, shifted: true),
                       [.key(FM1Keyboard.blackKeys[8], false), .barrier, .button(.fx, false)])
    }

    func testShiftChangingBetweenPressAndRelease() {
        // pressed with SHIFT (a punch), released without: the punch ends
        press(.pad(.left, 0), mode: .hotCue, shifted: true)
        XCTAssertEqual(press(.pad(.left, 0), false, mode: .hotCue), [.key(0, false), .barrier, .button(.fx, false)])
        // pressed without (a note), released with SHIFT: just the note
        press(.pad(.left, 0), mode: .hotCue)
        XCTAssertEqual(press(.pad(.left, 0), false, mode: .hotCue, shifted: true), [.key(0, false)])
        // pressed in PAD FX1 (punch), released in HOT CUE: the punch ends
        press(.pad(.left, 2), mode: .padFX1)
        XCTAssertEqual(press(.pad(.left, 2), false, mode: .hotCue), [.key(4, false), .barrier, .button(.fx, false)])
    }

    func testSameKeyFromTwoPadsRetriggers() {
        // F4 (key 12): HOT CUE left pad 8 and BEAT LOOP right pad 8
        XCTAssertEqual(press(.pad(.left, 7), mode: .hotCue), [.key(12, true)])
        XCTAssertEqual(press(.pad(.right, 7), mode: .beatLoop), [.key(12, false), .barrier, .key(12, true)])
        XCTAssertEqual(press(.pad(.left, 7), false), [], "still held by the other pad")
        XCTAssertEqual(press(.pad(.left, 7), mode: .hotCue), [.key(12, false), .barrier, .key(12, true)],
                       "a two-finger roll: every strike sounds")
        XCTAssertEqual(press(.pad(.left, 7), false), [])
        XCTAssertEqual(press(.pad(.right, 7), false), [.key(12, false)])
    }

    func testPunchOnHeldKey() {
        // C4 (key 7) held as a note (HOT CUE left pad 5), then SHIFT + BEAT LOOP right pad 5 (C4) punches it:
        // FX first, then the key struck again inside FX (SLOOP ends the note on the key's release, as it began)
        XCTAssertEqual(press(.pad(.left, 4), mode: .hotCue), [.key(7, true)])
        XCTAssertEqual(press(.pad(.right, 4), mode: .beatLoop, shifted: true),
                       [.button(.fx, true), .barrier, .key(7, false), .barrier, .key(7, true)])
        XCTAssertEqual(press(.pad(.right, 4), false), [.barrier, .button(.fx, false)], "the note pad still holds C4")
        XCTAssertEqual(press(.pad(.left, 4), false), [.key(7, false)])
        // the same from a PAD FX1 pad
        press(.pad(.left, 4), mode: .beatLoop)                                    // F3 (piano low)
        XCTAssertEqual(press(.pad(.left, 0), mode: .padFX1),
                       [.button(.fx, true), .barrier, .key(0, false), .barrier, .key(0, true)])
    }

    func testPunchInSequence() {
        let k = FM1Keyboard.whiteKeys[0]
        XCTAssertEqual(press(.pad(.left, 0), mode: .padFX1), [.button(.fx, true), .barrier, .key(k, true)])
        XCTAssertEqual(press(.pad(.left, 1), mode: .padFX1), [.key(2, true)])
        XCTAssertEqual(press(.pad(.left, 0), false), [.key(k, false)])
        XCTAssertEqual(press(.pad(.left, 1), false), [.key(2, false), .barrier, .button(.fx, false)])
        XCTAssertEqual(press(.pad(.right, 0), mode: .padFX2),
                       [.button(.fx, true), .barrier, .key(FM1Keyboard.blackKeys[8], true)])
    }

    func testPunchInWithPhysicalFX() {
        XCTAssertEqual(press(.fxOnOff), [.button(.fx, true)])
        XCTAssertEqual(press(.pad(.left, 0), mode: .padFX1), [.key(0, true)])
        XCTAssertEqual(press(.fxOnOff, false), [])
        XCTAssertEqual(press(.pad(.left, 0), false), [.key(0, false), .barrier, .button(.fx, false)])
        press(.pad(.left, 0), mode: .padFX1)
        XCTAssertEqual(press(.sync(.right)), [])
        XCTAssertEqual(press(.pad(.left, 0), false), [.key(0, false)])
        XCTAssertEqual(press(.sync(.right), false), [.button(.fx, false)])
    }

    // MARK: track selection

    func testTrackSelectPads() {
        state { $0.selectedTrack = 0 }
        XCTAssertEqual(press(.pad(.right, 7), mode: .sampler), [.selectTrack(3)])
        XCTAssertEqual(press(.pad(.right, 7), false), [])
        XCTAssertEqual(m.selectedTrack, 3)
        // SLOOP has not applied it yet (a tick in between): still assumed, the next pad goes from it
        state { $0.selectedTrack = 0 }
        XCTAssertEqual(m.selectedTrack, 3)
        XCTAssertEqual(press(.pad(.right, 6)), [.selectTrack(2)])
        press(.pad(.right, 6), false)
        XCTAssertEqual(press(.pad(.right, 6)), [], "already selected")
        press(.pad(.right, 6), false)
        // confirmed
        state { $0.selectedTrack = 2 }
        XCTAssertEqual(m.selectedTrack, 2)
        state { $0.selectedTrack = 1 }   // changed in SLOOP: followed at once
        XCTAssertEqual(m.selectedTrack, 1)
    }

    func testAssumedTrackTimesOut() {
        state { $0.selectedTrack = 0 }
        press(.pad(.right, 5), mode: .sampler)
        t += 0.3
        state { $0.selectedTrack = 0 }
        XCTAssertEqual(m.selectedTrack, 1)
        t += 0.3
        XCTAssertEqual(m.selectedTrack, 0, "never confirmed: SLOOP's own selection again")
        state { $0.selectedTrack = 0 }
        XCTAssertEqual(m.selectedTrack, 0)
    }

    func testLoadSelectsTrack() {
        state { $0.selectedTrack = 0 }
        XCTAssertEqual(press(.load(.left)), [], "clamped at track 1")
        XCTAssertEqual(press(.load(.right)), [.selectTrack(1)])
        XCTAssertEqual(press(.load(.right), false), [])
        XCTAssertEqual(press(.load(.right)), [.selectTrack(2)], "from the assumed track")
        XCTAssertEqual(press(.load(.right)), [.selectTrack(3)])
        XCTAssertEqual(press(.load(.right)), [], "clamped at the drums")
        XCTAssertEqual(press(.load(.left)), [.selectTrack(2)])
    }

    // MARK: buttons

    func testButtonTable() {
        let left: [(DDJControl, FM1Button)] = [
            (.play(.left), .play), (.cue(.left), .rec), (.sync(.left), .seq), (.loopIn(.left), .edit),
            (.loopOut(.left), .arp), (.reloop(.left), .glo), (.callLeft(.left), .octDown),
            (.callRight(.left), .octUp), (.headphoneCue(.left), .home),
        ]
        let right: [(DDJControl, FM1Button)] = [
            (.play(.right), .play), (.cue(.right), .rec), (.sync(.right), .fx), (.loopIn(.right), .env),
            (.loopOut(.right), .lfo), (.reloop(.right), .sel), (.callLeft(.right), .octDown),
            (.callRight(.right), .octUp), (.headphoneCue(.right), .save),
            (.browsePress, .home), (.fxOnOff, .fx),
        ]
        for (c, b) in left + right {
            XCTAssertEqual(press(c), [.button(b, true)], "\(c)")
            XCTAssertEqual(press(c, false), [.button(b, false)], "\(c)")
        }
        XCTAssertEqual(press(.play(.left), shifted: true), [.button(.play, true)])
        XCTAssertEqual(press(.play(.left), false), [.button(.play, false)])
        XCTAssertEqual(press(.sync(.right), shifted: true), [.button(.fx, true)])
        XCTAssertEqual(press(.sync(.right), false, shifted: true), [.button(.fx, false)])
    }

    func testBothPlayButtonsRefcounted() {
        XCTAssertEqual(press(.play(.left)), [.button(.play, true)])
        XCTAssertEqual(press(.play(.right)), [])
        XCTAssertEqual(press(.play(.left), false), [])
        XCTAssertEqual(press(.play(.right), false), [.button(.play, false)])
        XCTAssertEqual(press(.callLeft(.left)), [.button(.octDown, true)])
        XCTAssertEqual(press(.callLeft(.right)), [])
        XCTAssertEqual(press(.callLeft(.right), false), [])
        XCTAssertEqual(press(.callLeft(.left), false), [.button(.octDown, false)])
        XCTAssertEqual(press(.cue(.left), false), [])
        press(.cue(.left))
        XCTAssertEqual(press(.cue(.left)), [])
        XCTAssertEqual(press(.cue(.left), false), [.button(.rec, false)])
    }

    func testSyncLongPressReleasesSEQ() {
        // 0x58 on, 0x5C on (the long press), 0x5C off: SEQ is let go once
        var d = DDJ400Decoder()
        let msgs = [MIDIMessage(0x90, 0x58, 0x7F), MIDIMessage(0x90, 0x5C, 0x7F), MIDIMessage(0x90, 0x5C, 0x00),
                    MIDIMessage(0x90, 0x58, 0x00)]
        let out = msgs.map { d.decode($0).map(m.handle) ?? [] }
        XCTAssertEqual(out, [[.button(.seq, true)], [], [.button(.seq, false)], []])
    }

    func testCentreControls() {
        XCTAssertEqual(press(.beatLeft), [.encoder(.select, -1)])
        XCTAssertEqual(press(.beatRight), [.encoder(.select, 1)])
        XCTAssertEqual(press(.beatRight, false), [])
        XCTAssertEqual(press(.fxSelect), [.encoder(.presets, 1)])
        XCTAssertEqual(press(.fxSelect, shifted: true), [.encoder(.presets, -1)])
        XCTAssertEqual(rel(.browse, 3), [.encoder(.select, 3)])
        XCTAssertEqual(rel(.browse, -1), [.encoder(.select, -1)])
    }

    func testNoActionControls() {
        XCTAssertEqual(press(.shift(.left)), [])
        XCTAssertEqual(press(.jogTouch(.right)), [])
        XCTAssertEqual(abs(.headphoneLevel, 0.5), [])
        XCTAssertEqual(abs(.fxLevel, 0.5), [])
        XCTAssertEqual(abs(.trim(.right), 0.5), [])
        XCTAssertEqual(press(.padModeButton(.right, .sampler)), [])
    }

    // MARK: jogs

    func testLeftJogStepsTracks() {
        XCTAssertEqual(rel(.jog(.left), 20), [])
        XCTAssertEqual(rel(.jog(.left), 50), [.encoder(.algorithm, 1), .barrier, .encoder(.algorithm, 1), .barrier])
        XCTAssertEqual(rel(.jog(.left), -31), [], "a direction change resets the count")
        XCTAssertEqual(rel(.jog(.left), -1), [.encoder(.algorithm, -1), .barrier])
        XCTAssertEqual(rel(.jog(.left), 8, shifted: true), [.encoder(.algorithm, 1), .barrier], "search: 4x")
        m.jogTicksPerTrackStep = 4
        XCTAssertEqual(rel(.jog(.left), 4), [.encoder(.algorithm, 1), .barrier])
    }

    func testRightJogStepsPresets() {
        XCTAssertEqual(rel(.jog(.right), 5), [])
        XCTAssertEqual(rel(.jog(.right), 20), [.encoder(.presets, 3)])   // 25 -> 3, 1 left
        XCTAssertEqual(rel(.jog(.right), -7), [])                        // reset, -7
        XCTAssertEqual(rel(.jog(.right), -1), [.encoder(.presets, -1)])
        XCTAssertEqual(rel(.jog(.right), 4, shifted: true), [.encoder(.presets, 2)])
    }

    func testJogIdleAndTouchReset() {
        XCTAssertEqual(rel(.jog(.left), 31), [])
        t += 0.2
        XCTAssertEqual(rel(.jog(.left), 1), [.encoder(.algorithm, 1), .barrier], "within 300 ms: counted")
        XCTAssertEqual(rel(.jog(.left), 31), [])
        t += 0.31
        XCTAssertEqual(rel(.jog(.left), 1), [], "after 300 ms idle: from zero")
        XCTAssertEqual(rel(.jog(.right), 7), [])
        press(.jogTouch(.right))
        press(.jogTouch(.right), false)
        XCTAssertEqual(rel(.jog(.right), 1), [], "the jog let go: from zero")
    }

    // MARK: knobs

    func testEQRelative() {
        XCTAssertEqual(abs(.eqHi(.left), 0.5), [], "reference only")
        XCTAssertEqual(abs(.eqHi(.left), 0.5 + 1.0 / 64), [.encoder(.k1, 1)])
        XCTAssertEqual(abs(.eqHi(.left), 0.5 + 1.5 / 64), [], "half a detent kept")
        XCTAssertEqual(abs(.eqHi(.left), 0.5 + 2.0 / 64), [.encoder(.k1, 1)])
        XCTAssertEqual(abs(.eqHi(.left), 0.5 - 2.0 / 64), [.encoder(.k1, -4)])
        XCTAssertEqual(abs(.eqMid(.right), 0), [])
        XCTAssertEqual(abs(.eqMid(.right), 1), [.encoder(.k2, 64)])
        abs(.eqLow(.left), 0.2)
        XCTAssertEqual(abs(.eqLow(.left), 0.2 + 3.0 / 64), [.encoder(.k3, 3)])
        abs(.cfx(.right), 0.5)
        XCTAssertEqual(abs(.cfx(.right), 0.5 - 1.0 / 64), [.encoder(.k4, -1)])
    }

    // MARK: physical state across a reconnect

    func testResetPhysicalState() {
        abs(.eqHi(.left), 0.2)
        m.resetPhysicalState()
        XCTAssertEqual(abs(.eqHi(.left), 0.8), [], "the first message after a reconnect only sets the reference")
        state { $0.trackLevel = [50, 104, 104, 100] }
        XCTAssertEqual(abs(.channelFader(.left), 50.0 / 127), [.trackLevel(track: 0, value: 50)])
        m.resetPhysicalState()
        XCTAssertEqual(abs(.channelFader(.left), 120.0 / 127), [], "pickup again")
        XCTAssertEqual(rel(.jog(.left), 31), [])
        m.resetPhysicalState()
        XCTAssertEqual(rel(.jog(.left), 1), [])
    }

    func testReleaseAllForgetsPositions() {
        abs(.eqHi(.left), 0.2)
        state { $0.trackLevel = [50, 104, 104, 100] }
        XCTAssertEqual(abs(.channelFader(.left), 20.0 / 127), [])
        _ = m.releaseAll()
        XCTAssertEqual(abs(.eqHi(.left), 0.8), [])
        XCTAssertEqual(abs(.channelFader(.left), 120.0 / 127), [], "no crossing from a stale position")
    }

    // MARK: faders

    func testSoftTakeoverChannel1() {
        state {
            $0.selectedTrack = 1
            $0.trackLevel = [104, 50, 104, 100]
        }
        XCTAssertEqual(abs(.channelFader(.left), 0), [])
        XCTAssertEqual(abs(.channelFader(.left), 20.0 / 127), [])
        XCTAssertEqual(abs(.channelFader(.left), 48.0 / 127), [.trackLevel(track: 1, value: 48)])
        XCTAssertEqual(abs(.channelFader(.left), 10.0 / 127), [.trackLevel(track: 1, value: 10)])
        XCTAssertEqual(abs(.channelFader(.left), 10.0 / 127), [])
        state { $0.trackLevel[1] = 10 }
        XCTAssertEqual(abs(.channelFader(.left), 12.0 / 127), [.trackLevel(track: 1, value: 12)])
        state { $0.selectedTrack = 0 }
        XCTAssertEqual(abs(.channelFader(.left), 14.0 / 127), [])
        XCTAssertEqual(abs(.channelFader(.left), 1.0), [.trackLevel(track: 0, value: 127)], "crossed 104")
    }

    func testSoftTakeoverRearmsOnExternalChange() {
        state { $0.trackLevel[3] = 100 }
        XCTAssertEqual(abs(.channelFader(.right), 99.0 / 127), [.trackLevel(track: 3, value: 99)])
        state { $0.trackLevel[3] = 98 }
        XCTAssertEqual(abs(.channelFader(.right), 90.0 / 127), [.trackLevel(track: 3, value: 90)])
        state { $0.trackLevel[3] = 40 }
        XCTAssertEqual(abs(.channelFader(.right), 80.0 / 127), [])
        XCTAssertEqual(abs(.channelFader(.right), 30.0 / 127), [.trackLevel(track: 3, value: 30)])
    }

    func testTrackSelectPadRearmsChannel1() {
        state { $0.trackLevel = [104, 104, 50, 100] }
        XCTAssertEqual(abs(.channelFader(.left), 104.0 / 127), [.trackLevel(track: 0, value: 104)])
        press(.pad(.right, 6), mode: .sampler)
        XCTAssertEqual(abs(.channelFader(.left), 60.0 / 127), [])
        XCTAssertEqual(abs(.channelFader(.left), 52.0 / 127), [.trackLevel(track: 2, value: 52)])
    }

    func testTempoSliders() {
        state {
            $0.bpm = 120
            $0.swing = 0
        }
        XCTAssertEqual(abs(.tempo(.left), 1), [])
        XCTAssertEqual(abs(.tempo(.left), 0.5), [.bpm(120)])
        XCTAssertEqual(abs(.tempo(.left), 1), [.bpm(180)], "towards you (the + end): fastest")
        XCTAssertEqual(abs(.tempo(.left), 0), [.bpm(60)])
        XCTAssertEqual(abs(.tempo(.right), 0.5), [])
        XCTAssertEqual(abs(.tempo(.right), 0), [.swing(0)])
        XCTAssertEqual(abs(.tempo(.right), 1), [.swing(100)])
    }

    private func sweep(_ from: Int, _ to: Int) -> [GrooveAction] {
        stride(from: from, through: to, by: from < to ? 64 : -64).flatMap { abs(.tempo(.left), Double($0) / 16383) }
            + abs(.tempo(.left), Double(to) / 16383)
    }

    func testTempoOutsideSliderRange() {
        state { $0.bpm = 200 }
        let up = sweep(0, 16383)     // 60 -> 180 BPM
        XCTAssertEqual(up.first, .bpm(177), "picked up near the 180 end")
        XCTAssertEqual(up.last, .bpm(180))
        XCTAssertEqual(abs(.tempo(.left), 0.5), [.bpm(120)], "then it follows")

        m.resetPhysicalState()
        state { $0.bpm = 45 }
        let down = sweep(16383, 0)   // 180 -> 60 BPM
        XCTAssertEqual(down.first, .bpm(63), "picked up near the 60 end")
        XCTAssertEqual(down.last, .bpm(60))
    }

    func testCrossfaderSoftTakeover() {
        XCTAssertEqual(abs(.crossfader, 0.5), [.masterFilter(0)], "at the filter (off): it follows")
        XCTAssertEqual(abs(.crossfader, 0), [.masterFilter(-64)])
        XCTAssertEqual(abs(.crossfader, 0.48), [.masterFilter(0)], "the dead zone")
        XCTAssertEqual(abs(.crossfader, 0.52), [])
        XCTAssertEqual(abs(.crossfader, 1), [.masterFilter(63)])
        // the filter set in SLOOP (FX + KNOB 1): the crossfader must come to it
        state { $0.masterFilter = -40 }
        XCTAssertEqual(abs(.crossfader, 0.56), [], "no jump")
        XCTAssertEqual(abs(.crossfader, 0.5), [])
        let x = GrooveboxMapper.filterPosition(-40)
        XCTAssertEqual(GrooveboxMapper.filterValue(x), -40)
        XCTAssertEqual(abs(.crossfader, x - 0.05), [.masterFilter(GrooveboxMapper.filterValue(x - 0.05))], "crossed")
        XCTAssertEqual(GrooveboxMapper.filterValue(0.47), -1)
        XCTAssertEqual(GrooveboxMapper.filterValue(0.53), 1)
    }

    func testFilterPositionInvertsValue() {
        for v in -64...63 { XCTAssertEqual(GrooveboxMapper.filterValue(GrooveboxMapper.filterPosition(v)), v, "\(v)") }
    }

    func testMasterVolume() {
        XCTAssertEqual(abs(.trim(.left), 1), [.masterVolume(127)], "no takeover")
        XCTAssertEqual(abs(.trim(.left), 0), [.masterVolume(0)])
        XCTAssertEqual(abs(.trim(.left), 0), [])
    }

    func testTakeoverMarks() {
        state {
            $0.trackLevel = [64, 104, 104, 127]
            $0.bpm = 120
            $0.swing = 0
            $0.masterFilter = 0
        }
        XCTAssertEqual(m.takeover(for: .channelFader(.left))?.target ?? -1, 64.0 / 127, accuracy: 1e-9)
        XCTAssertEqual(m.takeover(for: .channelFader(.left))?.active, false)
        XCTAssertEqual(m.takeover(for: .channelFader(.right))?.target, 1)
        XCTAssertEqual(m.takeover(for: .tempo(.left))?.target, 0.5)
        XCTAssertEqual(m.takeover(for: .tempo(.right))?.target, 0, "swing 0: the near end")
        XCTAssertEqual(m.takeover(for: .crossfader)?.target, 0.5)
        XCTAssertNil(m.takeover(for: .eqHi(.left)))
        XCTAssertNil(m.takeover(for: .trim(.left)))
        abs(.channelFader(.left), 64.0 / 127)
        XCTAssertEqual(m.takeover(for: .channelFader(.left))?.active, true)
        state { $0.bpm = 200 }
        XCTAssertEqual(m.takeover(for: .tempo(.left))?.target, 1, "above 180: the fast (far) end")
        state { $0.masterFilter = -64 }
        XCTAssertEqual(m.takeover(for: .crossfader)?.target, 0)
        // another track selected: channel 1 is no longer following
        state { $0.selectedTrack = 2 }
        XCTAssertEqual(m.takeover(for: .channelFader(.left))?.active, false)
        XCTAssertEqual(m.takeover(for: .channelFader(.left))?.target, 104.0 / 127)
    }

    // MARK: lights

    func testLights() {
        state {
            $0.keysLit = 1 << 0 | 1 << 26
            $0.keysDim = 1 << 2
            $0.buttonsLit = 1 << UInt32(FM1Button.play.rawValue) | 1 << UInt32(FM1Button.fx.rawValue)
                | 1 << UInt32(FM1Button.save.rawValue)
            $0.selectedTrack = 1
        }
        let l = m.lights()
        XCTAssertEqual(l[.pad(.left, 0)], .on)
        XCTAssertEqual(l[.pad(.left, 1)], .dim)
        XCTAssertEqual(l[.pad(.left, 2)], .off)
        XCTAssertEqual(l[.pad(.right, 7)], .on)
        XCTAssertEqual(l[.play(.left)], .on)
        XCTAssertEqual(l[.play(.right)], .on)
        XCTAssertEqual(l[.sync(.right)], .on)
        XCTAssertEqual(l[.sync(.left)], .off)
        XCTAssertEqual(l[.fxOnOff], .on)
        XCTAssertEqual(l[.headphoneCue(.right)], .on)
        XCTAssertEqual(l[.headphoneCue(.left)], .off)
        XCTAssertEqual(l[.padModeButton(.left, .hotCue)], .on)
        XCTAssertEqual(l[.padModeButton(.left, .sampler)], .off)

        let sampler = m.padLights(for: .sampler)
        XCTAssertEqual(Array(sampler[1][3...]), [.off, .dim, .on, .dim, .dim])
        let low = m.padLights(for: .beatLoop)
        XCTAssertEqual(low[0][4], .on)
        XCTAssertEqual(low[0][3], .off)
        XCTAssertEqual(m.padLights(for: .padFX1), m.padLights(for: .hotCue))
    }

    func testCaptions() {
        XCTAssertEqual(m.caption(for: .pad(.left, 0)), "F3")
        XCTAssertEqual(m.caption(for: .pad(.right, 0)), "G4")
        m.setPadMode(.padFX1, deck: .right)
        XCTAssertEqual(m.caption(for: .pad(.right, 0)), "FX G4")
        XCTAssertEqual(m.caption(for: .pad(.left, 0)), "F3", "each deck in its own mode")
        m.setPadMode(.sampler, deck: .left)
        m.setPadMode(.sampler, deck: .right)
        XCTAssertEqual(m.caption(for: .pad(.left, 0)), "F#3")
        XCTAssertEqual(m.caption(for: .pad(.right, 3)), nil)
        XCTAssertEqual(m.caption(for: .pad(.right, 5)), "TRK 2")
        XCTAssertEqual(m.caption(for: .pad(.right, 7)), "DRUM")
        m.setPadMode(.beatLoop, deck: .left)
        XCTAssertNil(m.caption(for: .pad(.left, 3)))
        let expected: [(DDJControl, String?)] = [
            (.play(.left), "PLAY"), (.cue(.right), "REC"), (.sync(.left), "SEQ"), (.sync(.right), "FX"),
            (.loopIn(.right), "ENV"), (.reloop(.left), "GLO"), (.callLeft(.left), "OCT-"),
            (.headphoneCue(.right), "SAVE"),
            (.jog(.left), "TRACK"), (.jog(.right), "SOUND"), (.eqHi(.left), "K1"), (.eqMid(.right), "K2"),
            (.eqLow(.left), "K3"), (.cfx(.right), "K4"), (.channelFader(.left), "LVL SEL"),
            (.channelFader(.right), "LVL DRM"), (.crossfader, "FILTER"), (.tempo(.left), "BPM"),
            (.tempo(.right), "SWING"), (.trim(.left), "MASTER"), (.trim(.right), nil), (.browse, "SELECT"),
            (.browsePress, "HOME"), (.load(.left), "TRK -"), (.load(.right), "TRK +"), (.beatLeft, "SEL -"),
            (.beatRight, "SEL +"), (.fxSelect, "SOUND +"), (.fxOnOff, "FX"), (.shift(.left), nil),
            (.jogTouch(.left), nil), (.headphoneLevel, nil), (.fxLevel, nil),
        ]
        for (c, s) in expected { XCTAssertEqual(m.caption(for: c), s, "\(c)") }
    }

    func testLightsPerDeckMode() {
        state {
            $0.keysLit = 1 << 1   // F#3: SAMPLER left pad 1, not HOT CUE
            $0.selectedTrack = 3
        }
        m.setPadMode(.sampler, deck: .right)
        let l = m.lights()
        XCTAssertEqual(l[.pad(.left, 0)], .off, "left deck in HOT CUE: F3")
        XCTAssertEqual(l[.pad(.right, 7)], .on, "right deck in SAMPLER: the drums selected")
        XCTAssertEqual(l[.padModeButton(.left, .hotCue)], .on)
        XCTAssertEqual(l[.padModeButton(.right, .sampler)], .on)
        XCTAssertEqual(l[.padModeButton(.right, .hotCue)], .off)
        m.setPadMode(.sampler, deck: .left)
        XCTAssertEqual(m.lights()[.pad(.left, 0)], .on)
    }

    // MARK: releaseAll

    func testReleaseAll() {
        press(.pad(.left, 0), mode: .padFX1)
        press(.pad(.left, 1), mode: .hotCue)
        press(.play(.left))
        press(.play(.right))
        XCTAssertEqual(m.releaseAll(), [.key(0, false), .key(2, false), .barrier,
                                        .button(.fx, false), .button(.play, false)])
        XCTAssertEqual(m.releaseAll(), [])
        XCTAssertEqual(press(.pad(.left, 0), false), [])
        XCTAssertEqual(press(.play(.left), false), [])
        XCTAssertEqual(press(.play(.left)), [.button(.play, true)])
    }
}
