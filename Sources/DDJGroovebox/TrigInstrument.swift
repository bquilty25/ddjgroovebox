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

    func reset(_ c: DDJControl) {
        forceUpdate = true
        engine.apply(mapper.reset(c))
    }

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
        // the tooltip: the control's face, then what it does now (the help line)
        return [f.title, f.value].compactMap { $0 }.joined(separator: ": ") + "\n" + mapper.explain(c)
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
        var steps: [UInt8]                               // the whole track: 0 off, 1 trig, 2 lock only, 3 trig + locks
        var playhead: Int?
        var page: Int                                    // the 16 steps the pads show (framed)
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
            let len = max(1, engine.length(t))
            let steps: [UInt8] = (0..<len).map { s in
                let v = engine.step(track: t, step: s)
                // 1: a trig, 2: locks, 4: a condition (it plays only some times: drawn half full)
                return (v.trig ? 1 : 0) | (v.locks > 0 ? 2 : 0) | (v.trig && v.cond != 0 ? 4 : 0)
            }
            return TrackRow(name: engine.machineName(engine.machine(t).rawValue), color: TrigColors.track[t],
                            active: t == mapper.track, muted: engine.muted(t),
                            level: level, steps: steps, playhead: engine.playingStep(t), page: page)
        }
        let h = mapper.header
        var c = CenterInfo()
        func named(_ p: Int) -> String {                              // "P01 DEEP"
            let n = engine.patternName(p)
            return String(format: "P%02d", p + 1) + (n.isEmpty ? "" : " " + n)
        }
        c.pattern = named(h.pattern) + (h.queued.map { " → " + named($0) } ?? "")
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
        // the stage: each track's level (its figure jumps), the playhead, the transport
        let levels = (0..<8).map { engine.trackLevel($0) }
        let step = engine.playingStep(0).map { $0 % 16 }
        animFrame += 1
        let moving = h.playing || levels.contains { $0 > 0.005 } || h.perf != nil
        if (moving || animFrame % 3 == 0) && animFrame % 2 == 0 { setNeedsDisplay(stageRect.insetBy(dx: -2, dy: -2)) }
        stageLevels = levels
        stageStep = h.playing ? step : nil
        // the page's picture: shown while the page, the track or a value on it changes, and a few seconds after
        // (it shows when what the knobs show changes, not when a lock plays or an LFO moves; drawn from live values)
        let scope = PageScope.read(model: mapper.model, page: h.page, track: mapper.track)
        let key = "\(h.page.rawValue):\(mapper.track):\(mapper.model.machine(mapper.track).rawValue):"
            + (0..<8).map { mapper.knobFace($0).value ?? "" }.joined(separator: "|")
        let now = CACurrentMediaTime()
        if let old = scopeKey, old != key { lastEdit = now }
        scopeKey = key
        if scope != stageScope {
            stageScope = scope
            setNeedsDisplay(stageRect.insetBy(dx: -2, dy: -2))
        }
        if now - lastEdit < Self.scopeShown + 0.5 && animFrame % 2 == 0 { setNeedsDisplay(stageRect.insetBy(dx: -2, dy: -2)) }
        // the scenes' picture: shown while the crossfader moves or a scene is being edited, and a few seconds after
        if let x = lastXf, x != c.crossfader { sceneShown = now }
        if h.sceneEdit != nil { sceneShown = now }
        lastXf = c.crossfader
        if now - sceneShown < Self.scopeShown + 0.5 {
            sceneRows = Self.sceneRows(mapper.model)
            if animFrame % 2 == 0 { setNeedsDisplay(stageRect.insetBy(dx: -2, dy: -2)) }
        }
        // the pack browser (MASTER page, BROWSE): shown while browsing and a few seconds after
        packs = mapper.model.packNames
        packCursor = mapper.packCursor
        packLoaded = mapper.packLoaded
        packReverted = mapper.packReverted
        clockOffset = mapper.now() - CACurrentMediaTime()
        // the step page: a change shows the page map for a moment
        let sp = (mapper.stepPage, mapper.pageCount, mapper.track)
        if sp != stepPageSeen {
            if stepPageSeen.1 > 1 || sp.1 > 1, sp.0 != stepPageSeen.0 || sp.2 != stepPageSeen.2 { pageChanged = now }
            stepPageSeen = sp
        }
        if mapper.lengthChanged != lengthSeen { lengthSeen = mapper.lengthChanged; pageChanged = now }
        if now - pageChanged < Self.pageMapShown + 0.4 { setNeedsDisplay(stageRect.insetBy(dx: -2, dy: -2)) }
        let browsing = mapper.packBrowsed
        if browsing != packBrowsed { packBrowsed = browsing; setNeedsDisplay(stageRect.insetBy(dx: -2, dy: -2)) }
        if mapper.now() - packBrowsed < Self.scopeShown + 0.5 { setNeedsDisplay(stageRect.insetBy(dx: -2, dy: -2)) }
        // only what changed: a track's row, or the middle
        if rows.count != new.count { rows = new; center = c; needsDisplay = true; return }
        for t in 0..<8 where rows[t] != new[t] { setNeedsDisplay(rowRect(t).insetBy(dx: -2, dy: -2)) }
        if c != center { setNeedsDisplay(centerRect.insetBy(dx: -2, dy: -2)) }
        rows = new
        center = c
    }
    private var animFrame = 0
    private var stageLevels: [Double] = Array(repeating: 0, count: 8)
    private var stageStep: Int?
    private var jump: [Double] = Array(repeating: 0, count: 8)   // each figure's height above the floor, 0...1
    private var stageScope: PageScope?
    private var scopeKey: String?
    private var sceneShown: Double = -100
    private var lastXf: Double?
    private var sceneRows: [SceneRow] = []

    /// a parameter a scene holds: its name, range, scene A's and B's values (or nil), its written value
    struct SceneRow { var label: String, color: UInt32, min: Int, max: Int, a: Int?, b: Int?, base: Int }

    static func sceneRows(_ model: TrigModel) -> [SceneRow] {
        var rows: [SceneRow] = []
        for t in 0..<8 {
            for p in 0..<48 {
                let a = model.scene(0, track: t, param: p), b = model.scene(1, track: t, param: p)
                guard a != nil || b != nil, let info = model.info(track: t, param: p) else { continue }
                rows.append(SceneRow(label: "\(t + 1) \(info.name)", color: TrigColors.track[t], min: info.min, max: info.max,
                                     a: a, b: b, base: model.value(track: t, param: p)))
            }
        }
        return rows
    }

    /// The scenes: the crossfader's middle plays the pattern as written; towards A (left) or B (right) every parameter
    /// a scene holds morphs to the scene's value. One row per parameter: A's value, the written value (a thin line),
    /// B's value, and where the crossfader puts it now (the bright marker). (strip.js's _scenes)
    private func drawScenes(_ g: CGContext, _ r: CGRect) {
        let xf = center.crossfader
        let amtA = xf < 0.5 ? (0.5 - xf) * 2 : 0, amtB = xf > 0.5 ? (xf - 0.5) * 2 : 0
        _ = text("SCENE A", r.minX, r.minY, size: 12, color(TrigColors.sceneA))
        _ = text("SCENE B", r.maxX, r.minY, size: 12, color(TrigColors.sceneB), right: true)
        let whereNow = amtA > 0.005 ? "\(Int((amtA * 100).rounded()))% TOWARDS A"
            : amtB > 0.005 ? "\(Int((amtB * 100).rounded()))% TOWARDS B" : "THE PATTERN AS WRITTEN"
        let ww = NSAttributedString(string: whereNow, attributes: [.font: SloopStyle.font(12)]).size().width
        _ = text(whereNow, r.midX - ww / 2, r.minY, size: 12, SloopStyle.fg)
        if sceneRows.isEmpty {
            let lines = ["THIS PATTERN HAS NO SCENES.", "HOLD SCENE A OR SCENE B (THE CUE BUTTONS) AND TURN KNOBS:",
                         "THE CROSSFADER THEN MORPHS THE SOUND TO THEM."]
            for (i, l) in lines.enumerated() { _ = text(l, r.minX, r.minY + 30 + CGFloat(i) * 16, size: 12, SloopStyle.fg2) }
            return
        }
        // every row shown: up to four columns, the rows closer together when even that is not enough
        let top = r.minY + 22, h = r.maxY - top
        var rowH: CGFloat = 16
        var perCol = max(1, Int(h / rowH))
        let cols = min(4, (sceneRows.count + perCol - 1) / perCol)
        if sceneRows.count > perCol * cols {
            perCol = (sceneRows.count + cols - 1) / cols
            rowH = max(9, h / CGFloat(perCol))
        }
        perCol = max(1, min(perCol, Int(h / rowH)))
        let colW = (r.width - CGFloat(cols - 1) * 16) / CGFloat(cols)
        let n = sceneRows.count > perCol * cols ? perCol * cols - 1 : sceneRows.count   // (only if it still does not fit)
        let k = rowH / 16                                             // (the marks shrink with the rows)
        let labelW = min(130, colW * 0.42)
        func fill(_ rect: CGRect, _ c: NSColor) { g.setFillColor(c.cgColor); g.fill(rect) }
        for i in 0..<n {
            let row = sceneRows[i], y = top + CGFloat(i % perCol) * rowH
            let x0 = r.minX + CGFloat(i / perCol) * (colW + 16), bx = x0 + labelW, bw = colW - labelW - 6
            func px(_ v: Double) -> CGFloat {
                bx + CGFloat(row.max > row.min ? (v - Double(row.min)) / Double(row.max - row.min) : 0.5) * bw
            }
            _ = text(row.label, x0, y, size: min(11, rowH - 2), color(row.color))
            fill(CGRect(x: bx, y: y + 6 * k, width: bw, height: 1), SloopStyle.s3)
            if let a = row.a { fill(CGRect(x: px(Double(a)) - 2, y: y + 2 * k, width: 4, height: 9 * k), color(TrigColors.sceneA)) }
            if let b = row.b { fill(CGRect(x: px(Double(b)) - 2, y: y + 2 * k, width: 4, height: 9 * k), color(TrigColors.sceneB)) }
            fill(CGRect(x: px(Double(row.base)) - 0.5, y: y + k, width: 1, height: 11 * k), SloopStyle.fg2)
            var v = Double(row.base)
            if amtA > 0, let a = row.a { v += (Double(a) - v) * amtA }
            if amtB > 0, let b = row.b { v += (Double(b) - v) * amtB }
            fill(CGRect(x: px(v) - 1.5, y: y, width: 3, height: 13 * k), SloopStyle.fg)
        }
        if sceneRows.count > n {
            _ = text("+\(sceneRows.count - n) MORE", r.minX + CGFloat(n / perCol) * (colW + 16), top + CGFloat(n % perCol) * rowH,
                     size: 11, SloopStyle.fg2)
        }
    }
    private var lastEdit = -100.0
    private var lastHit: [Double] = Array(repeating: -100, count: 8)
    static let scopeShown = 4.0                                  // seconds the page's picture stays after an edit
    private var packs: [String] = []
    private var packCursor = 0
    private var packBrowsed: Double = -100
    private var packLoaded: (index: Int, at: Double)?
    private var packReverted: Double?
    private var clockOffset = 0.0                                // mapper clock - CACurrentMediaTime
    private var stepPageSeen = (0, 1, 0)                         // (page, pages, track)
    private var pageChanged = -100.0
    private var lengthSeen = -100.0
    static let pageMapShown = 2.5

    /// The active track's step pages as blocks, the one on the pads lit, the playhead running through them.
    private func drawPageMap(_ g: CGContext, _ r: CGRect) {
        let (page, pages, t) = stepPageSeen
        guard t < rows.count, pages > 0 else { return }
        let row = rows[t], c = NSColor(argb: row.color)
        let title = NSAttributedString(string: "STEPS \(page * 16 + 1)-\(min(row.steps.count, page * 16 + 16))  ·  PAGE \(page + 1)/\(pages)"
                                       + "  ·  LENGTH \(row.steps.count)  (SHIFT + PAGE ±: A BAR)",
                                       attributes: [.font: SloopStyle.font(13), .foregroundColor: SloopStyle.fg])
        title.draw(at: CGPoint(x: r.midX - title.size().width / 2, y: r.minY))
        let gap: CGFloat = 10, top = r.minY + 22
        let bw = min(130, (r.width - gap * CGFloat(pages - 1)) / CGFloat(pages)), bh = max(16, r.height - 30)
        let x0 = r.midX - (bw * CGFloat(pages) + gap * CGFloat(pages - 1)) / 2
        for p in 0..<pages {
            let box = CGRect(x: x0 + CGFloat(p) * (bw + gap), y: top, width: bw, height: bh)
            let on = p == page
            g.setFillColor((on ? c.withAlphaComponent(0.18) : SloopStyle.s2).cgColor); g.fill(box)
            let cw = box.width / 16
            for i in 0..<16 {                                     // the page's steps
                let s = p * 16 + i
                guard s < row.steps.count else { break }
                let v = row.steps[s]
                g.setFillColor((v & 1 != 0 ? c.withAlphaComponent(on ? 1 : 0.5) : SloopStyle.s3).cgColor)
                g.fill(CGRect(x: box.minX + CGFloat(i) * cw + 1, y: box.midY - 4, width: cw - 2, height: 8))
                if row.playhead == s { g.setFillColor(SloopStyle.fg.cgColor); g.fill(CGRect(x: box.minX + CGFloat(i) * cw, y: box.minY + 3, width: cw, height: box.height - 6).insetBy(dx: cw * 0.3, dy: 0)) }
            }
            g.setStrokeColor((on ? SloopStyle.fg : SloopStyle.s3).cgColor); g.setLineWidth(on ? 2 : 1); g.stroke(box)
            let lbl = NSAttributedString(string: "\(p * 16 + 1)-\(min(row.steps.count, p * 16 + 16))", attributes: [.font: SloopStyle.font(10),
                                         .foregroundColor: on ? SloopStyle.fg : SloopStyle.fg2])
            lbl.draw(at: CGPoint(x: box.midX - lbl.size().width / 2, y: box.maxY - 13))
        }
    }
    private var cartPos = 0.0                                   // the carousel's position (eases to the cursor)

    /// The packs as cartridges in a row: the chosen one in the middle, big and lit; loading pushes it into the slot.
    /// (the mapper's clock is the system's uptime: `now` below is the same clock)
    private func drawPacks(_ g: CGContext, _ r: CGRect, now: Double) {
        let n = packs.count
        guard n > 0 else { return }
        let target = Double(packCursor)
        if abs(target - cartPos) > Double(n) / 2 { cartPos = target } else { cartPos += (target - cartPos) * 0.35 }
        g.saveGState()
        defer { g.restoreGState() }
        g.clip(to: r.insetBy(dx: 0, dy: -2))
        let cw = min(120, r.width / 4.2), chh = min(r.height - 30, cw * 0.72)
        let loadAge = packLoaded.map { $0.index == packCursor ? now - $0.at : 99 } ?? 99
        let palette = (0..<8).map { NSColor(argb: TrigColors.track[$0]) }
        for i in 0..<n {
            let off = CGFloat(Double(i) - cartPos)
            guard abs(off) < 3.2 else { continue }
            let chosen = i == packCursor
            let scale = 1 - min(0.3, abs(off) * 0.18)
            let w = cw * scale, h = chh * scale
            var y = r.minY + 4 + (chh - h) / 2
            if chosen && loadAge < 0.5 { y += CGFloat(sin(min(1, loadAge / 0.25) * .pi / 2)) * 6 }   // pushed in
            let box = CGRect(x: r.midX + off * cw * 1.08 - w / 2, y: y, width: w, height: h)
            let a: CGFloat = chosen ? 1 : 0.45
            // the body, a label stripe in a track colour, the name
            g.setFillColor(SloopStyle.s2.withAlphaComponent(a).cgColor)
            let body = CGPath(roundedRect: box, cornerWidth: 6, cornerHeight: 6, transform: nil)
            g.addPath(body); g.fillPath()
            g.setStrokeColor((chosen ? SloopStyle.fg : SloopStyle.s3).cgColor)
            g.setLineWidth(chosen ? 2 : 1)
            g.addPath(body); g.strokePath()
            let stripe = CGRect(x: box.minX + 6, y: box.minY + 6, width: box.width - 12, height: max(4, h * 0.14))
            g.setFillColor(palette[i % 8].withAlphaComponent(a).cgColor); g.fill(stripe)
            let size = max(11, min(26, h * 0.32))
            let name = NSAttributedString(string: packs[i], attributes: [.font: SloopStyle.font(size),
                                                                          .foregroundColor: (chosen ? SloopStyle.fg : SloopStyle.fg2).withAlphaComponent(a)])
            let s = name.size()
            name.draw(at: CGPoint(x: box.midX - s.width / 2, y: box.midY - s.height / 2 + h * 0.06))
            // the cartridge's grip ridges
            g.setFillColor(SloopStyle.s3.withAlphaComponent(a).cgColor)
            for k in 0..<5 { g.fill(CGRect(x: box.midX - 14 + CGFloat(k) * 6, y: box.maxY - 9, width: 3, height: 5)) }
            if chosen && loadAge < 0.35 {                         // the flash as it seats
                g.setFillColor(SloopStyle.fg.withAlphaComponent(CGFloat(0.35 * (1 - loadAge / 0.35))).cgColor)
                g.addPath(body); g.fillPath()
            }
        }
        // what BROWSE and its press do here
        let status: String
        if let rv = packReverted, now - rv < 2 { status = "REVERTED: THE KIT BEFORE" }
        else if let l = packLoaded, l.index == packCursor { status = "LOADED · SHIFT + PUSH: REVERT" }
        else { status = "PUSH: LOAD (THE BEAT STAYS)" }
        let hint = NSAttributedString(string: "PACK \(packCursor + 1)/\(n) · " + status,
                                      attributes: [.font: SloopStyle.font(10), .foregroundColor: SloopStyle.fg2])
        hint.draw(at: CGPoint(x: r.midX - hint.size().width / 2, y: r.minY + 4 + chh + 10))
    }

    /// the empty middle of the strip, under the page line
    private var stageRect: CGRect {
        let r = centerRect
        return CGRect(x: r.minX, y: r.minY + 104, width: r.width, height: max(0, r.maxY - 4 - (r.minY + 104)))
    }

    /// The stage: one little figure per track, in its colour and its machine's shape, jumping when it plays, blinking
    /// when it does not; the floor is the 16 steps with the playhead.
    private func drawStage(_ g: CGContext, _ r: CGRect) {
        guard r.height >= 24, rows.count == 8 else { return }
        let t = CACurrentMediaTime()
        let floorY = r.maxY - 6
        // the floor: 16 steps
        let sw = r.width / 16
        for i in 0..<16 {
            let lit = stageStep == i
            g.setFillColor((lit ? SloopStyle.fg : (i % 4 == 0 ? SloopStyle.s3 : SloopStyle.s2)).cgColor)
            g.fill(CGRect(x: r.minX + CGFloat(i) * sw + 2, y: floorY + 1, width: sw - 4, height: lit ? 4 : 2))
        }
        // (a beat FX's name is on the page line above, not here: it covered the page's picture)
        // a hit lifts a figure at once; it falls back (kept up to date under the page's picture too)
        for i in 0..<8 {
            let lv = min(1, stageLevels[i] * 1.6)
            if lv > jump[i] + 0.15 { lastHit[i] = t }
            jump[i] = lv > jump[i] ? lv : jump[i] * 0.82
        }
        // the pack browser first (MASTER page, BROWSE turned or pushed), on the mapper's clock
        let pt = t + clockOffset, browsed = pt - packBrowsed
        if !packs.isEmpty, browsed < Self.scopeShown + 0.4 {
            let a = CGFloat(min(1, (Self.scopeShown + 0.4 - browsed) / 0.4))
            g.saveGState()
            g.setAlpha(a)
            drawPacks(g, CGRect(x: r.minX, y: r.minY, width: r.width, height: floorY - r.minY - 2), now: pt)
            g.restoreGState()
            if a >= 1 { return }
            g.setAlpha(1 - a)
        }
        // the step pages, just after PAGE - / PAGE + (or a track with several)
        let paged = t - pageChanged
        if paged < Self.pageMapShown + 0.4 {
            let a = CGFloat(min(1, (Self.pageMapShown + 0.4 - paged) / 0.4))
            g.saveGState()
            g.setAlpha(a)
            drawPageMap(g, CGRect(x: r.minX, y: r.minY, width: r.width, height: floorY - r.minY - 2))
            g.restoreGState()
            if a >= 1 { return }
            g.setAlpha(1 - a)
        }
        // the scenes, while the crossfader moves or a scene is edited
        let sceneT = t - sceneShown
        if sceneT < Self.scopeShown + 0.4 {
            let a = CGFloat(min(1, (Self.scopeShown + 0.4 - sceneT) / 0.4))
            g.saveGState()
            g.setAlpha(a)
            drawScenes(g, CGRect(x: r.minX, y: r.minY, width: r.width, height: floorY - r.minY - 2))
            g.restoreGState()
            if a >= 1 { return }
            g.setAlpha(1 - a)
        }
        // the page's picture, while editing; fading back to the figures after
        let shown = t - lastEdit
        if let scope = stageScope, shown < Self.scopeShown + 0.4 {
            let a = CGFloat(min(1, (Self.scopeShown + 0.4 - shown) / 0.4))
            g.saveGState()
            g.setAlpha(a)
            let since = t - lastHit[scope.track]
            scope.draw(g, CGRect(x: r.minX, y: r.minY, width: r.width, height: floorY - r.minY - 2), since: since < 2 ? since : nil)
            g.restoreGState()
            if a >= 1 { return }
            g.setAlpha(1 - a)
        }
        let cell = r.width / 8
        let base = min(r.height * 0.42, cell * 0.5)
        for i in 0..<8 {
            let row = rows[i]
            let j = row.muted ? 0 : jump[i]
            let breathe = 1 + 0.04 * sin(t * 2.2 + Double(i))
            let wobble = center.perf != nil ? CGFloat(sin(t * 11 + Double(i) * 0.8)) * 3 : 0
            let w = base * CGFloat(1 - 0.12 * j), h = base * CGFloat((1 + 0.25 * j) * breathe)
            let lift = CGFloat(j) * (r.height - h - 10)
            let cx = r.minX + (CGFloat(i) + 0.5) * cell + wobble
            let box = CGRect(x: cx - w / 2, y: floorY - 2 - lift - h, width: w, height: h)
            let c = NSColor(argb: row.color)
            g.saveGState()
            if row.muted {
                g.setStrokeColor(c.withAlphaComponent(0.45).cgColor)
                g.setLineWidth(1.5)
            } else {
                g.setFillColor(c.cgColor)
            }
            let path = Self.figure(row.name, in: box, spin: CGFloat(j))
            g.addPath(path)
            if row.muted { g.strokePath() } else { g.fillPath(using: row.name == "SAMPLE" ? .evenOdd : .winding) }   // (a ring)
            g.restoreGState()
            // eyes: two dots (a blink now and then; closed when muted)
            let ey = box.minY + box.height * 0.38, ex = box.width * 0.18, er = max(1.5, base * 0.07)
            let blink = row.muted || (t + Double(i) * 1.7).truncatingRemainder(dividingBy: 4.3) < 0.13
            g.setFillColor((row.muted ? c.withAlphaComponent(0.45) : SloopStyle.bg).cgColor)
            for dx in [-ex, ex] {
                let e = blink ? CGRect(x: box.midX + dx - er, y: ey - 0.75, width: 2 * er, height: 1.5)
                              : CGRect(x: box.midX + dx - er, y: ey - er, width: 2 * er, height: 2 * er)
                if blink { g.fill(e) } else { g.fillEllipse(in: e) }
            }
            // the active track: a marker above its figure
            if row.active {
                g.setFillColor(c.cgColor)
                let my = max(r.minY + 2, box.minY - 10)
                g.move(to: CGPoint(x: cx - 5, y: my)); g.addLine(to: CGPoint(x: cx + 5, y: my)); g.addLine(to: CGPoint(x: cx, y: my + 6))
                g.closePath(); g.fillPath()
            }
        }
    }

    /// a track's figure, by its machine: KICK a circle, SNARE / CLAP a square (it turns as it jumps), HAT a triangle,
    /// TOM a diamond, SYNTH a rounded bar, FM a plus, SAMPLE a ring
    private static func figure(_ machine: String, in b: CGRect, spin: CGFloat) -> CGPath {
        let p = CGMutablePath()
        switch machine {
        case "KICK":
            p.addEllipse(in: b)
        case "SNARE", "CLAP":
            let tr = CGAffineTransform(translationX: b.midX, y: b.midY).rotated(by: spin * .pi / 4)
            p.addRect(CGRect(x: -b.width / 2, y: -b.height / 2, width: b.width, height: b.height), transform: tr)
        case "HAT":
            p.move(to: CGPoint(x: b.midX, y: b.minY)); p.addLine(to: CGPoint(x: b.maxX, y: b.maxY)); p.addLine(to: CGPoint(x: b.minX, y: b.maxY))
            p.closeSubpath()
        case "TOM":
            p.move(to: CGPoint(x: b.midX, y: b.minY)); p.addLine(to: CGPoint(x: b.maxX, y: b.midY))
            p.addLine(to: CGPoint(x: b.midX, y: b.maxY)); p.addLine(to: CGPoint(x: b.minX, y: b.midY))
            p.closeSubpath()
        case "FM":
            let a = b.width / 3, c = b.height / 3
            p.addRect(CGRect(x: b.minX + a, y: b.minY, width: a, height: b.height))
            p.addRect(CGRect(x: b.minX, y: b.minY + c, width: b.width, height: c))
        case "SAMPLE":
            p.addEllipse(in: b)
            p.addEllipse(in: b.insetBy(dx: b.width * 0.3, dy: b.height * 0.3))
        default:                                         // SYNTH and anything else
            p.addRoundedRect(in: b, cornerWidth: min(b.width, b.height) * 0.3, cornerHeight: min(b.width, b.height) * 0.3)
        }
        return p
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
        if needsToDraw(stageRect) { drawStage(g, stageRect) }
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
        // the whole track (up to 4 bars), a gap between bars; the 16 steps the pads show are framed
        let gx = chip.maxX + 10 + nameW, gw = rect.maxX - gx - 14
        let n = r.steps.count, bars = (n + 15) / 16
        let barGap: CGFloat = bars > 1 ? 4 : 0
        let cell = (gw - CGFloat(bars - 1) * barGap) / CGFloat(max(16, n))
        let pad: CGFloat = cell > 8 ? 1 : 0.5
        func x(_ i: Int) -> CGFloat { gx + CGFloat(i) * cell + CGFloat(i / 16) * barGap }
        let cy = rect.minY + rect.height * 0.25, ch = rect.height * 0.5
        for (i, s) in r.steps.enumerated() {
            let cr = CGRect(x: x(i) + pad, y: cy, width: cell - 2 * pad, height: ch)
            let fill: NSColor = s & 1 != 0 ? (r.muted ? c.withAlphaComponent(0.3) : c)
                : s & 2 != 0 ? c.withAlphaComponent(0.25) : (i % 4 == 0 ? SloopStyle.s3 : SloopStyle.s2)
            if s & 4 != 0 {                                       // a condition: half full
                g.setFillColor(c.withAlphaComponent(0.25).cgColor); g.fill(cr)
                g.setFillColor(fill.cgColor); g.fill(CGRect(x: cr.minX, y: cr.midY, width: cr.width, height: cr.height / 2))
            } else { g.setFillColor(fill.cgColor); g.fill(cr) }
            if s & 2 != 0 && cell > 6 { g.setFillColor(SloopStyle.fg.cgColor); g.fill(CGRect(x: cr.midX - 1.5, y: cr.maxY - 5, width: 3, height: 3)) }
            if r.playhead == i { g.setStrokeColor(SloopStyle.fg.cgColor); g.setLineWidth(2); g.stroke(cr.insetBy(dx: -1, dy: -1)) }
        }
        if bars > 1 && r.active {                         // the page on the pads (the active track's): a frame
            let p = min(r.page, bars - 1)
            let fr = CGRect(x: x(p * 16) - 2, y: cy - 4, width: x(min(n, p * 16 + 16) - 1) + cell - x(p * 16) + 4, height: ch + 8)
            g.setStrokeColor(SloopStyle.fg.cgColor)
            g.setLineWidth(1.5)
            g.stroke(fr)
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
        _ = text(c.connected ? "DDJ ●" : "NO DDJ", r.maxX, y, size: 12, c.connected ? SloopStyle.track[1] : SloopStyle.dim, right: true)
        y += 22
        _ = text(c.bpm, r.minX, y, size: 32, SloopStyle.fg)
        _ = text("BPM", r.minX + 70, y + 14, size: 12, SloopStyle.dim)
        _ = text(c.pattern, r.maxX, y, size: 16, SloopStyle.fg, right: true)
        _ = text(c.transport, r.maxX, y + 18, size: 12, c.transport.hasPrefix("●") ? SloopStyle.red : SloopStyle.fg2, right: true)
        y += 40
        _ = text(c.page, r.minX, y, size: 16, color(c.pageColor))
        if let p = c.perf { _ = text("BEAT FX " + p, r.maxX, y, size: 16, NSColor(argb: 0xFF2E7CFF), right: true) }
        else if c.autopilot { _ = text("AUTOPILOT", r.maxX, y, size: 12, SloopStyle.track[2], right: true) }
        if let s = c.scene { _ = text(s == 0 ? "SCENE A EDIT" : "SCENE B EDIT", r.minX + 70, y + 2, size: 12, color(s == 0 ? TrigColors.sceneA : TrigColors.sceneB)) }
        if let p = c.problem { _ = text(p, r.minX, y + 22, size: 12, SloopStyle.red) }   // audio not running: say so
        // (the scene morph is the controller's crossfader, drawn at the bottom of the mixer: not repeated here)
    }
}
