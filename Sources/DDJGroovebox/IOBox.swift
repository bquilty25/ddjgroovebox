// SPDX-License-Identifier: GPL-3.0-only
import DDJMapping
import Foundation

/// The instrument's thread-safe side, swappable while MIDI threads read it (switching instruments).
final class IOBox: InstrumentIO, @unchecked Sendable {
    private let lock = NSLock()
    private var current: InstrumentIO
    init(_ io: InstrumentIO) { current = io }
    func set(_ io: InstrumentIO) { lock.lock(); current = io; lock.unlock() }
    private var io: InstrumentIO { lock.lock(); defer { lock.unlock() }; return current }
    func midiIn(_ m: MIDIMessage) { io.midiIn(m) }
    func drainMIDI(outputLatency: Double, _ send: ([UInt8], UInt64) -> Void) { io.drainMIDI(outputLatency: outputLatency, send) }
}
