// SPDX-License-Identifier: GPL-3.0-only
// CoreMIDI: the DDJ-400 (found by name, reconnected when plugged in again), any other MIDI input played straight into
// SLOOP (a keyboard plays the selected track, as on the FM-1's USB port), and SLOOP's own MIDI out (clock, the
// sequencer) published as a virtual source named "DDJ Groovebox".
import CoreMIDI
import DDJMapping
import Foundation

final class MIDIManager {
    /// a message from the DDJ-400 (called on CoreMIDI's thread)
    var onDDJ: ((MIDIMessage) -> Void)?
    /// a message from another MIDI input (called on CoreMIDI's thread)
    var onOtherInput: ((MIDIMessage) -> Void)?
    /// the DDJ-400 appeared or went away (called on the main thread)
    var onConnectionChange: ((Bool) -> Void)?

    private var client = MIDIClientRef()
    private var ddjInPort = MIDIPortRef()
    private var otherInPort = MIDIPortRef()
    private var outPort = MIDIPortRef()
    private var virtualSource = MIDIEndpointRef()
    private var ddjSource: MIDIEndpointRef = 0
    private var ddjDestination: MIDIEndpointRef = 0
    private var otherSources: Set<MIDIEndpointRef> = []
    private(set) var connected = false
    /// the last MIDISend's result (noErr: CoreMIDI took it) and how many messages went to the DDJ (diagnostics)
    private(set) var lastSendStatus: OSStatus = noErr
    private(set) var sentMessages = 0
    var ddjDestinationName: String { ddjDestination == 0 ? "" : Self.name(of: ddjDestination) }
    private(set) var otherInputNames: [String] = []

    static let deviceNameMatch = "DDJ-400"

    func start() {
        MIDIClientCreateWithBlock("DDJ Groovebox" as CFString, &client) { [weak self] notification in
            if notification.pointee.messageID == .msgSetupChanged {
                DispatchQueue.main.async { self?.rescan() }
            }
        }
        MIDIInputPortCreateWithProtocol(client, "DDJ in" as CFString, ._1_0, &ddjInPort) { [weak self] list, _ in
            self?.parse(list) { self?.onDDJ?($0) }
        }
        MIDIInputPortCreateWithProtocol(client, "Other in" as CFString, ._1_0, &otherInPort) { [weak self] list, _ in
            self?.parse(list) { self?.onOtherInput?($0) }
        }
        MIDIOutputPortCreate(client, "DDJ out" as CFString, &outPort)
        MIDISourceCreateWithProtocol(client, "DDJ Groovebox" as CFString, ._1_0, &virtualSource)
        rescan()
    }

    func rescan() {
        var foundSource: MIDIEndpointRef = 0
        var foundDest: MIDIEndpointRef = 0
        var others: Set<MIDIEndpointRef> = []
        var otherNames: [String] = []
        for i in 0..<MIDIGetNumberOfSources() {
            let src = MIDIGetSource(i)
            if src == virtualSource { continue }
            let name = Self.name(of: src)
            if name.localizedCaseInsensitiveContains(Self.deviceNameMatch) {
                if foundSource == 0 { foundSource = src }
            } else if !name.localizedCaseInsensitiveContains("DDJ Groovebox") {
                others.insert(src)
                otherNames.append(name)
            }
        }
        for i in 0..<MIDIGetNumberOfDestinations() {
            let dst = MIDIGetDestination(i)
            if Self.name(of: dst).localizedCaseInsensitiveContains(Self.deviceNameMatch) { foundDest = dst; break }
        }
        if foundSource != ddjSource {
            if ddjSource != 0 { MIDIPortDisconnectSource(ddjInPort, ddjSource) }
            ddjSource = foundSource
            if ddjSource != 0 { MIDIPortConnectSource(ddjInPort, ddjSource, nil) }
        }
        for s in otherSources.subtracting(others) { MIDIPortDisconnectSource(otherInPort, s) }
        for s in others.subtracting(otherSources) { MIDIPortConnectSource(otherInPort, s, nil) }
        otherSources = others
        otherInputNames = otherNames
        let destChanged = foundDest != ddjDestination
        ddjDestination = foundDest
        // connected: both of the DDJ's ports (input for its controls, output for its LEDs)
        let now = ddjSource != 0 && ddjDestination != 0
        if now != connected {
            connected = now
            onConnectionChange?(now)
        } else if now && destChanged {
            onConnectionChange?(true)                    // (a quick replug between two scans: resend everything)
        }
    }

    /// short messages to the DDJ-400 (its LEDs)
    func sendToDDJ(_ messages: [MIDIMessage]) {
        guard ddjDestination != 0, !messages.isEmpty else { return }
        send(messages.map { [$0.status, $0.data1, $0.data2] }, to: ddjDestination)
    }

    /// the DDJ-400 reports where its faders and knobs are (the request Mixxx sends at start)
    func requestControlPositions() {
        guard ddjDestination != 0 else { return }
        send([[0xF0, 0x00, 0x40, 0x05, 0x00, 0x00, 0x02, 0x06, 0x00, 0x03, 0x01, 0xF7]], to: ddjDestination)
    }

    /// the instrument's MIDI out, to whatever listens to the "DDJ Groovebox" source, at host time `at` (0: now)
    func publish(_ bytes: [UInt8], at: UInt64 = 0) {
        guard virtualSource != 0, !bytes.isEmpty else { return }
        var list = MIDIEventList()
        var word = UInt32(0x2000_0000) | UInt32(bytes[0]) << 16
        if bytes.count > 1 { word |= UInt32(bytes[1]) << 8 }
        if bytes.count > 2 { word |= UInt32(bytes[2]) }
        if bytes[0] >= 0xF0 { word = UInt32(0x1000_0000) | UInt32(bytes[0]) << 16 }   // system real-time: UMP type 1
        let packet = MIDIEventListInit(&list, ._1_0)
        _ = MIDIEventListAdd(&list, MemoryLayout<MIDIEventList>.size, packet, MIDITimeStamp(at), 1, [word])
        MIDIReceivedEventList(virtualSource, &list)
    }

    private func send(_ messages: [[UInt8]], to dest: MIDIEndpointRef) {
        let bufferSize = 4096
        let raw = UnsafeMutableRawPointer.allocate(byteCount: bufferSize, alignment: 8)
        defer { raw.deallocate() }
        let list = raw.assumingMemoryBound(to: MIDIPacketList.self)
        // 64 short messages (or one SysEx) per list: always far below the buffer's size
        var i = 0
        while i < messages.count {
            var packet = MIDIPacketListInit(list)
            let chunk = messages[i..<min(messages.count, i + 64)]
            for m in chunk {
                packet = m.withUnsafeBufferPointer { MIDIPacketListAdd(list, bufferSize, packet, 0, m.count, $0.baseAddress!) }
            }
            lastSendStatus = MIDISend(outPort, dest, list)
            sentMessages += chunk.count
            i += chunk.count
        }
    }

    /// MIDI 1.0 channel voice messages (UMP type 2) and system real-time (type 1: clock, start, continue, stop) out of an
    /// event list (the real-time ones as MIDIMessage(status, 0, 0))
    private func parse(_ list: UnsafePointer<MIDIEventList>, _ body: (MIDIMessage) -> Void) {
        for packet in list.unsafeSequence() {
            let words = packet.words()
            for w in words {
                let type = w >> 28
                let status = UInt8((w >> 16) & 0xFF)
                if type == 1 {
                    if [0xF8, 0xFA, 0xFB, 0xFC].contains(status) { body(MIDIMessage(status, 0, 0)) }
                    continue
                }
                guard type == 2 else { continue }
                body(MIDIMessage(status, UInt8((w >> 8) & 0x7F), UInt8(w & 0x7F)))
            }
        }
    }

    private static func name(of endpoint: MIDIEndpointRef) -> String {
        var cf: Unmanaged<CFString>?
        if MIDIObjectGetStringProperty(endpoint, kMIDIPropertyDisplayName, &cf) == noErr, let s = cf?.takeRetainedValue() {
            return s as String
        }
        if MIDIObjectGetStringProperty(endpoint, kMIDIPropertyName, &cf) == noErr, let s = cf?.takeRetainedValue() {
            return s as String
        }
        return ""
    }
}
