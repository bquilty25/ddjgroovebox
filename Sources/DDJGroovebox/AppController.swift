// SPDX-License-Identifier: GPL-3.0-only
// The app around an instrument (TRIGDECK, or a cartridge such as SLOOP): DDJ-400 MIDI, clicks on the drawing and the
// Mac keyboard -> the instrument; the instrument -> the drawing, the strip above it, the DDJ-400's LEDs, audio, MIDI.
import AppKit
import DDJMapping

@MainActor
final class AppController: NSObject, NSWindowDelegate {
    let audio = AudioOutput()
    let midi = MIDIManager()
    private(set) var instrument: Instrument
    private let io: IOBox                               // (swappable: the MIDI threads read it)
    private var ledWriter = DDJ400LEDWriter()
    private let decoderQueue = DispatchQueue(label: "ddj.decoder")
    private nonisolated(unsafe) var decoder = DDJ400Decoder()   // used on decoderQueue only
    private nonisolated(unsafe) var latency: Double = 0.01       // written on main, read by the MIDI out thread (a Double)

    private(set) var window: NSWindow!
    let ddjView = DDJ400View()
    private var layout: GrooveboxLayoutView!
    private var timer: Timer?

    init(instrument: Instrument, io: InstrumentIO) {
        self.instrument = instrument
        self.io = IOBox(io)
        super.init()
    }

    var groovebox: Groovebox? { (instrument as? SloopInstrument)?.groovebox }

    /// Play another instrument: what was held is let go and saved, the strip and the drawing change, audio restarts at
    /// the new instrument's rate. (Each instrument keeps its state: switching back finds it as it was.)
    func switchTo(_ next: Instrument, io nextIO: InstrumentIO) {
        guard next !== instrument else { return }
        instrument.releaseAll()
        instrument.shutdown()
        instrument = next
        io.set(nextIO)
        ddjView.faces = [:]
        ddjView.header = nil
        ddjView.deckTracks = [:]
        ddjView.jogDials = [:]
        ddjView.captions = [:]
        ddjView.lights = [:]
        ddjView.takeovers = [:]
        layout.setStrip(next.strip)
        ledWriter.invalidate()
        audio.start(rate: next.sampleRate, render: next.render)
        window.title = "DDJ Groovebox — \(next.name)"
        next.connectionChanged(midi.connected)
        tick()
    }

    func start() {
        buildWindow()
        audio.onProblemChange = { [weak self] p in
            MainActor.assumeIsolated {
                (self?.instrument as? SloopInstrument)?.audioProblem = p
                (self?.instrument as? TrigInstrument)?.audioProblem = p
            }
        }
        audio.start(rate: instrument.sampleRate, render: instrument.render)

        ddjView.onEvent = { [weak self] e in self?.handle(e, fromHardware: false) }
        ddjView.helpText = { [weak self] c in self?.instrument.help(for: c) }
        layout.onKeyEvent = { [weak self] e in self?.handle(e, fromHardware: false, fromKeyboard: true) }
        midi.onDDJ = { [weak self] m in
            guard let self else { return }
            DispatchQueue.main.async { self.noteAnswer(m) }
            self.decoderQueue.async {
                guard let e = self.decoder.decode(m) else { return }
                DispatchQueue.main.async { self.handle(e, fromHardware: true) }
            }
        }
        midi.onOtherInput = { [io] m in io.midiIn(m) }
        midi.onConnectionChange = { [weak self] connected in
            guard let self else { return }
            self.ddjView.connected = connected
            self.ledWriter.invalidate()
            self.decoderQueue.async { self.decoder.reset() }
            self.instrument.connectionChanged(connected)
            if connected { self.askPositions() } else { self.ddjView.clearPressed(); self.setControllerProblem(nil) }
        }
        // scripted runs (snapshots, measurements) never touch a real controller: one in use would get their LEDs,
        // and they would get its presses
        let env = ProcessInfo.processInfo.environment
        if !Self.scripted {
            midi.start()
            ddjView.connected = midi.connected
            if midi.connected { askPositions() }
            startMIDIOut()
        }

        timer = Timer(timeInterval: 1.0 / 60.0, repeats: true) { [weak self] _ in MainActor.assumeIsolated { self?.tick() } }
        RunLoop.main.add(timer!, forMode: .common)       // (also while a control is dragged or a menu is open)
        tick()
        if let path = env["DDJ_SNAPSHOT"] { scheduleScript(snapshot: path) } else if env["DDJ_DEMO"] != nil { scheduleScript(snapshot: nil) }
        else if env["DDJ_CLICKTEST"] != nil { DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { self.clickTest() } }
    }

    /// The instrument's MIDI out every 3 ms, stamped with the time it belongs at (the audio's own clock + the output's
    /// latency), so clock and notes leave on time whatever the UI is doing. One dedicated thread: a dispatch timer this
    /// fast kept the system's worker threads spinning (measured: the app's largest cost).
    private func startMIDIOut() {
        let noOut = TrigInstrument.diag.contains("nomidiout")
        let t = Thread { [weak self, io, midi] in
            while let s = self, !s.midiOutStopped {
                io.drainMIDI(outputLatency: s.latency) { bytes, at in if !noOut { midi.publish(bytes, at: at) } }
                usleep(3000)
            }
        }
        t.name = "midi out"
        t.qualityOfService = .userInteractive
        t.start()
    }
    private nonisolated(unsafe) var midiOutStopped = false
    private var meterTick = 0

    func handle(_ e: DDJEvent, fromHardware: Bool, fromKeyboard: Bool = false) {
        if fromHardware || fromKeyboard { ddjView.show(e) }
        instrument.handle(e)
    }

    // MARK: is the DDJ-400 hearing us? It answers the position request with every control (~37); a controller whose
    // MIDI input is stuck answers nothing, and shows no LED, while its own controls still work (seen: fixed by a replug)

    private var positionsAsked: Double?
    private var answers: Set<UInt16> = []
    private var unanswered = 0

    /// a fresh check (on connect): asks, and asks again every 2 s until answered
    private func askPositions() {
        unanswered = 0
        ask()
    }

    private func ask() {
        answers = []
        positionsAsked = ProcessInfo.processInfo.systemUptime
        midi.requestControlPositions()
    }

    private func noteAnswer(_ m: MIDIMessage) {
        guard positionsAsked != nil else { return }
        answers.insert(UInt16(m.status) << 8 | UInt16(m.data1))
        if answers.count >= 8 {                          // answered: it hears us
            positionsAsked = nil
            unanswered = 0
            setControllerProblem(nil)
        }
    }

    /// A first request can go unanswered (just after the port connects, or while the MIDI server starts): only three
    /// in a row mean the controller is not receiving. While it says so, it keeps asking (a replug also starts over).
    private func checkAnswered() {
        guard let t = positionsAsked, midi.connected, ProcessInfo.processInfo.systemUptime - t > 2 else { return }
        unanswered += 1
        if unanswered >= 3 { setControllerProblem("DDJ-400 NOT RECEIVING (NO LEDS): UNPLUG IT AND PLUG IT BACK IN") }
        ask()
    }

    private func setControllerProblem(_ p: String?) {
        (instrument as? TrigInstrument)?.controllerProblem = p
        (instrument as? SloopInstrument)?.controllerProblem = p
    }

    private func tick() {
        checkAnswered()
        latency = audio.outputLatency
        instrument.update(ddjView, connected: midi.connected)
        let m = instrument.meters
        meterTick += 1
        if meterTick % 3 == 0 && !TrigInstrument.diag.contains("nometers") { ddjView.meters = m }   // (20 a second)
        if midi.connected { midi.sendToDDJ(ledWriter.messages(mapper: instrument.leds, meters: m)) }
        if !Self.scripted { instrument.saveIfNeeded() }
    }

    /// a scripted run (DDJ_SNAPSHOT, DDJ_DEMO, DDJ_CLICKTEST): no controller, and the user's projects are never written
    static let scripted: Bool = {
        let env = ProcessInfo.processInfo.environment
        return env["DDJ_SNAPSHOT"] != nil || env["DDJ_DEMO"] != nil || env["DDJ_CLICKTEST"] != nil
    }()

    // MARK: the window

    private func buildWindow() {
        let visible = NSScreen.main?.visibleFrame ?? NSRect(x: 0, y: 0, width: 1440, height: 900)
        let size = NSSize(width: min(1320, visible.width * 0.95), height: min(1020, visible.height * 0.95))
        window = NSWindow(contentRect: NSRect(origin: .zero, size: size),
                          styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
        window.title = "DDJ Groovebox — \(instrument.name)"
        window.minSize = NSSize(width: 900, height: 640)
        window.delegate = self
        window.backgroundColor = SloopStyle.bg
        window.appearance = NSAppearance(named: .darkAqua)
        window.titlebarAppearsTransparent = true
        window.collectionBehavior = [.fullScreenPrimary]
        window.colorSpace = .sRGB                       // (plain 8-bit sRGB: see GrooveboxLayoutView.use8Bit)
        layout = GrooveboxLayoutView(strip: instrument.strip, ddj: ddjView)
        window.contentView = layout
        if let hide = ProcessInfo.processInfo.environment["DDJ_HIDE"] {    // (diagnostics: DDJ_HIDE=ddj | screen)
            ddjView.isHidden = hide.contains("ddj")
            instrument.strip.isHidden = hide.contains("screen")
        }
        window.center()
        if let s = ProcessInfo.processInfo.environment["DDJ_WINDOW"]?.split(separator: "x").compactMap({ Double($0) }), s.count == 2 {
            window.setContentSize(NSSize(width: s[0], height: s[1]))   // (scripted runs: DDJ_WINDOW=1512x945)
        } else {
            window.setFrameAutosaveName("DDJGrooveboxMain")    // size and position remembered
        }
        window.makeKeyAndOrderFront(nil)
        window.makeFirstResponder(layout)
    }

    /// a control held with the mouse or the keyboard must not stay down when the window loses focus
    func windowDidResignKey(_ notification: Notification) {
        ddjView.pointerUp()
        layout.releaseKeys()
    }

    func shutdown() {
        timer?.invalidate()
        midiOutStopped = true
        instrument.releaseAll()
        midi.sendToDDJ(ledWriter.allOff())
        audio.stop()
        if !Self.scripted { instrument.shutdown() }      // (it saves)
    }

    /// DDJ_CLICKTEST=1: real mouse events posted to the window (hit testing and all), one control at a time; prints
    /// what each changed and quits.
    private func clickTest() {
        guard let trig = instrument as? TrigInstrument else { print("click test: TRIGDECK only"); exit(1) }
        func click(_ c: DDJControl) {
            guard let p = ddjView.center(of: c) else { print("  \(c): not drawn"); return }
            let w = ddjView.convert(p, to: nil)
            for type in [NSEvent.EventType.leftMouseDown, .leftMouseUp] {
                if let e = NSEvent.mouseEvent(with: type, location: w, modifierFlags: [], timestamp: ProcessInfo.processInfo.systemUptime,
                                              windowNumber: window.windowNumber, context: nil, eventNumber: 0, clickCount: 1, pressure: 1) {
                    window.sendEvent(e)
                }
            }
            let frameView = window.contentView?.superview
            let hit = frameView?.hitTest(frameView!.convert(w, from: nil))   // (hitTest takes its superview's coordinates)
            print("  \(c): hit \(hit.map { String(describing: type(of: $0)) } ?? "nothing")")
        }
        func state() -> String {
            "playing \(trig.engine.playing), track \(trig.mapper.track + 1), step 2 of track \(trig.mapper.track + 1) "
                + "\(trig.mapper.model.step(track: trig.mapper.track, step: 1).trig ? "on" : "off"), page \(trig.mapper.page)"
        }
        print("before: " + state())
        click(.play(.left)); print("after PLAY: " + state())
        click(.pad(.left, 1)); print("after pad 2: " + state())
        click(.reloop(.left)); print("after RELOOP (FLTR page): " + state())
        click(.padModeButton(.right, .beatJump))
        click(.pad(.right, 2)); print("after right BEAT JUMP, pad 3 (track 7): " + state())
        click(.play(.left)); print("after PLAY again: " + state())
        shutdown()
        exit(0)
    }

    func panic() {
        instrument.releaseAll()
        groovebox?.panic()
        if let t = instrument as? TrigInstrument {          // stop, every note let go
            if t.engine.playing { t.engine.apply([.togglePlay]) }
            t.engine.apply([.perf(fx: 0, division: 0, target: 2, on: false, amount: 0), .fill(false), .nudge(0)])
        }
    }

    // MARK: scripted runs (DDJ_SNAPSHOT: a PNG of the window; DDJ_DEMO: keeps running, for measurements)

    private func scheduleScript(snapshot path: String?) {
        let trig: [(Double, DDJEvent)] = [
            (0.3, DDJEvent(.play(.left), .press(true))),
            (0.4, DDJEvent(.play(.left), .press(false))),
            (0.6, DDJEvent(.reloop(.left), .press(true))),                          // the FLTR page
            (0.7, DDJEvent(.reloop(.left), .press(false))),
            (0.9, DDJEvent(.pad(.left, 2), .press(true), padMode: .hotCue)),       // hold step 3 ...
            (1.0, DDJEvent(.eqHi(.left), .absolute(0.5))),
            (1.2, DDJEvent(.eqHi(.left), .absolute(0.8))),                         // ... and lock its cutoff
            (1.5, DDJEvent(.pad(.left, 2), .press(false), padMode: .hotCue)),
            (1.8, DDJEvent(.pad(.right, 1), .press(true), padMode: .beatJump)),     // right deck: TRACKS
            (1.9, DDJEvent(.pad(.right, 1), .press(false), padMode: .beatJump)),
            (2.2, DDJEvent(.crossfader, .absolute(0.3))),
        ]
        let script: [(Double, DDJEvent)] = instrument is TrigInstrument ? trig : [
            (0.5, DDJEvent(.pad(.right, 7), .press(true), padMode: .sampler)),
            (0.6, DDJEvent(.pad(.right, 7), .press(false), padMode: .sampler)),
            (0.8, DDJEvent(.sync(.left), .press(true))),
            (1.0, DDJEvent(.pad(.left, 0), .press(true), padMode: .hotCue)),
            (1.1, DDJEvent(.pad(.left, 0), .press(false), padMode: .hotCue)),
            (1.2, DDJEvent(.pad(.left, 4), .press(true), padMode: .hotCue)),
            (1.3, DDJEvent(.pad(.left, 4), .press(false), padMode: .hotCue)),
            (1.4, DDJEvent(.pad(.right, 0), .press(true), padMode: .hotCue)),
            (1.5, DDJEvent(.pad(.right, 0), .press(false), padMode: .hotCue)),
            (1.6, DDJEvent(.pad(.right, 4), .press(true), padMode: .hotCue)),
            (1.7, DDJEvent(.pad(.right, 4), .press(false), padMode: .hotCue)),
            (1.9, DDJEvent(.sync(.left), .press(false))),
            (2.1, DDJEvent(.play(.left), .press(true))),
            (2.2, DDJEvent(.play(.left), .press(false))),
        ]
        for (t, e) in script {
            DispatchQueue.main.asyncAfter(deadline: .now() + t) { [weak self] in self?.handle(e, fromHardware: true) }
        }
        guard let path else { return }
        DispatchQueue.main.asyncAfter(deadline: .now() + 3.4) { [weak self] in
            guard let self, let view = self.window.contentView,
                  let rep = view.bitmapImageRepForCachingDisplay(in: view.bounds) else { exit(1) }
            DDJLayerView.snapshotting = true
            view.cacheDisplay(in: view.bounds, to: rep)
            DDJLayerView.snapshotting = false
            try? rep.representation(using: .png, properties: [:])?.write(to: URL(fileURLWithPath: path))
            self.shutdown()
            exit(0)
        }
    }
}
