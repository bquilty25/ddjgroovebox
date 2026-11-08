// SPDX-License-Identifier: GPL-3.0-only
// DDJ_PROBE=1: a check of the real DDJ-400 without touching it. Finds it, asks it where its faders and knobs are (it
// answers with one message per control), decodes the answers with the app's decoder, flashes the pads through the LED
// writer, prints what happened and quits. DDJ_PROBE_SECONDS=n listens longer (to try controls by hand).
import AppKit
import DDJMapping

@MainActor
enum HardwareProbe {
    static func run() {
        let midi = MIDIManager()
        let lock = NSLock()
        var raw: [MIDIMessage] = []
        var decoder = DDJ400Decoder()
        var events: [DDJEvent] = []
        midi.onDDJ = { m in
            lock.lock(); defer { lock.unlock() }
            raw.append(m)
            if let e = decoder.decode(m) { events.append(e) }
        }
        midi.start()
        print("DDJ-400 found: \(midi.connected)")
        guard midi.connected else { exit(2) }
        midi.requestControlPositions()

        // the pads: every pad of the current mode lit in turn, then all off
        let mapper = GrooveboxMapper()
        var writer = DDJ400LEDWriter()
        var sent = 0
        for step in 0..<16 {
            var s = GrooveState()
            s.keysLit = UInt32(1) << UInt32(FM1Keyboard.whiteKeys[step])
            mapper.update(state: s)
            let msgs = writer.messages(mapper: mapper, meters: (Double(step) / 15, Double(step) / 15))
            sent += msgs.count
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.05 * Double(step)) { midi.sendToDDJ(msgs) }
        }
        let seconds = Double(ProcessInfo.processInfo.environment["DDJ_PROBE_SECONDS"] ?? "") ?? 2
        DispatchQueue.main.asyncAfter(deadline: .now() + seconds) {
            midi.sendToDDJ(writer.allOff())
            lock.lock()
            print("LED messages sent: \(sent) (CoreMIDI took \(midi.sentMessages), last status \(midi.lastSendStatus)) to '\(midi.ddjDestinationName)'")
            print("messages received: \(raw.count), decoded as controls: \(events.count)")
            var seen = Set<String>()
            for e in events {
                let line = "\(e.control) = \(e.value)\(e.shifted ? " (shift)" : "")"
                if seen.insert(line).inserted { print("  " + line) }
            }
            let unknown = raw.filter { m in DDJ400Decoder().decodeCopy(m) == nil }
            if !unknown.isEmpty { print("not decoded: " + Set(unknown.map(\.description)).sorted().joined(separator: ", ")) }
            lock.unlock()
            exit(0)
        }
    }
}

private extension DDJ400Decoder {
    /// a fresh decode of one message (an LSB alone is not decodable: counted with its MSB in the summary above)
    func decodeCopy(_ m: MIDIMessage) -> DDJEvent? {
        var d = DDJ400Decoder()
        if m.status & 0xF0 == 0xB0, (0x20...0x3F).contains(m.data1) {
            _ = d.decode(MIDIMessage(m.status, m.data1 - 0x20, 0))
        }
        return d.decode(m)
    }
}
