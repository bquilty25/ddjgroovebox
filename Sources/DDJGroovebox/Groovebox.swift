// SPDX-License-Identifier: GPL-3.0-only
// The SLOOP firmware (SloopCore) as a Swift object: actions in, state and screen out, the flash kept on disk.
import AppKit
import DDJMapping
import Foundation
import SloopCore

final class Groovebox: @unchecked Sendable {
    let flashURL: URL
    private var screenBuffer = [UInt32](repeating: 0, count: Int(SLOOP_SCREEN_W * SLOOP_SCREEN_H))
    private var lastScreenFrame: UInt32 = .max
    private var lastScreenPixels: [UInt32] = []

    init(flashURL: URL) {
        self.flashURL = flashURL
        try? FileManager.default.createDirectory(at: flashURL.deletingLastPathComponent(), withIntermediateDirectories: true)
        sloop_init(flashURL.path)
    }

    static var defaultFlashURL: URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first!
        return base.appendingPathComponent("DDJGroovebox", isDirectory: true).appendingPathComponent("sloop-flash.bin")
    }

    func apply(_ actions: [GrooveAction]) {
        for a in actions { apply(a) }
    }

    func apply(_ a: GrooveAction) {
        switch a {
        case let .key(k, down): sloop_key(Int32(k), down ? 1 : 0)
        case let .button(b, down): sloop_button(Int32(b.rawValue), down ? 1 : 0)
        case let .encoder(e, steps): sloop_encoder(Int32(e.rawValue), Int32(steps))
        case .barrier: sloop_input_barrier()
        case let .trackLevel(track, value): sloop_set_track_level(Int32(track), Int32(value))
        case let .masterFilter(v): sloop_set_master_filter(Int32(v))
        case let .bpm(v): sloop_set_bpm(Int32(v))
        case let .swing(v): sloop_set_swing(Int32(v))
        case let .masterVolume(v): sloop_set_master_volume(Int32(v))
        case let .selectTrack(t): sloop_select_track(Int32(t))
        }
    }

    func midiIn(_ m: MIDIMessage) { sloop_midi_in(m.status, m.data1, m.data2) }

    func state() -> GrooveState {
        var s = sloop_state_t()
        sloop_get_state(&s)
        var g = GrooveState()
        g.keysLit = s.keys_lit
        g.keysDim = s.keys_dim
        g.buttonsLit = s.buttons_lit
        g.selectedTrack = Int(s.selected_track)
        g.playing = s.playing != 0
        g.recording = s.recording != 0
        g.bpm = Int(s.bpm)
        g.swing = Int(s.swing)
        g.step = Int(s.step)
        g.trackLevel = withUnsafeBytes(of: s.track_level) { Array($0.bindMemory(to: Int32.self)).map(Int.init) }
        g.masterFilter = Int(s.master_filter)
        g.peakLeft = Double(s.peak_l)
        g.peakRight = Double(s.peak_r)
        return g
    }

    /// The screen as a CGImage, or nil when its pixels have not changed since the last call (SLOOP redraws often
    /// without changing anything: a new image every UI frame would redraw the window 60 times a second).
    func screenImage(force: Bool = false) -> CGImage? {
        let frame = screenBuffer.withUnsafeMutableBufferPointer { sloop_copy_screen($0.baseAddress) }
        if frame == lastScreenFrame && !force { return nil }
        lastScreenFrame = frame
        if !force && screenBuffer == lastScreenPixels { return nil }
        lastScreenPixels = screenBuffer
        let w = Int(SLOOP_SCREEN_W), h = Int(SLOOP_SCREEN_H)
        let data = screenBuffer.withUnsafeBytes { Data($0) }
        guard let provider = CGDataProvider(data: data as CFData) else { return nil }
        return CGImage(width: w, height: h, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: w * 4,
                       space: CGColorSpaceCreateDeviceRGB(),
                       bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipFirst.rawValue | CGBitmapInfo.byteOrder32Little.rawValue),
                       provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
    }

    var flashDirty: Bool { sloop_flash_dirty() != 0 }

    @discardableResult
    func save() -> Bool { sloop_save_flash(flashURL.path) == 0 }

    func panic() { sloop_panic() }

    /// MIDI the firmware sends (clock, the sequencer to MIDI out), each with the mach time it belongs at
    func drainMIDIOut(outputLatency: Double, _ body: ([UInt8], UInt64) -> Void) {
        var b: (UInt8, UInt8, UInt8) = (0, 0, 0)
        var n: Int32 = 0
        var frames: Int64 = 0
        var anchor: UInt64 = 0
        let ticksPerFrame = AudioOutput.ticksPerSecond / Double(SLOOP_SAMPLE_RATE)
        let latency = UInt64(max(0, outputLatency) * AudioOutput.ticksPerSecond)
        while withUnsafeMutableBytes(of: &b, {
            sloop_midi_out_timed($0.baseAddress!.assumingMemoryBound(to: UInt8.self), &n, &frames, &anchor)
        }) != 0 {
            let at = anchor == 0 ? 0 : UInt64(max(0, Double(anchor) + Double(frames) * ticksPerFrame)) &+ latency
            body(Array([b.0, b.1, b.2].prefix(Int(n))), at)
        }
    }
}

extension Groovebox: InstrumentIO {
    func drainMIDI(outputLatency: Double, _ send: ([UInt8], UInt64) -> Void) { drainMIDIOut(outputLatency: outputLatency, send) }
}
