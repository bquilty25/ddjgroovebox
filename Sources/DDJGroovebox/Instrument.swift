// SPDX-License-Identifier: GPL-3.0-only
// What the app plays: TRIGDECK (the native engine) or a cartridge (an FM-1 firmware, SLOOP first). The app gives every
// instrument the same things: the DDJ-400's events, the drawn controller, a strip above it, audio, MIDI and the LEDs.
import AppKit
import DDJMapping

/// The strip above the drawn controller, as tall as it wants to be for a given width.
@MainActor
protocol TopStrip: NSView {
    func preferredHeight(forWidth w: CGFloat, backing: CGFloat) -> CGFloat
}

@MainActor
protocol Instrument: AnyObject {
    var name: String { get }
    /// the rate `render` produces
    var sampleRate: Double { get }
    /// called on the audio thread (and by the fallback clock): C calls only
    var render: RenderFunction { get }
    var strip: TopStrip { get }
    var leds: LEDSource { get }
    /// the output's levels for the meters, 0...1
    var meters: (Double, Double) { get }

    func handle(_ e: DDJEvent)
    /// ~60 times a second: read the instrument's state, update the drawing (faces, captions, lights...)
    func update(_ ddj: DDJ400View, connected: Bool)
    func help(for c: DDJControl) -> String?
    func connectionChanged(_ connected: Bool)
    func releaseAll()
    func saveIfNeeded()
    func shutdown()
}

/// Instruments' thread-safe side (audio, MIDI): called from other threads.
protocol InstrumentIO: AnyObject, Sendable {
    func midiIn(_ m: MIDIMessage)
    /// the instrument's MIDI out, each message with the mach time it belongs at (latency: the output's, in seconds)
    func drainMIDI(outputLatency: Double, _ send: ([UInt8], UInt64) -> Void)
}

/// Every control of the DDJ-400, for captions, faces and help.
let allDDJControls: [DDJControl] = {
    var c: [DDJControl] = [.crossfader, .browse, .browsePress, .beatLeft, .beatRight, .fxSelect, .fxOnOff, .fxLevel,
                           .headphoneLevel, .fxChannel(0), .fxChannel(1), .fxChannel(2)]
    for d in Deck.allCases {
        c += [.play(d), .cue(d), .sync(d), .shift(d), .loopIn(d), .loopOut(d), .reloop(d), .callLeft(d), .callRight(d),
              .jog(d), .jogTouch(d), .tempo(d), .channelFader(d), .trim(d), .eqHi(d), .eqMid(d), .eqLow(d), .cfx(d),
              .headphoneCue(d), .load(d)]
        c += (0..<8).map { DDJControl.pad(d, $0) }
        c += PadMode.allCases.map { DDJControl.padModeButton(d, $0) }
    }
    return c
}()
