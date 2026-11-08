// SPDX-License-Identifier: GPL-3.0-only
// The FM-1 screen widened to the DDJ-400's width: one SLOOP display, 240 screen pixels tall. SLOOP's own 240 x 240
// screen (the firmware's) sits at the left; the rest of the strip is drawn on the same pixel grid in the same style
// (Terminus at its native 16 px, black, the four track colours): status, the four tracks' levels, and the FM-1
// keyboard with its LEDs (what the pads stand for). Every screen pixel is a whole number of device pixels.
import AppKit
import DDJMapping

@MainActor
final class WideScreenView: NSView {
    var image: CGImage? { didSet { setNeedsDisplay(imageRect) } }   // (only SLOOP's own 240 x 240 part)

    /// where SLOOP's screen is drawn, in view coordinates
    private var imageRect: CGRect {
        let s = Self.pixelSize(forHeight: bounds.height, backing: window?.backingScaleFactor ?? 2)
        let cols = (bounds.width / s).rounded(.down)
        let x0 = ((bounds.width - cols * s) / 2).rounded()
        return CGRect(x: x0, y: 0, width: 240 * s, height: 240 * s).insetBy(dx: -1, dy: -1)
    }
    var state = GrooveState() { didSet { if state != oldValue { needsDisplay = true } } }
    var padMode: PadMode = .hotCue { didSet { if padMode != oldValue { needsDisplay = true } } }
    var padHelp = "" { didSet { if padHelp != oldValue { needsDisplay = true } } }
    var connected = false { didSet { if connected != oldValue { needsDisplay = true } } }
    var notes: [String] = [] { didSet { if notes != oldValue { needsDisplay = true } } }

    static let rows: CGFloat = 240                      // screen pixels, as the FM-1

    override var isFlipped: Bool { true }

    /// points per screen pixel: the largest whole number of device pixels that fits the height
    static func pixelSize(forHeight h: CGFloat, backing: CGFloat) -> CGFloat {
        let devicePx = max(1, ((h * backing) / rows).rounded(.down))
        return devicePx / backing
    }

    override func draw(_ dirtyRect: NSRect) {
        guard let ctx = NSGraphicsContext.current?.cgContext else { return }
        let backing = window?.backingScaleFactor ?? 2
        let s = Self.pixelSize(forHeight: bounds.height, backing: backing)
        let cols = (bounds.width / s).rounded(.down)
        let screen = CGRect(x: 0, y: 0, width: cols * s, height: Self.rows * s)

        ctx.setFillColor(SloopStyle.bg.cgColor)
        ctx.fill(bounds)
        ctx.setStrokeColor(SloopStyle.line.cgColor)        // the screen's edge
        ctx.setLineWidth(1)
        ctx.stroke(bounds.insetBy(dx: 0.5, dy: 0.5))
        ctx.saveGState()
        ctx.translateBy(x: ((bounds.width - screen.width) / 2).rounded(), y: 0)
        ctx.scaleBy(x: s, y: s)                         // from here: screen pixels
        ctx.interpolationQuality = .none

        // SLOOP's own screen, 240 x 240 at the left
        if let image {
            ctx.saveGState()
            ctx.translateBy(x: 0, y: 240)
            ctx.scaleBy(x: 1, y: -1)
            ctx.draw(image, in: CGRect(x: 0, y: 0, width: 240, height: 240))
            ctx.restoreGState()
        }
        // a divider as SLOOP draws its rules
        fill(ctx, CGRect(x: 244, y: 0, width: 1, height: 240), SloopStyle.line)
        drawPanel(ctx, x: 252, width: cols - 252)
        ctx.restoreGState()
    }

    // ---- the panel right of the screen, in screen pixels (y down)

    private func drawPanel(_ ctx: CGContext, x: CGFloat, width w: CGFloat) {
        guard w > 120 else { return }
        // header: SLOOP in the track colours, the controller, the transport (as SLOOP's header: big BPM)
        var cx = x
        for (i, ch) in "SLOOP".enumerated() {
            cx += text(String(ch), at: CGPoint(x: cx, y: 2), size: 32, SloopStyle.track[min(i, 3)])
        }
        cx += text(" × DDJ-400", at: CGPoint(x: cx, y: 2), size: 32, SloopStyle.fg)
        let link = connected ? ("CONNECTED", SloopStyle.track[1]) : ("NOT FOUND: CLICK THE PADS", SloopStyle.dim)
        rightText(link.0, right: x + w - 2, y: 4, size: 16, link.1)
        let tr: (String, NSColor) = state.recording ? ("● REC", SloopStyle.red)
            : state.playing ? ("▶ PLAY", SloopStyle.track[1]) : ("■ STOP", SloopStyle.dim)
        rightText(tr.0, right: x + w - 2, y: 20, size: 16, tr.1)

        fill(ctx, CGRect(x: x, y: 40, width: w, height: 1), SloopStyle.line)

        // BPM, swing, the selected track
        var bx = x
        bx += text("\(state.bpm)", at: CGPoint(x: bx, y: 46), size: 32, SloopStyle.fg)
        bx += text(" BPM", at: CGPoint(x: bx, y: 62), size: 16, SloopStyle.dim)
        bx += 16
        bx += text("SWING ", at: CGPoint(x: bx, y: 62), size: 16, SloopStyle.dim)
        bx += text("\(state.swing)", at: CGPoint(x: bx, y: 62), size: 16, SloopStyle.fg)
        bx += 16
        bx += text("FILT ", at: CGPoint(x: bx, y: 62), size: 16, SloopStyle.dim)
        _ = text(state.masterFilter == 0 ? "OFF" : state.masterFilter < 0 ? "LP \(-state.masterFilter)" : "HP \(state.masterFilter)",
                 at: CGPoint(x: bx, y: 62), size: 16, SloopStyle.fg)
        rightText("⌘? FAQ", right: x + w - 2, y: 62, size: 16, SloopStyle.dim)

        // the four tracks: number tile, level bar, the selected one marked (as SLOOP's TRACKS rows)
        let tracksW = min(w * 0.42, 260)
        for t in 0..<4 {
            let y = 88 + CGFloat(t) * 26
            let col = SloopStyle.track[t]
            let sel = t == state.selectedTrack
            fill(ctx, CGRect(x: x, y: y, width: 22, height: 22), sel ? col : col.withAlphaComponent(0.35))
            _ = text("\(t + 1)", at: CGPoint(x: x + 7, y: y + 3), size: 16, sel ? SloopStyle.bg : SloopStyle.fg)
            _ = text(t == 3 ? "DRUMS" : "TRACK \(t + 1)", at: CGPoint(x: x + 28, y: y + 3), size: 16,
                     sel ? SloopStyle.fg : SloopStyle.fg2)
            let barX = x + 28 + 72, barW = tracksW - 100
            let lvl = CGFloat(state.trackLevel.indices.contains(t) ? state.trackLevel[t] : 0) / 127
            fill(ctx, CGRect(x: barX, y: y + 8, width: barW, height: 6), SloopStyle.s2)
            fill(ctx, CGRect(x: barX, y: y + 8, width: (barW * lvl).rounded(), height: 6), sel ? col : SloopStyle.dim)
        }

        // the pads' job, and notes (MIDI inputs, audio errors)
        let lx = x + tracksW + 16, lw = x + w - lx
        if lw > 80 {
            _ = text("PADS", at: CGPoint(x: lx, y: 88), size: 16, SloopStyle.dim)
            _ = text(padMode.ddjLabel, at: CGPoint(x: lx + 44, y: 88), size: 16, SloopStyle.track[max(0, min(3, state.selectedTrack))])
            wrapped(padHelp.uppercased(), x: lx, y: 108, width: lw, size: 16, SloopStyle.fg2, maxLines: 3)
            var ny: CGFloat = 166
            for n in notes.prefix(1) {                     // (one line: the keyboard starts at 196)
                wrapped(n.uppercased(), x: lx, y: ny, width: lw, size: 16, SloopStyle.dim, maxLines: 1)
                ny += 18
            }
        }

        // the FM-1 keyboard, F3..G5: its LEDs (lit, glowing), so you see what the pads are doing
        let kbY: CGFloat = 196, kbH: CGFloat = 42
        let whites = FM1Keyboard.whiteKeys, blacks = FM1Keyboard.blackKeys
        let kw = ((w + 2) / CGFloat(whites.count)).rounded(.down)
        let col = SloopStyle.track[max(0, min(3, state.selectedTrack))]
        for (i, k) in whites.enumerated() {
            let r = CGRect(x: x + CGFloat(i) * kw, y: kbY, width: kw - 2, height: kbH)
            let lit = state.keyLit(k)
            fill(ctx, r, lit ? col : state.keyDim(k) ? col.withAlphaComponent(0.3) : SloopStyle.s3)
            if kw >= 24 {                                   // the note name at the foot of the key
                _ = text(FM1Keyboard.noteName(k), at: CGPoint(x: r.minX + 3, y: kbY + kbH - 17), size: 16,
                         lit ? SloopStyle.bg : SloopStyle.dim)
            }
        }
        for k in blacks {
            guard let w0 = whites.lastIndex(where: { $0 < k }) else { continue }
            let r = CGRect(x: x + CGFloat(w0 + 1) * kw - kw / 3 - 1, y: kbY, width: (kw * 2 / 3).rounded(), height: 22)
            fill(ctx, r.insetBy(dx: -1, dy: -1).offsetBy(dx: 0, dy: 1), SloopStyle.bg)
            fill(ctx, r, state.keyLit(k) ? col : state.keyDim(k) ? col.withAlphaComponent(0.3) : SloopStyle.s1)
        }
    }

    // ---- drawing helpers (screen pixels)

    private func fill(_ ctx: CGContext, _ r: CGRect, _ c: NSColor) {
        ctx.setFillColor(c.cgColor)
        ctx.fill(r)
    }

    private func attrs(_ size: CGFloat, _ c: NSColor) -> [NSAttributedString.Key: Any] {
        [.font: SloopStyle.font(size), .foregroundColor: c]
    }

    /// draws text with its top at p; returns its width
    @discardableResult
    private func text(_ s: String, at p: CGPoint, size: CGFloat, _ c: NSColor) -> CGFloat {
        let a = NSAttributedString(string: s, attributes: attrs(size, c))
        a.draw(at: p)
        return a.size().width.rounded()
    }

    private func rightText(_ s: String, right: CGFloat, y: CGFloat, size: CGFloat, _ c: NSColor) {
        let a = NSAttributedString(string: s, attributes: attrs(size, c))
        a.draw(at: CGPoint(x: (right - a.size().width).rounded(), y: y))
    }

    private func wrapped(_ s: String, x: CGFloat, y: CGFloat, width: CGFloat, size: CGFloat, _ c: NSColor, maxLines: Int) {
        var lines: [String] = [], cur = ""
        for word in s.split(separator: " ") {
            let next = cur.isEmpty ? String(word) : cur + " " + word
            if NSAttributedString(string: next, attributes: attrs(size, c)).size().width > width, !cur.isEmpty {
                lines.append(cur)
                cur = String(word)
            } else { cur = next }
        }
        if !cur.isEmpty { lines.append(cur) }
        for (i, l) in lines.prefix(maxLines).enumerated() {
            _ = text(l, at: CGPoint(x: x, y: y + CGFloat(i) * 18), size: size, c)
        }
    }
}

