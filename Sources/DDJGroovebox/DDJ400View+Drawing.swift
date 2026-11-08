// SPDX-License-Identifier: GPL-3.0-only
// Drawing of the on-screen DDJ-400. Flat: s1 columns open at the top, s2 controls with s3 edges, small radii.
// Each control shows its look: its face (title, value, arc, colour, light, badge, outline) or, without one, its
// caption, light and accent. Titles are 16 px design and never under 12 pt on screen (wrapped or truncated with an
// ellipsis, never shrunk); values are a smaller line, briefly enlarged while their control moves. The DDJ's printed
// labels are smaller still, in a light grey.
// The static parts (panels, scales, fader slots, knob tracks) are cached in a bitmap per size and backing scale, so
// a partial redraw only paints the controls in the dirty area over it.

import AppKit
import DDJMapping

extension DDJ400View {

    enum Align { case left, center, right }

    /// What a control shows now: its face, or its caption / light / accent.
    struct Look {
        var title: String?
        var value: String?
        var color: NSColor?
        var light: LightLevel
        var badge: String?
        var outlined: Bool
        var arc: Double?
        var hasFace: Bool
        /// a face with an empty title: nothing there (a step past the track's length)
        var empty: Bool { hasFace && (title ?? "").isEmpty }
    }

    func look(_ c: DDJControl) -> Look {
        if let f = faces[c] {
            return Look(title: f.title.isEmpty ? "" : f.title.uppercased(), value: f.value.map { $0.uppercased() },
                        color: f.color.map(argb), light: f.light, badge: f.badge, outlined: f.outlined, arc: f.arc, hasFace: true)
        }
        return Look(title: caption(c), value: nil, color: knobAccents[c], light: lights[c] ?? .off, badge: nil,
                    outlined: false, arc: nil, hasFace: false)
    }

    /// an ARGB colour from a face
    func argb(_ v: UInt32) -> NSColor {
        if let c = colorCache[v] { return c }
        let c = NSColor(srgbRed: CGFloat((v >> 16) & 0xFF) / 255, green: CGFloat((v >> 8) & 0xFF) / 255,
                        blue: CGFloat(v & 0xFF) / 255, alpha: CGFloat((v >> 24) & 0xFF) / 255)
        colorCache[v] = c
        return c
    }

    // MARK: - Text metrics

    /// title size in design units: 16, or more when the drawing is small, so titles stay >= 12 pt
    var capSize: CGFloat { max(16, 12 / fit.scale) }
    /// value size (>= 9 pt on screen)
    var valSize: CGFloat { max(12, 9 / fit.scale) }
    /// printed-label and badge size (>= 8 pt on screen)
    var lblSize: CGFloat { max(10, 8 / fit.scale) }
    var labelColor: NSColor { SloopStyle.fg2.withAlphaComponent(0.8) }

    func font(_ size: CGFloat) -> NSFont {
        let k = (size * 64).rounded() / 64
        if let f = fontCache[k] { return f }
        let f = SloopStyle.font(k)
        fontCache[k] = f
        return f
    }

    func lineHeight(_ size: CGFloat) -> CGFloat {
        let f = font(size)
        return ceil(f.ascender - f.descender)
    }

    /// Terminus is monospaced: the advance per character at a size
    func charWidth(_ size: CGFloat) -> CGFloat {
        if advanceRatio == 0 {
            let w = ("0000000000" as NSString).size(withAttributes: [.font: font(16)]).width
            advanceRatio = max(0.3, w / 10 / 16)
        }
        return size * advanceRatio
    }

    func maxChars(_ width: CGFloat, _ size: CGFloat) -> Int { max(1, Int((width + 0.5) / charWidth(size))) }

    func textWidth(_ s: String, _ size: CGFloat) -> CGFloat {
        (s as NSString).size(withAttributes: [.font: font(size)]).width
    }

    nonisolated static func truncate(_ s: String, _ n: Int) -> String {
        s.count <= n ? s : String(s.prefix(max(0, n - 1))) + "\u{2026}"
    }

    /// greedy word wrap into at most `maxLines` lines of `maxChars`; the last line is truncated with an ellipsis
    nonisolated static func wrap(_ s: String, maxChars: Int, maxLines: Int) -> [String] {
        guard maxChars > 0, maxLines > 0, !s.isEmpty else { return [] }
        if s.count <= maxChars { return [s] }
        if maxLines == 1 { return [truncate(s, maxChars)] }
        var lines: [String] = []
        var cur = ""
        for word in s.split(separator: " ").map(String.init) {
            if cur.isEmpty {
                cur = word
            } else if cur.count + 1 + word.count <= maxChars {
                cur += " " + word
            } else {
                lines.append(cur)
                cur = word
            }
            while cur.count > maxChars {
                lines.append(String(cur.prefix(maxChars)))
                cur = String(cur.dropFirst(maxChars))
            }
        }
        if !cur.isEmpty { lines.append(cur) }
        if lines.count > maxLines {
            var kept = Array(lines.prefix(maxLines))
            kept[maxLines - 1] = truncate(lines[(maxLines - 1)...].joined(separator: " "), maxChars)
            return kept
        }
        return lines
    }

    /// Draws one line of text with its vertical centre at y. `shrink`: scale it down to fit `maxWidth` (printed
    /// labels only); otherwise callers truncate first.
    func drawText(_ s: String, size: CGFloat, _ color: NSColor, x: CGFloat, y: CGFloat, align: Align = .center,
                  maxWidth: CGFloat = .greatestFiniteMagnitude, shrink: Bool = false) {
        guard !s.isEmpty else { return }
        var attrs: [NSAttributedString.Key: Any] = [.font: font(size), .foregroundColor: color]
        let str = s as NSString
        var sz = str.size(withAttributes: attrs)
        if shrink, sz.width > maxWidth {
            attrs[.font] = font(max(5, size * maxWidth / sz.width))
            sz = str.size(withAttributes: attrs)
        }
        let ox: CGFloat
        switch align {
        case .left: ox = x
        case .center: ox = x - sz.width / 2
        case .right: ox = x - sz.width
        }
        str.draw(at: NSPoint(x: ox, y: y - sz.height / 2), withAttributes: attrs)
    }

    /// one line truncated to a width
    func drawLine(_ s: String, size: CGFloat, _ color: NSColor, x: CGFloat, y: CGFloat, align: Align = .center, width: CGFloat) {
        drawText(Self.truncate(s, maxChars(width, size)), size: size, color, x: x, y: y, align: align)
    }

    func drawLabel(_ s: String, x: CGFloat, y: CGFloat, align: Align = .center, color: NSColor? = nil,
                   maxWidth: CGFloat = .greatestFiniteMagnitude) {
        drawText(s, size: lblSize, color ?? labelColor, x: x, y: y, align: align, maxWidth: maxWidth, shrink: true)
    }

    // MARK: - Title geometry

    private func sideGap(_ it: Item) -> CGFloat {
        if case .knob = it.kind { return 10 }
        return 8
    }

    /// where an item's title goes, in design space (nil: on the control)
    func captionRect(_ it: Item) -> CGRect? {
        let lh = lineHeight(capSize)
        switch it.caption {
        case .at(let p, let w):
            return CGRect(x: p.x - w / 2, y: p.y - lh / 2, width: w, height: lh)
        case .above(let w):
            return CGRect(x: it.rect.midX - w / 2, y: it.rect.minY - 5 - lh, width: w, height: lh)
        case .side(let left, let w):
            let h = lh + lineHeight(capSize) + lineHeight(lblSize) + 4
            let edge = left ? it.rect.minX - sideGap(it) : it.rect.maxX + sideGap(it)
            return CGRect(x: left ? edge - w : edge, y: it.rect.midY - h / 2, width: w, height: h)
        case .inside:
            return nil
        }
    }

    func caption(_ c: DDJControl) -> String? {
        guard let s = captions[c], !s.isEmpty else { return nil }
        return s.uppercased()
    }

    var trackColor: NSColor { SloopStyle.track[min(SloopStyle.track.count - 1, max(0, selectedTrack))] }

    func isHeld(_ it: Item) -> Bool {
        guard let h = heldButton else { return false }
        if h == it.control { return true }
        if case .padModeButton = h, let i = itemIndex(for: h) { return items[i].control == it.control }
        return false
    }

    func titleColor(_ it: Item, _ lk: Look) -> NSColor {
        if isHeld(it) { return heldColor }
        if let c = lk.color { return c }
        if !lk.hasFace, let s = lk.title, s.hasPrefix("REC") || s.hasPrefix("FX ") { return SloopStyle.red }
        return SloopStyle.fg
    }

    /// black text on light colours, white on dark ones
    func ink(on c: NSColor) -> NSColor {
        let s = c.usingColorSpace(.sRGB) ?? c
        let lum = 0.2126 * s.redComponent + 0.7152 * s.greenComponent + 0.0722 * s.blueComponent
        return lum > 0.45 ? SloopStyle.bg : SloopStyle.fg
    }

    func light(_ c: DDJControl) -> LightLevel { look(c).light }

    func isPressed(_ c: DDJControl) -> Bool { hwPressed.contains(c) || mouseHeld == c }

    /// a value's size and colour: bigger and brighter for a moment while its control moves
    private func valueStyle(_ c: DDJControl, _ lk: Look) -> (size: CGFloat, color: NSColor) {
        isFlashing(c) ? (capSize, lk.color ?? SloopStyle.fg) : (valSize, SloopStyle.fg2)
    }

    // MARK: - Shapes

    var radius: CGFloat { 2 }

    func box(_ g: CGContext, _ r: CGRect, fill: NSColor?, stroke: NSColor?, width: CGFloat = 1, dash: Bool = false) {
        if let fill {
            g.addPath(CGPath(roundedRect: r, cornerWidth: radius, cornerHeight: radius, transform: nil))
            g.setFillColor(fill.cgColor)
            g.fillPath()
        }
        if let stroke {
            g.saveGState()
            if dash { g.setLineDash(phase: 0, lengths: [2, 2]) }
            g.addPath(CGPath(roundedRect: r.insetBy(dx: width / 2, dy: width / 2), cornerWidth: radius, cornerHeight: radius, transform: nil))
            g.setStrokeColor(stroke.cgColor)
            g.setLineWidth(width)
            g.strokePath()
            g.restoreGState()
        }
    }

    func disc(_ g: CGContext, _ r: CGRect, fill: NSColor?, stroke: NSColor?, width: CGFloat = 1) {
        if let fill {
            g.setFillColor(fill.cgColor)
            g.fillEllipse(in: r)
        }
        if let stroke {
            g.setStrokeColor(stroke.cgColor)
            g.setLineWidth(width)
            g.strokeEllipse(in: r.insetBy(dx: width / 2, dy: width / 2))
        }
    }

    private func isRound(_ it: Item) -> Bool {
        switch it.kind {
        case .round, .knob, .browse, .jog: return true
        default: return false
        }
    }

    private func outline(_ g: CGContext, _ it: Item, _ color: NSColor, width: CGFloat, offset: CGFloat) {
        g.setStrokeColor(color.cgColor)
        g.setLineWidth(width)
        var r = it.rect
        if case .vfader = it.kind { r = r.insetBy(dx: 0, dy: -8) }
        if case .knob = it.kind { r = r.insetBy(dx: -4, dy: -4) }
        r = r.insetBy(dx: -offset, dy: -offset)
        if isRound(it) {
            g.strokeEllipse(in: r)
        } else {
            g.addPath(CGPath(roundedRect: r, cornerWidth: radius + 1, cornerHeight: radius + 1, transform: nil))
            g.strokePath()
        }
    }

    // MARK: - Draw

    /// the static parts (panels, printed labels, scales): drawn by the backdrop layer, again only when the size changes
    func drawBackdrop(_ dirtyRect: NSRect) {
        guard let g = NSGraphicsContext.current?.cgContext else { return }
        let deviceScale = max(1, hypot(g.ctm.a, g.ctm.b))
        if let img = staticImage(deviceScale) {
            g.saveGState()
            g.translateBy(x: 0, y: bounds.minY + bounds.maxY)
            g.scaleBy(x: 1, y: -1)
            g.draw(img, in: bounds)
            g.restoreGState()
        }
    }

    /// what moves (controls, faces, meters, the header): drawn by the transparent overlay above the backdrop, so a
    /// partial redraw never paints the background again (that was most of the app's CPU while music played)
    func drawDynamic(_ dirtyRect: NSRect) {
        guard let g = NSGraphicsContext.current?.cgContext else { return }
        let f = fit
        g.saveGState()
        g.translateBy(x: f.origin.x, y: f.origin.y)
        g.scaleBy(x: f.scale, y: f.scale)
        if needsToDraw(viewRect(Self.statusRect)) { drawHeader(g) }
        if needsToDraw(viewRect(Self.meterRect)) { drawMeters(g) }
        for it in items where needsToDraw(viewRect(area(it))) {
            let lk = look(it.control)
            drawItem(g, it, lk)
            drawTexts(g, it, lk)
        }
        g.restoreGState()
    }

    public override func viewDidChangeBackingProperties() {
        super.viewDidChangeBackingProperties()
        staticCache = nil
        backdropNeedsDisplay()
        needsDisplay = true
    }

    private func staticImage(_ scale: CGFloat) -> CGImage? {
        if let c = staticCache, c.size == bounds.size, c.scale == scale { return c.image }
        let w = Int((bounds.width * scale).rounded(.up)), h = Int((bounds.height * scale).rounded(.up))
        guard w > 0, h > 0, w < 16384, h < 16384,
              let space = CGColorSpace(name: CGColorSpace.sRGB),
              let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: 0, space: space,
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        ctx.translateBy(x: 0, y: CGFloat(h))
        ctx.scaleBy(x: scale, y: -scale)
        ctx.translateBy(x: -bounds.minX, y: -bounds.minY)
        let f = fit
        ctx.translateBy(x: f.origin.x, y: f.origin.y)
        ctx.scaleBy(x: f.scale, y: f.scale)
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(cgContext: ctx, flipped: true)
        drawStatic(ctx)
        NSGraphicsContext.restoreGraphicsState()
        guard let image = ctx.makeImage() else { return nil }
        staticCache = (bounds.size, scale, image)
        return image
    }

    // MARK: - Static parts

    private func drawStatic(_ g: CGContext) {
        let W = Self.designW, H = Self.designH
        let px = 1 / fit.scale   // one point
        // the body: three columns, open at the top, 1 pt borders on the sides, between the columns and at the bottom
        g.setFillColor(SloopStyle.s1.cgColor)
        g.fill(CGRect(x: 0, y: 0, width: W, height: H))
        g.setStrokeColor(SloopStyle.line.cgColor)
        g.setLineWidth(px)
        for x in [px / 2, Self.mixerX, Self.rightDeckX, W - px / 2] {
            g.move(to: CGPoint(x: x, y: 0)); g.addLine(to: CGPoint(x: x, y: H))
        }
        g.move(to: CGPoint(x: 0, y: H - px / 2)); g.addLine(to: CGPoint(x: W, y: H - px / 2))
        g.strokePath()

        for deck in Deck.allCases {
            let x0: CGFloat = deck == .left ? 0 : Self.rightDeckX
            drawText(deck == .left ? "1" : "2", size: 16, SloopStyle.fg2, x: x0 + 10, y: 13, align: .left)
            drawLabel("CUE/LOOP CALL", x: x0 + 243, y: 100)
        }

        // beat FX group
        let fx = Self.beatFXBox
        g.setStrokeColor(SloopStyle.line.cgColor)
        g.setLineWidth(px)
        g.stroke(fx.insetBy(dx: px / 2, dy: px / 2))
        let legend = "BEAT FX"
        let lw = CGFloat(legend.count) * charWidth(lblSize) + 8
        g.setFillColor(SloopStyle.s1.cgColor)
        g.fill(CGRect(x: fx.minX + 6, y: fx.minY - 6, width: lw, height: 12))
        drawLabel(legend, x: fx.minX + 10, y: fx.minY, align: .left, color: SloopStyle.fg2)
        drawLabel("BEAT", x: 526, y: 222)

        // fader scale between the channel faders
        if let i = itemIndex(for: .channelFader(.left)) {
            let r = items[i].rect
            for v in stride(from: 0, through: 10, by: 2) {
                drawLabel("\(v)", x: 620, y: r.minY + r.height * CGFloat(10 - v) / 10)
            }
        }

        for it in items {
            switch it.kind {
            case .knob:
                let c = CGPoint(x: it.rect.midX, y: it.rect.midY), rad = it.rect.width / 2
                g.setLineWidth(2)
                g.setStrokeColor(SloopStyle.s3.cgColor)
                g.addArc(center: c, radius: rad + 4, startAngle: knobAngle(0), endAngle: knobAngle(1), clockwise: false)
                g.strokePath()
            case .vfader:
                drawVFaderTrack(g, it)
            case .hfader:
                drawHFaderTrack(g, it)
            default:
                break
            }
        }
    }

    private func drawVFaderTrack(_ g: CGContext, _ it: Item) {
        let r = it.rect
        let isTempo: Bool = { if case .tempo = it.control { return true }; return false }()
        g.setStrokeColor(SloopStyle.s3.cgColor)
        g.setLineWidth(1)
        let n = isTempo ? 20 : 10
        for i in 0...n {
            let y = r.minY + r.height * CGFloat(i) / CGFloat(n)
            let long = i == 0 || i == n || (isTempo && i == n / 2) || (!isTempo && i % 2 == 0)
            let w: CGFloat = long ? 6 : 3
            g.move(to: CGPoint(x: r.minX, y: y)); g.addLine(to: CGPoint(x: r.minX + w, y: y))
            g.move(to: CGPoint(x: r.maxX - w, y: y)); g.addLine(to: CGPoint(x: r.maxX, y: y))
        }
        g.strokePath()
        if isTempo {
            g.setFillColor(SloopStyle.track[1].cgColor)
            g.fill(CGRect(x: r.maxX + 2, y: r.midY - 2, width: 4, height: 4))
        }
        box(g, CGRect(x: r.midX - 2, y: r.minY - 4, width: 4, height: r.height + 8), fill: SloopStyle.bg, stroke: SloopStyle.line)
    }

    private func drawHFaderTrack(_ g: CGContext, _ it: Item) {
        let r = it.rect
        g.setStrokeColor(SloopStyle.s3.cgColor)
        g.setLineWidth(1)
        for i in 0...8 {
            let x = r.minX + r.width * CGFloat(i) / 8
            let h: CGFloat = i == 4 || i == 0 || i == 8 ? 5 : 3
            g.move(to: CGPoint(x: x, y: r.minY)); g.addLine(to: CGPoint(x: x, y: r.minY + h))
            g.move(to: CGPoint(x: x, y: r.maxY - h)); g.addLine(to: CGPoint(x: x, y: r.maxY))
        }
        g.strokePath()
        box(g, CGRect(x: r.minX - 4, y: r.midY - 2, width: r.width + 8, height: 4), fill: SloopStyle.bg, stroke: SloopStyle.line)
    }

    // MARK: - Header (global state) or status line

    private func drawHeader(_ g: CGContext) {
        let r = Self.statusRect
        let dot = connected ? SloopStyle.track[1] : SloopStyle.track[3]
        guard let h = header else {
            let s = connected ? "DDJ-400 CONNECTED" : "NO DDJ-400: CLICK TO PLAY"
            let w = textWidth(s, lblSize)
            let x0 = r.midX - (w + 10) / 2
            g.setFillColor(dot.cgColor)
            g.fill(CGRect(x: x0, y: 11 - 3, width: 6, height: 6))
            drawText(s, size: lblSize, SloopStyle.fg2, x: x0 + 10, y: 11, align: .left)
            return
        }
        // the connection: a small dot in the corner
        g.setFillColor(dot.cgColor)
        g.fill(CGRect(x: r.minX + 6, y: 6, width: 5, height: 5))

        typealias Run = (text: String, color: NSColor, priority: Int)
        let fg = SloopStyle.fg, fg2 = SloopStyle.fg2
        // line 1: pattern, tempo, swing, transport, fill
        var line1: [Run] = []
        var pat = String(format: "P%02d", h.pattern + 1)
        if let q = h.queued, q != h.pattern { pat += String(format: " \u{2192} P%02d", q + 1) }
        line1.append((pat, fg, 0))
        let bpm = abs(h.bpm - h.bpm.rounded()) < 0.05 ? String(format: "%.0f", h.bpm) : String(format: "%.1f", h.bpm)
        line1.append((bpm + " BPM", fg, 0))
        line1.append(("SW \(h.swing)%", fg2, 3))
        if h.recording {
            line1.append(("\u{25CF} REC", SloopStyle.red, 1))
        } else {
            line1.append((h.playing ? "\u{25B6}" : "\u{25A0}", h.playing ? SloopStyle.track[1] : fg2, 1))
        }
        if h.fill { line1.append(("FILL", argb(TrigColors.track[2]), 2)) }
        // line 2: track and machine, page, step pages, performance FX, scene edit
        var line2: [Run] = []
        let tc = TrigColors.track[min(TrigColors.track.count - 1, max(0, h.track))]
        line2.append(("T\(h.track + 1) \(h.machine.uppercased())", argb(tc), 0))
        line2.append((h.page.name, argb(TrigColors.page[min(TrigColors.page.count - 1, h.page.rawValue)]), 0))
        if h.pages > 1 {
            line2.append((String((0..<h.pages).map { $0 == h.stepPage ? "\u{25CF}" : "\u{25CB}" }), fg2, 3))
        }
        if let p = h.perf { line2.append((p.uppercased(), fg, 2)) }
        if let s = h.sceneEdit {
            line2.append((s == 0 ? "SCENE A EDIT" : "SCENE B EDIT", argb(s == 0 ? TrigColors.sceneA : TrigColors.sceneB), 1))
        }
        let maxW = r.width - 24
        drawRuns(line1, size: capSize, centreX: r.midX, y: 14, maxWidth: maxW)
        drawRuns(line2, size: valSize, centreX: r.midX, y: 34, maxWidth: maxW)
    }

    /// a line of coloured runs, centred; the least important runs are dropped until it fits
    private func drawRuns(_ runs: [(text: String, color: NSColor, priority: Int)], size: CGFloat, centreX: CGFloat, y: CGFloat,
                          maxWidth: CGFloat) {
        var runs = runs
        let gap = charWidth(size) * 1.5
        func width() -> CGFloat { runs.reduce(0) { $0 + textWidth($1.text, size) } + gap * CGFloat(max(0, runs.count - 1)) }
        while width() > maxWidth, runs.count > 1, let i = runs.indices.max(by: { runs[$0].priority < runs[$1].priority }),
              runs[i].priority > 0 {
            runs.remove(at: i)
        }
        var x = centreX - width() / 2
        for run in runs {
            drawText(run.text, size: size, run.color, x: x, y: y, align: .left)
            x += textWidth(run.text, size) + gap
        }
    }

    // MARK: - Texts around a control: title, value, printed label, badge

    private func drawTexts(_ g: CGContext, _ it: Item, _ lk: Look) {
        let c = it.control
        let value = lk.value
        let (vSize, vColor) = valueStyle(c, lk)
        switch it.caption {
        case .at, .above:
            guard let r = captionRect(it) else { break }
            if let t = lk.title, !t.isEmpty {
                if it.valuePos == .inline, let value {
                    // title and value on one line
                    let tw = textWidth(t, capSize), gap = charWidth(capSize)
                    let room = r.width - tw - gap
                    let v = Self.truncate(value, maxChars(max(0, room), vSize))
                    let total = tw + gap + textWidth(v, vSize)
                    drawText(t, size: capSize, titleColor(it, lk), x: r.midX - total / 2, y: r.midY, align: .left)
                    drawText(v, size: vSize, vColor, x: r.midX - total / 2 + tw + gap, y: r.midY, align: .left)
                } else {
                    drawTitleWithBadge(g, t, lk, it, rect: r, align: .center)
                }
            }
            if it.valuePos == .underTitle, let value {
                drawLine(value, size: vSize, vColor, x: r.midX, y: r.maxY + lineHeight(vSize) / 2, width: r.width)
            }
        case .side(let left, _):
            guard let r = captionRect(it) else { break }
            let align: Align = left ? .right : .left
            let x = left ? r.maxX : r.minX
            let lh = lineHeight(capSize), vlh = lineHeight(vSize), llh = lineHeight(lblSize)
            var titleLines: [String] = []
            if let t = lk.title, !t.isEmpty {
                titleLines = Self.wrap(t, maxChars: maxChars(r.width - badgeWidth(lk), capSize), maxLines: value == nil ? 2 : 1)
            }
            let h = CGFloat(titleLines.count) * lh + (value != nil ? vlh : 0) + llh
            var y = it.rect.midY - h / 2
            for (i, line) in titleLines.enumerated() {
                let lr = CGRect(x: r.minX, y: y, width: r.width, height: lh)
                if i == 0 {
                    drawTitleWithBadge(g, line, lk, it, rect: lr, align: align)
                } else {
                    drawText(line, size: capSize, titleColor(it, lk), x: x, y: y + lh / 2, align: align)
                }
                y += lh
            }
            if let value {
                drawLine(value, size: vSize, vColor, x: x, y: y + vlh / 2, align: align, width: r.width)
                y += vlh
            }
            drawLabel(it.label, x: x, y: y + llh / 2, align: align, maxWidth: r.width)
        case .inside:
            break
        }
        // under the control: its value (knobs, BROWSE) or its printed label
        let below = it.rect.maxY + (isRound(it) ? 6 : 3) + (it.kind.isKnob ? 4 : 0) + (it.kind.isVFader ? 9 : 0)
        if it.valuePos == .below, let value {
            drawLine(value, size: vSize, vColor, x: it.rect.midX, y: below + lineHeight(vSize) / 2, width: 110)
        } else if it.labelPos == .below {
            drawLabel(it.label, x: it.rect.midX, y: below + lineHeight(lblSize) / 2, maxWidth: 70)
        }
        if it.labelPos == .left {
            drawLabel(it.label, x: it.rect.minX - 10, y: it.rect.midY, align: .right)
        }
        // BROWSE: what pushing it does
        if case .browse = it.kind {
            let push = faces[.browsePress]?.title ?? caption(.browsePress)
            if let push, !push.isEmpty {
                let y = below + max(lineHeight(vSize), lineHeight(lblSize)) + 2 + lineHeight(valSize) / 2
                drawLine("PUSH: " + push.uppercased(), size: valSize, SloopStyle.fg2, x: it.rect.midX, y: y, width: 130)
            }
        }
    }

    private func badgeWidth(_ lk: Look) -> CGFloat {
        guard let b = lk.badge, !b.isEmpty else { return 0 }
        return textWidth(b, lblSize) + 8 + 4
    }

    /// a title on one line; a knob's badge ("LOCK", "A", "B") as a small tag beside it, on the side away from the knob
    private func drawTitleWithBadge(_ g: CGContext, _ t: String, _ lk: Look, _ it: Item, rect r: CGRect, align: Align) {
        let bw = badgeWidth(lk)
        let line = Self.truncate(t, maxChars(r.width - bw, capSize))
        let color = titleColor(it, lk)
        let tw = textWidth(line, capSize)
        var tx: CGFloat
        switch align {
        case .left: tx = r.minX
        case .right: tx = r.maxX - tw
        case .center: tx = r.midX - (tw + bw) / 2 + (bw > 0 ? 0 : 0)
        }
        if bw > 0, let b = lk.badge {
            // tag: after the title (left-aligned / centred), before it (right-aligned)
            let tagW = bw - 4, tagH = lineHeight(lblSize) + 2
            let tagX: CGFloat
            switch align {
            case .right: tagX = tx - bw
            default: tagX = tx + tw + 4
            }
            let tag = CGRect(x: tagX, y: r.midY - tagH / 2, width: tagW, height: tagH)
            let fill = lk.color ?? SloopStyle.fg
            box(g, tag, fill: fill, stroke: nil)
            drawText(b, size: lblSize, ink(on: fill), x: tag.midX, y: tag.midY)
        }
        if align == .center, bw > 0 { tx = r.midX - (tw + bw) / 2 }
        drawText(line, size: capSize, color, x: tx, y: r.midY, align: .left)
    }

    // MARK: - Controls

    private func drawItem(_ g: CGContext, _ it: Item, _ lk: Look) {
        let held = isHeld(it)
        switch it.kind {
        case .button, .shiftButton:
            var lvl = lk.light
            if case .shift(let d) = it.control, shiftLatched.contains(d) { lvl = .on }
            var hue = lk.color ?? SloopStyle.fg
            if lk.color == nil, case .headphoneCue = it.control { hue = SloopStyle.track[3] }
            drawRectButton(g, it, lk, level: lvl, hue: hue, held: held)
        case .round:
            drawRoundButton(g, it, lk, held: held)
        case .mode(let base, let alt):
            drawModeButton(g, it, base: base, alt: alt, held: held)
        case .pad:
            drawPad(g, it, lk, held: held)
        case .knob:
            drawKnob(g, it, lk)
        case .browse:
            drawBrowse(g, it)
        case .vfader:
            drawVFaderCap(g, it, lk)
        case .hfader:
            drawHFaderCap(g, it, lk)
        case .jog:
            drawJog(g, it, lk)
        case .segment:
            drawSegment(g, it, lk)
        }
        if held { outline(g, it, heldColor, width: 2, offset: 4) }
        if highlighted.contains(it.control) {
            outline(g, it, SloopStyle.fg, width: 2, offset: held ? 7 : 4)
        } else if hovered == it.control, !held {
            outline(g, it, SloopStyle.fg2.withAlphaComponent(0.6), width: 1, offset: 4)
        }
    }

    private func pressRing(_ g: CGContext, _ r: CGRect, round: Bool) {
        g.setStrokeColor(SloopStyle.fg.cgColor)
        g.setLineWidth(1.5)
        if round {
            g.strokeEllipse(in: r.insetBy(dx: -2, dy: -2))
        } else {
            g.addPath(CGPath(roundedRect: r.insetBy(dx: -2, dy: -2), cornerWidth: radius + 1, cornerHeight: radius + 1, transform: nil))
            g.strokePath()
        }
    }

    /// fill for a light level: lit full, dim the colour at a quarter, off the plain control
    private func lightFill(_ g: CGContext, _ r: CGRect, _ level: LightLevel, _ hue: NSColor, pressed: Bool) {
        switch level {
        case .on: box(g, r, fill: hue, stroke: nil)
        case .dim:
            box(g, r, fill: SloopStyle.s2, stroke: nil)
            box(g, r, fill: hue.withAlphaComponent(0.25), stroke: hue.withAlphaComponent(0.5))
        case .off: box(g, r, fill: pressed ? SloopStyle.s3 : SloopStyle.s2, stroke: SloopStyle.s3)
        }
    }

    private func drawRectButton(_ g: CGContext, _ it: Item, _ lk: Look, level: LightLevel, hue: NSColor, held: Bool) {
        let r = it.rect
        let pressed = isPressed(it.control)
        if held {
            box(g, r, fill: heldColor, stroke: nil)
            if pressed { pressRing(g, r, round: false) }
            drawText("HELD", size: lblSize, ink(on: heldColor), x: r.midX, y: r.midY, maxWidth: r.width - 4, shrink: true)
            return
        }
        lightFill(g, r, level, hue, pressed: pressed)
        if pressed { pressRing(g, r, round: false) }
        let lc: NSColor = level == .on ? ink(on: hue) : SloopStyle.fg2
        if it.valuePos == .inside, let v = lk.value {
            let (vs, vc) = valueStyle(it.control, lk)
            drawLine(v, size: vs, level == .on ? ink(on: hue) : (isFlashing(it.control) ? vc : SloopStyle.fg), x: r.midX, y: r.midY, width: r.width - 4)
        } else {
            drawText(it.label, size: lblSize, lc, x: r.midX, y: r.midY, maxWidth: r.width - 4, shrink: true)
        }
    }

    private func drawRoundButton(_ g: CGContext, _ it: Item, _ lk: Look, held: Bool) {
        let r = it.rect
        let lvl = lk.light
        let pressed = isPressed(it.control)
        let big = r.width >= 40
        let ringColor: NSColor
        if let c = lk.color {
            ringColor = c
        } else {
            switch it.control {
            case .play: ringColor = SloopStyle.track[1]
            case .cue: ringColor = SloopStyle.red
            default: ringColor = it.control == .fxOnOff ? SloopStyle.fg : trackColor
            }
        }
        disc(g, r, fill: pressed ? SloopStyle.s3 : SloopStyle.s2, stroke: SloopStyle.s3)
        if held {
            disc(g, r.insetBy(dx: 1, dy: 1), fill: heldColor, stroke: nil)
        } else if big {
            let ring = r.insetBy(dx: 3, dy: 3)
            g.setLineWidth(3)
            switch lvl {
            case .on: g.setStrokeColor(ringColor.cgColor)
            case .dim: g.setStrokeColor(ringColor.withAlphaComponent(0.35).cgColor)
            case .off: g.setStrokeColor(SloopStyle.line.cgColor)
            }
            g.strokeEllipse(in: ring)
        } else {
            switch lvl {
            case .on: disc(g, r.insetBy(dx: 1, dy: 1), fill: ringColor, stroke: nil)
            case .dim: disc(g, r.insetBy(dx: 1, dy: 1), fill: ringColor.withAlphaComponent(0.25), stroke: ringColor.withAlphaComponent(0.5))
            case .off: break
            }
        }
        if pressed { pressRing(g, r, round: true) }
        let onFill = held ? heldColor : ringColor
        let inkC = (held || (!big && lvl == .on)) ? ink(on: onFill) : (lvl == .on ? SloopStyle.fg : SloopStyle.fg2)
        if big {
            // the value (or the printed label) big in the middle, the printed label (or HELD) small under it
            if let v = lk.value {
                let (vs, _) = valueStyle(it.control, lk)
                drawLine(v, size: max(vs, 16), held ? inkC : SloopStyle.fg, x: r.midX, y: r.midY - 5, width: r.width - 10)
                drawText(held ? "HELD" : it.label, size: lblSize, inkC, x: r.midX, y: r.midY + 12, maxWidth: r.width - 16, shrink: true)
            } else if held {
                drawText(it.label, size: 16, inkC, x: r.midX, y: r.midY - 5, maxWidth: r.width - 10, shrink: true)
                drawText("HELD", size: lblSize, inkC, x: r.midX, y: r.midY + 11, maxWidth: r.width - 12, shrink: true)
            } else {
                drawText(it.label, size: 16, inkC, x: r.midX, y: r.midY, maxWidth: r.width - 10, shrink: true)
            }
        } else if held {
            drawText("HELD", size: lblSize, inkC, x: r.midX, y: r.midY, maxWidth: r.width - 4, shrink: true)
        } else if it.valuePos == .inside, let v = lk.value {
            drawLine(v, size: lblSize, inkC, x: r.midX, y: r.midY, width: r.width - 4)
        } else if it.labelPos == .inside {
            drawText(it.label, size: lblSize, inkC, x: r.midX, y: r.midY, maxWidth: r.width - 10, shrink: true)
        }
    }

    private func drawModeButton(_ g: CGContext, _ it: Item, base: PadMode, alt: PadMode, held: Bool) {
        guard case .padModeButton(let deck, _) = it.control else { return }
        let m = mode(deck)
        let baseLook = look(.padModeButton(deck, base)), altLook = look(.padModeButton(deck, alt))
        let altActive = m == alt || altLook.light == .on
        let active = m == base || altActive || baseLook.light == .on
        let lvl: LightLevel = active ? .on : max(baseLook.light, altLook.light)
        let pressed = isPressed(.padModeButton(deck, base)) || isPressed(.padModeButton(deck, alt))
        let r = it.rect
        let hue = baseLook.color ?? SloopStyle.fg
        if held {
            box(g, r, fill: heldColor, stroke: nil)
        } else {
            lightFill(g, r, lvl, hue, pressed: pressed)
        }
        if pressed { pressRing(g, r, round: false) }
        let mainColor: NSColor = (lvl == .on || held) ? ink(on: held ? heldColor : hue).withAlphaComponent(altActive ? 0.45 : 1) : SloopStyle.fg2
        drawText(base.ddjLabel, size: lblSize, mainColor, x: r.midX, y: r.midY, maxWidth: r.width - 4, shrink: true)
        // the shifted mode printed under the button, red when it is the one selected
        drawLabel(alt.ddjLabel, x: r.midX, y: r.maxY + 3 + lineHeight(lblSize) / 2,
                  color: altActive ? SloopStyle.red : labelColor, maxWidth: r.width)
    }

    private func drawSegment(_ g: CGContext, _ it: Item, _ lk: Look) {
        let r = it.rect
        let pressed = isPressed(it.control)
        let hue = lk.color ?? SloopStyle.fg
        lightFill(g, r, lk.light, hue, pressed: pressed)
        if pressed { pressRing(g, r, round: false) }
        let title = (lk.title?.isEmpty == false ? lk.title : nil) ?? it.label
        let color: NSColor = lk.light == .on ? ink(on: hue) : (lk.light == .dim ? SloopStyle.fg : SloopStyle.fg2)
        drawLine(title, size: lblSize, color, x: r.midX, y: r.midY, width: r.width - 6)
    }

    private func drawPad(_ g: CGContext, _ it: Item, _ lk: Look, held: Bool) {
        let r = it.rect
        let pressed = isPressed(it.control)
        if lk.empty && !held {   // nothing here (past the track's length)
            box(g, r, fill: SloopStyle.s1, stroke: SloopStyle.line)
            if pressed { pressRing(g, r, round: false) }
            return
        }
        let lvl = held ? .on : lk.light
        let title = lk.title
        let isFX = !lk.hasFace && (title?.hasPrefix("FX") ?? false)
        let hue = held ? heldColor : (lk.color ?? (isFX ? SloopStyle.red : trackColor))
        lightFill(g, r, lvl, hue, pressed: pressed)
        if pressed { pressRing(g, r, round: false) }
        // the playhead or a held step: a crisp outline inside the pad
        if lk.outlined {
            g.setStrokeColor((lvl == .on ? ink(on: hue) : SloopStyle.fg).cgColor)
            g.setLineWidth(2)
            g.addPath(CGPath(roundedRect: r.insetBy(dx: 4, dy: 4), cornerWidth: radius, cornerHeight: radius, transform: nil))
            g.strokePath()
        }
        let onInk = ink(on: hue)
        // badge in the top-right corner
        if let b = lk.badge, !b.isEmpty {
            let bc: NSColor = lvl == .on ? onInk : SloopStyle.fg
            drawLine(b, size: lblSize, bc, x: r.maxX - 7, y: r.minY + 6 + lineHeight(lblSize) / 2, align: .right, width: r.width - 12)
        }
        guard let title, !title.isEmpty else { return }
        let color: NSColor
        switch lvl {
        case .on: color = isFX ? SloopStyle.fg2 : onInk
        case .dim: color = SloopStyle.fg
        case .off: color = lk.hasFace ? SloopStyle.fg2 : (knobAccents[it.control] ?? (isFX ? SloopStyle.red : SloopStyle.fg))
        }
        let lh = lineHeight(capSize)
        let lines = Self.wrap(title, maxChars: maxChars(r.width - 8, capSize), maxLines: lk.value == nil ? 2 : 1)
        let vlh = lk.value == nil ? 0 : lineHeight(valSize)
        var y = r.midY - (CGFloat(lines.count) * lh + vlh) / 2 + lh / 2 + 3
        for line in lines {
            drawText(line, size: capSize, color, x: r.midX, y: y)
            y += lh
        }
        if let v = lk.value {
            let vc: NSColor = lvl == .on ? onInk.withAlphaComponent(0.8) : SloopStyle.fg2
            drawLine(v, size: valSize, vc, x: r.midX, y: y - lh / 2 + vlh / 2 + 1, width: r.width - 6)
        }
    }

    func knobAngle(_ v: Double) -> CGFloat { -.pi / 2 + CGFloat(v - 0.5) * 1.5 * .pi }

    private func drawKnob(_ g: CGContext, _ it: Item, _ lk: Look) {
        let r = it.rect, c = CGPoint(x: r.midX, y: r.midY), rad = r.width / 2
        let pressed = isPressed(it.control)
        let accent = lk.color
        let take = takeovers[it.control]
        let p = position(it.control)
        let ap = knobAngle(p)
        if let arc = lk.arc {
            // the parameter's value as a ring from the minimum, in the knob row's colour
            let a = knobAngle(Self.clamp(arc))
            if arc > 0.001 {
                g.setLineWidth(3)
                g.setStrokeColor((accent ?? SloopStyle.fg2).cgColor)
                g.addArc(center: c, radius: rad + 4, startAngle: knobAngle(0), endAngle: a, clockwise: false)
                g.strokePath()
            }
        } else {
            let amid = knobAngle(0.5)
            if abs(ap - amid) > 0.001 {
                g.setLineWidth(2)
                g.setStrokeColor((accent ?? SloopStyle.fg2).cgColor)
                g.addArc(center: c, radius: rad + 4, startAngle: min(amid, ap), endAngle: max(amid, ap), clockwise: false)
                g.strokePath()
            }
        }
        disc(g, r, fill: pressed ? SloopStyle.s3 : SloopStyle.s2, stroke: pressed ? SloopStyle.fg2 : (accent ?? SloopStyle.s3))
        let inactive = take.map { !$0.active } ?? false
        g.setStrokeColor((inactive ? SloopStyle.dim : (accent ?? SloopStyle.fg)).cgColor)
        g.setLineWidth(2)
        g.move(to: CGPoint(x: c.x + cos(ap) * rad * 0.25, y: c.y + sin(ap) * rad * 0.25))
        g.addLine(to: CGPoint(x: c.x + cos(ap) * (rad - 2), y: c.y + sin(ap) * (rad - 2)))
        g.strokePath()
        if let take {
            let a = knobAngle(Self.clamp(take.target))
            g.setFillColor((accent ?? trackColor).cgColor)
            g.fillEllipse(in: CGRect(x: c.x + cos(a) * (rad + 4) - 2.5, y: c.y + sin(a) * (rad + 4) - 2.5, width: 5, height: 5))
        }
    }

    private func drawBrowse(_ g: CGContext, _ it: Item) {
        let r = it.rect, c = CGPoint(x: r.midX, y: r.midY), rad = r.width / 2
        let pressed = isPressed(.browse) || isPressed(.browsePress)
        disc(g, r, fill: pressed ? SloopStyle.s3 : SloopStyle.s2, stroke: SloopStyle.s3)
        g.setStrokeColor(SloopStyle.line.cgColor)
        g.setLineWidth(1)
        for i in 0..<24 {
            let a = browseAngle + CGFloat(i) * .pi / 12
            g.move(to: CGPoint(x: c.x + cos(a) * (rad - 5), y: c.y + sin(a) * (rad - 5)))
            g.addLine(to: CGPoint(x: c.x + cos(a) * (rad - 1.5), y: c.y + sin(a) * (rad - 1.5)))
        }
        g.strokePath()
        let a = browseAngle - .pi / 2
        g.setFillColor(SloopStyle.fg.cgColor)
        g.fill(CGRect(x: c.x + cos(a) * rad * 0.55 - 2, y: c.y + sin(a) * rad * 0.55 - 2, width: 4, height: 4))
        if pressed { pressRing(g, r, round: true) }
    }

    private func takeoverColor(_ c: DDJControl, _ lk: Look) -> NSColor { lk.color ?? trackColor }

    /// How far down its slot a vertical fader's value sits (0 top, 1 bottom). The channel faders send 1 at the top; the
    /// tempo sliders send 1 at the bottom (measured on a DDJ-400: towards you is 16383, the "+" end)
    static func downFraction(_ c: DDJControl, _ v: Double) -> Double {
        if case .tempo = c { return v }
        return 1 - v
    }

    private func drawVFaderCap(_ g: CGContext, _ it: Item, _ lk: Look) {
        let r = it.rect
        let isTempo: Bool = { if case .tempo = it.control { return true }; return false }()
        let take = takeovers[it.control]
        if let take {   // where the value really is: a mark either side of the slot
            let y = r.minY + CGFloat(Self.downFraction(it.control, Self.clamp(take.target))) * r.height
            g.setFillColor(takeoverColor(it.control, lk).cgColor)
            for (x, dir) in [(r.minX - 1, CGFloat(1)), (r.maxX + 1, CGFloat(-1))] {
                g.move(to: CGPoint(x: x, y: y - 5)); g.addLine(to: CGPoint(x: x + 7 * dir, y: y)); g.addLine(to: CGPoint(x: x, y: y + 5))
                g.closePath()
            }
            g.fillPath()
        }
        let y = r.minY + CGFloat(Self.downFraction(it.control, position(it.control))) * r.height
        let capH: CGFloat = isTempo ? 16 : 14
        drawCap(g, CGRect(x: r.minX + 3, y: y - capH / 2, width: r.width - 6, height: capH), pressed: isPressed(it.control),
                vertical: true, inactive: take.map { !$0.active } ?? false)
    }

    private func drawHFaderCap(_ g: CGContext, _ it: Item, _ lk: Look) {
        let r = it.rect
        let take = takeovers[it.control]
        if let take {
            let x = r.minX + CGFloat(Self.clamp(take.target)) * r.width
            g.setFillColor(takeoverColor(it.control, lk).cgColor)
            for (y, dir) in [(r.minY - 1, CGFloat(1)), (r.maxY + 1, CGFloat(-1))] {
                g.move(to: CGPoint(x: x - 5, y: y)); g.addLine(to: CGPoint(x: x, y: y + 7 * dir)); g.addLine(to: CGPoint(x: x + 5, y: y))
                g.closePath()
            }
            g.fillPath()
        }
        let x = r.minX + CGFloat(position(it.control)) * r.width
        drawCap(g, CGRect(x: x - 7, y: r.minY + 2, width: 14, height: r.height - 4), pressed: isPressed(it.control),
                vertical: false, inactive: take.map { !$0.active } ?? false)
    }

    private func drawCap(_ g: CGContext, _ cap: CGRect, pressed: Bool, vertical: Bool, inactive: Bool) {
        if inactive {   // hollow: not in control until moved past the mark
            box(g, cap, fill: SloopStyle.s1, stroke: SloopStyle.fg2.withAlphaComponent(0.7), width: 1, dash: true)
        } else {
            box(g, cap, fill: pressed ? SloopStyle.s3 : SloopStyle.s2, stroke: pressed ? SloopStyle.fg2 : SloopStyle.s3)
        }
        g.setFillColor((inactive ? SloopStyle.dim : SloopStyle.fg).cgColor)
        if vertical {
            g.fill(CGRect(x: cap.minX + 3, y: cap.midY - 1, width: cap.width - 6, height: 2))
        } else {
            g.fill(CGRect(x: cap.midX - 1, y: cap.minY + 3, width: 2, height: cap.height - 6))
        }
    }

    // MARK: - Jog: the deck's display

    private func drawJog(_ g: CGContext, _ it: Item, _ lk: Look) {
        guard case .jog(let deck) = it.control else { return }
        let r = it.rect, c = CGPoint(x: r.midX, y: r.midY), R = r.width / 2
        let touched = jogTouched == deck || hwPressed.contains(.jogTouch(deck)) || isPressed(.jog(deck)) || lk.light == .on
        let angle = jogAngle[deck.rawValue]
        let accent = lk.color
        let tracks = deckTracks[deck] ?? []

        // outer ring with grip notches that turn with the platter
        disc(g, r, fill: SloopStyle.s2, stroke: SloopStyle.s3)
        g.setStrokeColor(SloopStyle.line.cgColor)
        g.setLineWidth(1.5)
        for i in 0..<72 {
            let a = angle + CGFloat(i) * .pi / 36
            g.move(to: CGPoint(x: c.x + cos(a) * R * 0.88, y: c.y + sin(a) * R * 0.88))
            g.addLine(to: CGPoint(x: c.x + cos(a) * (R - 3), y: c.y + sin(a) * (R - 3)))
        }
        g.strokePath()
        // platter
        let pr = R * 0.85
        let pRect = CGRect(x: c.x - pr, y: c.y - pr, width: 2 * pr, height: 2 * pr)
        disc(g, pRect, fill: SloopStyle.s1, stroke: SloopStyle.s3)
        let cr = R * 0.42

        if tracks.isEmpty {
            g.setStrokeColor(SloopStyle.line.cgColor)
            g.setLineWidth(1)
            g.strokeEllipse(in: pRect.insetBy(dx: pr * 0.28, dy: pr * 0.28))
            // marker across the platter
            g.setStrokeColor((accent ?? SloopStyle.fg).cgColor)
            g.setLineCap(.round)
            g.setLineWidth(3)
            g.move(to: CGPoint(x: c.x + cos(angle) * pr * 0.5, y: c.y + sin(angle) * pr * 0.5))
            g.addLine(to: CGPoint(x: c.x + cos(angle) * pr * 0.95, y: c.y + sin(angle) * pr * 0.95))
            g.strokePath()
            g.setLineCap(.butt)
        } else {
            drawDeckTracks(g, tracks, centre: c, inner: cr, outer: pr)
            // the marker on the grip ring: the dial (it points into the active track's quadrant), else the platter
            let ma = jogDials[deck].map { Self.ringCentreAngle(0, of: tracks.count) + CGFloat($0) * 2 * .pi / CGFloat(tracks.count) } ?? angle
            g.setStrokeColor((accent ?? SloopStyle.fg).cgColor)
            g.setLineWidth(4)
            g.move(to: CGPoint(x: c.x + cos(ma) * R * 0.87, y: c.y + sin(ma) * R * 0.87))
            g.addLine(to: CGPoint(x: c.x + cos(ma) * (R - 2), y: c.y + sin(ma) * (R - 2)))
            g.strokePath()
        }

        // touched: the scratch ring, its dashes turning with the platter
        if touched {
            let sc = accent ?? SloopStyle.fg
            g.saveGState()
            g.setStrokeColor(sc.cgColor)
            g.setLineWidth(3)
            g.setLineDash(phase: -angle * (pr + 3), lengths: [10, 6])
            g.strokeEllipse(in: pRect.insetBy(dx: -3, dy: -3))
            g.restoreGState()
        }

        // centre: the deck's title and value (the active track's machine)
        disc(g, CGRect(x: c.x - cr, y: c.y - cr, width: 2 * cr, height: 2 * cr), fill: SloopStyle.s2, stroke: SloopStyle.s3)
        let llh = lineHeight(lblSize), lh = lineHeight(capSize), vlh = lineHeight(valSize)
        let foot = touched ? "SCRATCH" : "JOG"
        let footColor = touched ? (accent ?? SloopStyle.fg) : labelColor
        if let title = lk.title, !title.isEmpty {
            let lines = Self.wrap(title, maxChars: maxChars(cr * 1.75, capSize), maxLines: lk.value == nil ? 2 : 1)
            let h = CGFloat(lines.count) * lh + (lk.value != nil ? vlh : 0) + llh
            var y = c.y - h / 2 + lh / 2
            let tc: NSColor = lk.hasFace ? SloopStyle.fg : titleColor(it, lk)
            for line in lines {
                drawText(line, size: capSize, tc, x: c.x, y: y)
                y += lh
            }
            y -= lh / 2
            if let v = lk.value {
                drawLine(v, size: valSize, accent ?? SloopStyle.fg2, x: c.x, y: y + vlh / 2, width: cr * 1.8)
                y += vlh
            }
            drawLabel(foot, x: c.x, y: y + llh / 2 + 1, color: footColor)
        } else {
            drawLabel(foot, x: c.x, y: c.y, color: footColor)
        }
    }

    /// the deck's tracks as arc segments between the centre and the platter's edge: a coloured band (bright when
    /// active, hollow when muted), the name, and a level meter along the inner edge
    /// the centre of track i's part of a jog's ring of n: track 1 at the top left, then clockwise
    static func ringCentreAngle(_ i: Int, of n: Int) -> CGFloat {
        let span = 2 * CGFloat.pi / CGFloat(max(1, n))
        return -CGFloat.pi * 3 / 4 + CGFloat(i) * span + (n == 4 ? 0 : span / 2 - .pi / 4)
    }

    private func drawDeckTracks(_ g: CGContext, _ tracks: [DeckTrackFace], centre c: CGPoint, inner: CGFloat, outer: CGFloat) {
        let n = tracks.count
        let span = 2 * CGFloat.pi / CGFloat(n)
        let gap: CGFloat = 0.09
        let bandR = outer - 9, meterR = inner + 9, nameR = (bandR + meterR) / 2 + 1
        for (i, t) in tracks.enumerated() {
            let mid = Self.ringCentreAngle(i, of: n)
            let a0 = mid - span / 2 + gap, a1 = mid + span / 2 - gap
            g.setLineCap(.butt)
            if t.muted {
                g.setLineWidth(1.5)
                g.setStrokeColor(t.color.withAlphaComponent(0.6).cgColor)
                g.addArc(center: c, radius: bandR + 5, startAngle: a0, endAngle: a1, clockwise: false)
                g.addArc(center: c, radius: bandR - 5, startAngle: a1, endAngle: a0, clockwise: true)
                g.closePath()
                g.strokePath()
            } else {
                g.setLineWidth(t.active ? 12 : 8)
                g.setStrokeColor(t.color.withAlphaComponent(t.active ? 1 : 0.5).cgColor)
                g.addArc(center: c, radius: bandR, startAngle: a0, endAngle: a1, clockwise: false)
                g.strokePath()
            }
            // level meter along the inner edge
            g.setLineWidth(6)
            g.setStrokeColor(SloopStyle.s2.cgColor)
            g.addArc(center: c, radius: meterR, startAngle: a0, endAngle: a1, clockwise: false)
            g.strokePath()
            let lv = Self.clamp(t.level)
            if !t.muted, lv > 0.005 {
                g.setStrokeColor(t.color.withAlphaComponent(t.active ? 1 : 0.75).cgColor)
                g.addArc(center: c, radius: meterR, startAngle: a0, endAngle: a0 + (a1 - a0) * CGFloat(lv), clockwise: false)
                g.strokePath()
            }
            // the name
            let p = CGPoint(x: c.x + cos(mid) * nameR, y: c.y + sin(mid) * nameR)
            let nc: NSColor = t.muted ? SloopStyle.dim : (t.active ? SloopStyle.fg : SloopStyle.fg2)
            let name = t.muted ? Self.truncate(t.name.uppercased(), 6) : t.name.uppercased()
            drawLine(name, size: valSize, nc, x: p.x, y: p.y - (t.muted ? 4 : 0), width: 56)
            if t.muted { drawLabel("MUTE", x: p.x, y: p.y + 7, color: SloopStyle.dim) }
        }
    }

    private func drawMeters(_ g: CGContext) {
        let r = Self.meterRect
        let top = r.minY + 4, h = r.height - 8
        let segs = Self.meterSegmentCount, gap: CGFloat = 3
        let segH = (h - CGFloat(segs - 1) * gap) / CGFloat(segs)
        let levels = [meters.0, meters.1]
        for (k, x) in [CGFloat(612), CGFloat(621)].enumerated() {
            let lit = Self.segments(levels[k])
            for i in 0..<segs {
                let y = top + h - CGFloat(i + 1) * segH - CGFloat(i) * gap
                let color = i >= segs - 2 ? SloopStyle.red : (i >= segs - 5 ? SloopStyle.track[2] : SloopStyle.track[1])
                g.setFillColor(i < lit ? color.cgColor : SloopStyle.s2.cgColor)
                g.fill(CGRect(x: x, y: y, width: 7, height: segH))
            }
        }
    }
}

extension DDJ400View.Item.Kind {
    var isKnob: Bool { if case .knob = self { return true }; return false }
    var isVFader: Bool { if case .vfader = self { return true }; return false }
}
