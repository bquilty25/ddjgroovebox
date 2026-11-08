// SPDX-License-Identifier: GPL-3.0-only
// TRIGDECK: the native groovebox (TrigCore) played from the DDJ-400 (TrigMapper). The controller is the screen: every
// control shows its face (job, value, colour, light); the strip above shows all 8 tracks' steps and the transport.
import AppKit
import DDJMapping
import TrigCore

@MainActor
final class TrigInstrument: Instrument {
    let name = "TRIGDECK"
    let engine: TrigEngine
    let mapper: TrigMapper
    private let stripView = TrigStripView()
    var audioProblem: String?
    var controllerProblem: String?

    var sampleRate: Double { Double(TD_SAMPLE_RATE) }
    let render: RenderFunction = { buf, frames, host in td_render(buf, frames, host) }
    var strip: TopStrip { stripView }
    var leds: LEDSource { mapper }
    var meters: (Double, Double) { (min(1, Double(engine.state.peak_l)), min(1, Double(engine.state.peak_r))) }

    init(projectURL: URL) {
        engine = TrigEngine(projectURL: projectURL)
        mapper = TrigMapper(model: engine)
        stripView.onSelectTrack = { [weak self] t in
            guard let self else { return }
            self.forceUpdate = true
            self.engine.apply(self.mapper.select(track: t))
        }
    }

    private var tickCount = 0
    private var forceUpdate = true                       // (an event shows at the next tick, not the one after)

    func handle(_ e: DDJEvent) {
        forceUpdate = true
        let actions = mapper.handle(e)
        engine.apply(actions)
        // a resample lands on the active track when it is a SAMPLE track
        if actions.contains(where: { if case .capture = $0 { return true }; return false }),
           let cap = engine.lastCapture, engine.machine(mapper.track) == .sample {
            td_set_sample(Int32(mapper.track), Int32(cap.slot))
        }
    }

    func update(_ ddj: DDJ400View, connected: Bool) {
        engine.apply(mapper.tick())
        engine.refresh()
        // the drawing follows at 20 frames a second, at once after an event (the playhead moves 8 times a second at
        // 122 BPM): every repaint costs a Core Animation commit of the large controller (measured: CPU scales with
        // the number of commits, not their area)
        tickCount += 1
        guard tickCount % 3 == 0 || forceUpdate else { return }
        forceUpdate = false
        var faces: [DDJControl: ControlFace] = [:]
        for c in allDDJControls { if let f = mapper.face(c) { faces[c] = f } }
        if Self.debugFaces {                             // (DDJ_DEBUG_FACES=1: which faces change, for performance work)
            let changed = faces.filter { ddj.faces[$0.key] != $0.value }.map { "\($0.key)" }
            if !changed.isEmpty { print("faces changed:", changed.sorted().joined(separator: " ")) }
        }
        if ddj.faces != faces && !(Self.diag.contains("nofaces") && !ddj.faces.isEmpty) { ddj.faces = faces }
        let modes: [Deck: PadMode] = [.left: mapper.padMode(.left), .right: mapper.padMode(.right)]
        if ddj.padModes != modes { ddj.padModes = modes }
        ddj.header = mapper.header
        // levels move every frame while music plays: the jogs' and the strip's meters update 20 times a second, in
        // 8 steps (a full 60 Hz redraw of those large areas was most of the app's CPU)
        frame += 1
        let meterFrame = frame % 3 == 0
        var decks: [Deck: [DeckTrackFace]] = [:]
        for d in Deck.allCases {
            let base = d == .left ? 0 : 4
            decks[d] = (base..<base + 4).map { t in
                let shown = meterFrame ? (engine.trackLevel(t) * 8).rounded() / 8 : (lastLevels[t] ?? 0)
                lastLevels[t] = shown
                return DeckTrackFace(name: engine.machineName(engine.machine(t).rawValue),
                                     color: NSColor(argb: TrigColors.track[t]), level: shown,
                                     active: t == mapper.track, muted: engine.muted(t))
            }
        }
        if ddj.deckTracks != decks && !Self.diag.contains("nojogs") { ddj.deckTracks = decks }
        let dials = Dictionary(uniqueKeysWithValues: Deck.allCases.map { ($0, mapper.jogDialPosition($0)) })
        if ddj.jogDials != dials { ddj.jogDials = dials }
        stripView.update(engine: engine, mapper: mapper, connected: connected, problem: audioProblem ?? controllerProblem,
                         meters: meterFrame)
    }
    private var frame = 0
    private static let debugFaces = ProcessInfo.processInfo.environment["DDJ_DEBUG_FACES"] != nil
    /// (DDJ_DIAG=nojogs,nofaces,nometers: switch parts of the drawing's updates off, for performance work)
    static let diag = ProcessInfo.processInfo.environment["DDJ_DIAG"] ?? ""
    private var lastLevels: [Int: Double] = [:]

    func help(for c: DDJControl) -> String? {
        guard let f = mapper.face(c) else { return nil }
        return [f.title, f.value].compactMap { $0 }.joined(separator: ": ")
    }

    func connectionChanged(_ connected: Bool) {
        if !connected { engine.apply(mapper.releaseAll()) }
    }

    func releaseAll() { engine.apply(mapper.releaseAll()) }
    func saveIfNeeded() { engine.saveIfDirty() }
    func shutdown() {
        engine.apply(mapper.releaseAll())
        engine.saveIfDirty(force: true)
    }
}

extension NSColor {
    convenience init(argb: UInt32) {
        self.init(srgbRed: CGFloat((argb >> 16) & 0xFF) / 255, green: CGFloat((argb >> 8) & 0xFF) / 255,
                  blue: CGFloat(argb & 0xFF) / 255, alpha: CGFloat((argb >> 24) & 0xFF) / 255)
    }
}

/// The strip above the controller, in the controller's three columns: deck A's 4 tracks over deck A, the transport
/// and the scenes over the mixer, deck B's 4 tracks over deck B. Each track: its colour, machine, the steps of the page
/// shown (trigs, locks, the playhead), mute, level.
@MainActor
final class TrigStripView: NSView, TopStrip {
    private var rows: [TrackRow] = []
    private var center = CenterInfo()
    struct TrackRow: Equatable {
        var name: String, color: UInt32, active: Bool, muted: Bool, level: Int
        var steps: [UInt8]                               // 0 off, 1 trig, 2 lock only, 3 trig + locks
        var playhead: Int?
    }
    struct CenterInfo: Equatable {
        var pattern = "P01", bpm = "120", transport = "■", page = "SRC", pageColor: UInt32 = 0xFFFFFFFF
        var crossfader = 0.5, autopilot = false, perf: String?, connected = false, scene: Int?
        var problem: String?
    }

    override var isFlipped: Bool { true }
    /// a click on a track's row selects it
    var onSelectTrack: ((Int) -> Void)?

    override func mouseDown(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        if let t = (0..<8).first(where: { rowRect($0).contains(p) }) { onSelectTrack?(t) }
    }

    override func resetCursorRects() {
        for t in 0..<8 { addCursorRect(rowRect(t), cursor: .pointingHand) }
    }

    func preferredHeight(forWidth w: CGFloat, backing: CGFloat) -> CGFloat { (w * 0.17).rounded() }

    func update(engine: TrigEngine, mapper: TrigMapper, connected: Bool, problem: String? = nil, meters: Bool = true) {
        let page = mapper.stepPage
        let new: [TrackRow] = (0..<8).map { t in
            let level = meters || t >= rows.count ? Int((engine.trackLevel(t) * 10).rounded()) : rows[t].level
            let len = engine.length(t)
            let steps: [UInt8] = (0..<16).map { i in
                let s = page * 16 + i
                guard s < len else { return 255 }
                let v = engine.step(track: t, step: s)
                return v.trig ? (v.locks > 0 ? 3 : 1) : (v.locks > 0 ? 2 : 0)
            }
            let ph = engine.playingStep(t).flatMap { $0 / 16 == page ? $0 % 16 : nil }
            return TrackRow(name: engine.machineName(engine.machine(t).rawValue), color: TrigColors.track[t],
                            active: t == mapper.track, muted: engine.muted(t),
                            level: level, steps: steps, playhead: ph)
        }
        let h = mapper.header
        var c = CenterInfo()
        c.pattern = String(format: "P%02d", h.pattern + 1) + (h.queued.map { String(format: " → P%02d", $0 + 1) } ?? "")
        c.bpm = String(format: "%.0f", h.bpm)
        c.transport = h.recording ? "● REC" : h.playing ? "▶ PLAY" : "■ STOP"
        c.page = h.page.name
        c.pageColor = TrigColors.page[h.page.rawValue]
        c.crossfader = Double(engine.state.crossfader)
        c.autopilot = engine.autopilot
        c.perf = h.perf
        c.connected = connected
        c.scene = h.sceneEdit
        c.problem = problem
        // only what changed: a track's row, or the middle
        if rows.count != new.count { rows = new; center = c; needsDisplay = true; return }
        for t in 0..<8 where rows[t] != new[t] { setNeedsDisplay(rowRect(t).insetBy(dx: -2, dy: -2)) }
        if c != center { setNeedsDisplay(centerRect.insetBy(dx: -2, dy: -2)) }
        rows = new
        center = c
    }

    private var mixerX: CGFloat { (bounds.width * DDJ400View.columnFractions.mixerStart).rounded() }
    private var rightX: CGFloat { (bounds.width * DDJ400View.columnFractions.rightStart).rounded() }
    private func rowRect(_ t: Int) -> CGRect {
        let rowH = (bounds.height - 16) / 4
        let x0: CGFloat = t < 4 ? 8 : rightX + 8
        let colW = (t < 4 ? mixerX : bounds.width - rightX) - 16
        return CGRect(x: x0, y: 8 + CGFloat(t % 4) * rowH, width: colW, height: rowH - 4)
    }
    private var centerRect: CGRect { CGRect(x: mixerX + 10, y: 8, width: rightX - mixerX - 20, height: bounds.height - 16) }

    override func draw(_ dirtyRect: NSRect) {
        guard let g = NSGraphicsContext.current?.cgContext else { return }
        let W = bounds.width, H = bounds.height
        let mx = mixerX, rx = rightX
        _ = W
        g.setFillColor(SloopStyle.bg.cgColor); g.fill(bounds)
        // the three columns, open at the bottom: the controller continues below
        g.setFillColor(SloopStyle.s1.cgColor)
        g.fill(CGRect(x: 0, y: 0, width: W, height: H))
        g.setStrokeColor(SloopStyle.line.cgColor); g.setLineWidth(1)
        for x in [0.5, mx + 0.5, rx + 0.5, W - 0.5] { g.move(to: CGPoint(x: x, y: H)); g.addLine(to: CGPoint(x: x, y: 0.5)) }
        g.move(to: CGPoint(x: 0, y: 0.5)); g.addLine(to: CGPoint(x: W, y: 0.5))
        g.strokePath()

        for t in 0..<8 where t < rows.count && needsToDraw(rowRect(t)) {
            drawRow(g, rows[t], number: t + 1, rect: rowRect(t))
        }
        if needsToDraw(centerRect) { drawCenter(g, centerRect) }
        _ = (mx, rx, H)
    }

    private func color(_ argb: UInt32, _ a: CGFloat = 1) -> NSColor { NSColor(argb: argb).withAlphaComponent(a) }

    private func text(_ s: String, _ x: CGFloat, _ y: CGFloat, size: CGFloat, _ c: NSColor, right: Bool = false) -> CGFloat {
        let a = NSAttributedString(string: s, attributes: [.font: SloopStyle.font(size), .foregroundColor: c])
        let w = a.size().width
        a.draw(at: CGPoint(x: right ? x - w : x, y: y))
        return w
    }

    private func drawRow(_ g: CGContext, _ r: TrackRow, number: Int, rect: CGRect) {
        let size: CGFloat = rect.height > 40 ? 16 : 12
        let c = color(r.color)
        // number chip and machine
        let chip = CGRect(x: rect.minX, y: rect.minY + 2, width: rect.height - 4, height: rect.height - 4)
        g.setFillColor((r.active ? c : c.withAlphaComponent(0.35)).cgColor)
        g.fill(chip)
        _ = text("\(number)", chip.midX - size * 0.25, chip.midY - size * 0.5, size: size, r.active ? SloopStyle.bg : SloopStyle.fg)
        let nameW: CGFloat = min(96, rect.width * 0.22)
        _ = text(r.muted ? "MUTED" : r.name, chip.maxX + 6, rect.midY - size * 0.5, size: size,
                 r.muted ? SloopStyle.dim : (r.active ? SloopStyle.fg : SloopStyle.fg2))
        // the 16 steps of the page shown
        let gx = chip.maxX + 10 + nameW, gw = rect.maxX - gx - 14
        let cell = gw / 16
        for (i, s) in r.steps.enumerated() {
            let cr = CGRect(x: gx + CGFloat(i) * cell + 1, y: rect.minY + rect.height * 0.25, width: cell - 2, height: rect.height * 0.5)
            switch s {
            case 255: continue
            case 1, 3: g.setFillColor((r.muted ? c.withAlphaComponent(0.3) : c).cgColor)
            case 2: g.setFillColor(c.withAlphaComponent(0.25).cgColor)
            default: g.setFillColor((i % 4 == 0 ? SloopStyle.s3 : SloopStyle.s2).cgColor)
            }
            g.fill(cr)
            if s >= 2 { g.setFillColor(SloopStyle.fg.cgColor); g.fill(CGRect(x: cr.midX - 1.5, y: cr.maxY - 5, width: 3, height: 3)) }
            if r.playhead == i { g.setStrokeColor(SloopStyle.fg.cgColor); g.setLineWidth(2); g.stroke(cr.insetBy(dx: -1, dy: -1)) }
        }
        // level
        let lv = CGRect(x: rect.maxX - 8, y: rect.minY + 2, width: 5, height: rect.height - 4)
        g.setFillColor(SloopStyle.s2.cgColor); g.fill(lv)
        let h = lv.height * CGFloat(min(10, r.level)) / 10
        g.setFillColor(c.cgColor); g.fill(CGRect(x: lv.minX, y: lv.maxY - h, width: lv.width, height: h))
    }

    private func drawCenter(_ g: CGContext, _ r: CGRect) {
        let c = center
        var y = r.minY
        // TRIGDECK, in the track colours
        var x = r.minX
        for (i, ch) in "TRIGDECK".enumerated() { x += text(String(ch), x, y, size: 16, color(TrigColors.track[i])) }
        _ = text(c.connected ? "DDJ-400 ●" : "NO DDJ-400", r.maxX, y, size: 12, c.connected ? SloopStyle.track[1] : SloopStyle.dim, right: true)
        y += 22
        _ = text(c.bpm, r.minX, y, size: 32, SloopStyle.fg)
        _ = text("BPM", r.minX + 70, y + 14, size: 12, SloopStyle.dim)
        _ = text(c.pattern, r.maxX, y, size: 16, SloopStyle.fg, right: true)
        _ = text(c.transport, r.maxX, y + 18, size: 12, c.transport.hasPrefix("●") ? SloopStyle.red : SloopStyle.fg2, right: true)
        y += 40
        _ = text(c.page, r.minX, y, size: 16, color(c.pageColor))
        if let p = c.perf { _ = text(p, r.maxX, y, size: 16, NSColor(argb: 0xFF2E7CFF), right: true) }
        else if c.autopilot { _ = text("AUTOPILOT", r.maxX, y, size: 12, SloopStyle.track[2], right: true) }
        if let s = c.scene { _ = text(s == 0 ? "SCENE A EDIT" : "SCENE B EDIT", r.minX + 70, y + 2, size: 12, color(s == 0 ? TrigColors.sceneA : TrigColors.sceneB)) }
        if let p = c.problem { _ = text(p, r.minX, y + 22, size: 12, SloopStyle.red) }   // audio not running: say so
        // the scene morph: A ◀ crossfader ▶ B
        let by = r.maxY - 12
        let bar = CGRect(x: r.minX + 16, y: by, width: r.width - 32, height: 6)
        g.setFillColor(SloopStyle.s2.cgColor); g.fill(bar)
        let mid = bar.midX, pos = bar.minX + CGFloat(c.crossfader) * bar.width
        g.setFillColor(color(c.crossfader < 0.5 ? TrigColors.sceneA : TrigColors.sceneB).cgColor)
        g.fill(CGRect(x: min(mid, pos), y: bar.minY, width: abs(pos - mid), height: bar.height))
        g.setFillColor(SloopStyle.fg.cgColor); g.fill(CGRect(x: pos - 1.5, y: bar.minY - 3, width: 3, height: bar.height + 6))
        _ = text("A", r.minX, by - 5, size: 12, color(TrigColors.sceneA))
        _ = text("B", r.maxX, by - 5, size: 12, color(TrigColors.sceneB), right: true)
    }
}
