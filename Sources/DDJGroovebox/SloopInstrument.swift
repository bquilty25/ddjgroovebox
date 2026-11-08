// SPDX-License-Identifier: GPL-3.0-only
// The SLOOP cartridge: SLOOP (an FM-1 firmware, vendor/sloop) running on SloopCore, played through the FM-1 panel
// mapping (GrooveboxMapper), with its own screen widened into the strip above the controller (WideScreenView).
import AppKit
import DDJMapping
import SloopCore

@MainActor
final class SloopInstrument: Instrument {
    let name = "SLOOP"
    let groovebox: Groovebox
    let mapper = GrooveboxMapper()
    private let screen = WideScreenView()
    private var lastSave = Date()
    private var state = GrooveState()
    var audioProblem: String?
    var controllerProblem: String?

    var sampleRate: Double { Double(SLOOP_SAMPLE_RATE) }
    let render: RenderFunction = { buf, frames, host in sloop_render_at(buf, frames, host) }
    var strip: TopStrip { screen }
    var leds: LEDSource { mapper }
    var meters: (Double, Double) { (min(1, state.peakLeft), min(1, state.peakRight)) }

    init(flashURL: URL) {
        groovebox = Groovebox(flashURL: flashURL)
    }

    func handle(_ e: DDJEvent) {
        groovebox.apply(mapper.handle(e))
    }

    func update(_ ddj: DDJ400View, connected: Bool) {
        state = groovebox.state()
        mapper.update(state: state)
        if let img = groovebox.screenImage() { screen.image = img }
        let lights = mapper.lights()
        if ddj.lights != lights { ddj.lights = lights }
        let modes: [Deck: PadMode] = [.left: mapper.padMode(.left), .right: mapper.padMode(.right)]
        if ddj.padModes != modes { ddj.padModes = modes }
        if ddj.selectedTrack != state.selectedTrack { ddj.selectedTrack = state.selectedTrack }
        var caps: [DDJControl: String] = [:]
        for c in allDDJControls { if let s = mapper.caption(for: c) { caps[c] = s } }
        if ddj.captions != caps { ddj.captions = caps }
        var takeovers: [DDJControl: (target: Double, active: Bool)] = [:]
        for c in [DDJControl.channelFader(.left), .channelFader(.right), .tempo(.left), .tempo(.right), .crossfader] {
            if let t = mapper.takeover(for: c) { takeovers[c] = t }
        }
        if !connected { for (c, t) in takeovers { ddj.setPosition(c, t.target) } }   // no hardware: show SLOOP's values
        ddj.takeovers = connected ? takeovers : [:]
        var st = state
        st.peakLeft = 0; st.peakRight = 0
        screen.state = st
        screen.padMode = mapper.padMode
        screen.padHelp = Self.padModeHelp(mapper.padMode)
        screen.connected = connected
        screen.notes = [audioProblem, controllerProblem].compactMap { $0 }
    }

    func help(for c: DDJControl) -> String? {
        guard let cap = mapper.caption(for: c) else { return nil }
        if let b = GrooveboxMapper.fm1Button(for: c) {
            return "\(cap): the FM-1's \(b.label) button. Tap it to open its pages; hold it and use the pads for its layer."
        }
        return cap
    }

    func connectionChanged(_ connected: Bool) {
        if connected { mapper.resetPhysicalState() } else { groovebox.apply(mapper.releaseAll()) }
    }

    func releaseAll() { groovebox.apply(mapper.releaseAll()) }

    func saveIfNeeded() {
        guard Date().timeIntervalSince(lastSave) > 5, groovebox.flashDirty else { return }
        lastSave = Date()
        let gb = groovebox
        DispatchQueue.global(qos: .utility).async { gb.save() }
    }

    func shutdown() {
        groovebox.apply(mapper.releaseAll())
        groovebox.save()
    }

    static func padModeHelp(_ m: PadMode) -> String {
        switch m {
        case .hotCue: return "16 white keys (steps, drum sounds, layer tiles)"
        case .beatLoop, .keyboard: return "piano F3 to F4"
        case .beatJump, .keyShift: return "piano G4 to G5"
        case .sampler: return "11 black keys; right pads 5 to 8 select tracks"
        case .padFX1: return "punch-in FX on the white keys"
        case .padFX2: return "punch-in FX on the black keys"
        }
    }
}

extension WideScreenView: TopStrip {
    func preferredHeight(forWidth w: CGFloat, backing: CGFloat) -> CGFloat {
        Self.rows * Self.pixelSize(forHeight: w / 3.6, backing: backing)
    }
}
