// SPDX-License-Identifier: GPL-3.0-only
// A picture of what the page being edited does to the active track's sound, as an Elektron box shows it: the wave of
// the machine (SRC), the filter's response (FLTR), the envelope (AMP), the sends (FX), the LFO's wave (LFO), the
// delay's echoes and the reverb (MASTER), the trig (TRIG). Drawn from the parameters' values, live as knobs turn.
import AppKit
import DDJMapping

struct PageScope: Equatable {
    var page: TrigPage
    var machine: Machine
    var track: Int
    var color: UInt32
    /// the page's 8 values, raw, and normalised to 0...1 by their ranges
    var raw: [Int]
    var n: [Double]
    /// the page's 8 values as the knobs show them (for a caption or two)
    var shown: [String]

    @MainActor
    static func read(model: TrigModel, page: TrigPage, track: Int) -> PageScope {
        var raw: [Int] = [], n: [Double] = [], shown: [String] = []
        for slot in 0..<8 {
            let info = page == .master ? model.globalInfo(slot: slot) : model.info(track: track, param: page.rawValue * 8 + slot)
            let v = page == .master ? model.globalValue(slot: slot) : model.liveValue(track: track, param: page.rawValue * 8 + slot)
            raw.append(v)
            if let info, info.max > info.min {
                n.append(Double(v - info.min) / Double(info.max - info.min))
                shown.append(model.format(info, v))
            } else {
                n.append(0)
                shown.append("")
            }
        }
        return PageScope(page: page, machine: model.machine(track), track: track, color: TrigColors.track[track],
                         raw: raw, n: n, shown: shown)
    }

    /// Draws into `r`. `since`: seconds since the track last played (a dot runs along time-based pictures), or nil.
    func draw(_ g: CGContext, _ r: CGRect, since: Double?) {
        let c = NSColor(argb: color)
        let plot = r.insetBy(dx: 6, dy: 4)
        // the axis
        g.setStrokeColor(SloopStyle.s3.cgColor)
        g.setLineWidth(1)
        g.move(to: CGPoint(x: plot.minX, y: plot.maxY)); g.addLine(to: CGPoint(x: plot.maxX, y: plot.maxY))
        g.strokePath()
        switch page {
        case .src: drawSource(g, plot, c, since: since)
        case .fltr: drawFilter(g, plot, c)
        case .amp: drawEnvelope(g, plot, c, since: since)
        case .fx: drawSends(g, plot, c)
        case .lfo: drawLFO(g, plot, c)
        case .master: drawMaster(g, plot, c)
        case .trig: drawTrig(g, plot, c)
        }
    }

    // MARK: SRC: the machine's wave

    private func drawSource(_ g: CGContext, _ r: CGRect, _ c: NSColor, since: Double?) {
        let N = 360
        var ys: [Double] = []
        var rng: UInt32 = 0x1234_5678
        func noise() -> Double { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return Double(rng) / Double(UInt32.max) * 2 - 1 }
        var phase = 0.0
        for i in 0..<N {
            let x = Double(i) / Double(N - 1)                    // time, 0...1 of the picture
            var y = 0.0
            switch machine {
            case .kick, .tom:
                // a sine falling in pitch (SWEEP over SWTIME), dying away (DECAY), a click at the start
                let tune = Double(raw[0]) / 24, decay = 0.08 + n[1] * 0.9, sweep = n[2], swt = 0.02 + n[3] * 0.3
                let f = (machine == .kick ? 5.0 : 9.0) * pow(2, tune) * (1 + sweep * 5 * exp(-x / swt))
                phase += f / Double(N) * 2 * .pi
                let amp = exp(-x / decay) * (machine == .kick && x < n[7] * 0.3 ? 1 : 1)
                y = sin(phase) * amp
                if machine == .kick, x < 0.015 { y += n[4] * (1 - x / 0.015) }
                y = tanh(y * (1 + n[5] * 4)) / tanh(1 + n[5] * 4)
            case .snare:
                // a body (TUNE, DECAY) under noise (NOISE, NDECAY)
                let body = sin(Double(i) * 0.35 * pow(2, Double(raw[0]) / 24)) * exp(-x / (0.05 + n[1] * 0.5)) * (0.3 + n[5] * 0.7)
                y = body + noise() * n[2] * exp(-x / (0.04 + n[3] * 0.5))
            case .hat:
                // a burst of noise: DECAY and OPEN set how long, BRIGHT how busy
                let len = 0.02 + n[1] * 0.2 + n[2] * 0.6
                let busy = noise() * (0.4 + n[4] * 0.6) + (i % 2 == 0 ? 0.2 : -0.2) * n[3]
                y = busy * exp(-x / len)
            case .clap:
                // BURSTS hits SPREAD apart, then the tail (DECAY) and the room (ROOM)
                let bursts = max(1, raw[3]), gap = 0.015 + n[2] * 0.05
                let lastHit = Double(bursts - 1) * gap
                var e = 0.0
                if x <= lastHit + gap { e = exp(-(x.truncatingRemainder(dividingBy: gap)) / (gap * 0.3)) }
                if x > lastHit { e = max(e, exp(-(x - lastHit) / (0.05 + n[1] * 0.5)) + n[5] * 0.25 * exp(-(x - lastHit) / 0.6)) }
                y = noise() * min(1, e)
            case .synth:
                // two cycles of the wave (SHAPE: sine, triangle, saw, square), the SUB an octave down
                let p = (x * 2).truncatingRemainder(dividingBy: 1)
                y = Self.morph(p, n[1]) * (1 - n[4] * 0.4) + sin(x * .pi * 2) * n[4] * 0.6
                y += noise() * n[5] * 0.3
            case .fm:
                // a sine modulated at RATIO, by INDEX, the index dying away (ENVDEC) by ENVAMT
                let ratios: [Double] = [0.5, 1, 1.5, 2, 2.5, 3, 3.5, 4, 4.5, 5, 6, 7, 8, 9, 11, 14]
                let ratio = ratios[max(0, min(15, raw[1]))]
                let idx = n[2] * 6 * (1 - n[5] + n[5] * exp(-x / (0.05 + n[4] * 0.6)))
                y = sin(2 * .pi * 3 * x + idx * sin(2 * .pi * 3 * ratio * x))
            case .sample:
                // the slot's span: START to END (dimmed outside), REVERSE, LOOP
                let inSpan = x >= n[2] && x <= max(n[2], n[3])
                y = noise() * (0.3 + 0.6 * abs(sin(x * 9))) * (inSpan ? 1 : 0.25)
            }
            ys.append(max(-1, min(1, y)))
        }
        plotWave(g, r, ys, c)
        if machine == .sample {
            g.setStrokeColor(c.cgColor); g.setLineWidth(1.5)
            for v in [n[2], n[3]] {
                let x = r.minX + CGFloat(v) * r.width
                g.move(to: CGPoint(x: x, y: r.minY)); g.addLine(to: CGPoint(x: x, y: r.maxY))
            }
            g.strokePath()
        }
        if let s = since, machine != .synth { runDot(g, r, x: s / 0.8, c) }
    }

    /// sine (0) -> triangle -> saw -> square (1), one cycle, p in 0..<1
    private static func morph(_ p: Double, _ shape: Double) -> Double {
        let sine = sin(2 * .pi * p)
        let tri = p < 0.25 ? p * 4 : p < 0.75 ? 2 - p * 4 : p * 4 - 4
        let saw = p < 0.5 ? p * 2 : p * 2 - 2
        let sq: Double = p < 0.5 ? 1 : -1
        let waves = [sine, tri, saw, sq]
        let s = shape * 3, i = min(2, Int(s)), f = s - Double(i)
        return waves[i] * (1 - f) + waves[i + 1] * f
    }

    private func plotWave(_ g: CGContext, _ r: CGRect, _ ys: [Double], _ c: NSColor) {
        let mid = r.midY, half = r.height / 2 - 2
        g.setStrokeColor(SloopStyle.s3.cgColor); g.setLineWidth(1)
        g.move(to: CGPoint(x: r.minX, y: mid)); g.addLine(to: CGPoint(x: r.maxX, y: mid)); g.strokePath()
        g.setStrokeColor(c.cgColor); g.setLineWidth(1.5)
        for (i, y) in ys.enumerated() {
            let p = CGPoint(x: r.minX + CGFloat(i) / CGFloat(ys.count - 1) * r.width, y: mid - CGFloat(y) * half)
            if i == 0 { g.move(to: p) } else { g.addLine(to: p) }
        }
        g.strokePath()
    }

    private func runDot(_ g: CGContext, _ r: CGRect, x: Double, _ c: NSColor) {
        guard x >= 0, x <= 1 else { return }
        let px = r.minX + CGFloat(x) * r.width
        g.setFillColor(SloopStyle.fg.withAlphaComponent(0.8).cgColor)
        g.fill(CGRect(x: px - 1, y: r.minY, width: 2, height: r.height))
    }

    // MARK: FLTR: the response, cutoff and resonance; the envelope's reach as a ghost

    private func drawFilter(_ g: CGContext, _ r: CGRect, _ c: NSColor) {
        let type = max(0, min(2, raw[2]))
        let q = 0.5 + n[1] * n[1] * 12
        func curve(_ cut: Double) -> [CGPoint] {
            (0...200).map { i in
                let x = Double(i) / 200
                let ratio = pow(1000, x - cut)                  // f / fc on a 20 Hz .. 20 kHz log axis
                let d = sqrt(pow(1 - ratio * ratio, 2) + pow(ratio / q, 2))
                let mag = type == 0 ? 1 / d : type == 2 ? ratio * ratio / d : (ratio / q) / d
                let db = max(-36, min(18, 20 * log10(max(1e-6, mag))))
                return CGPoint(x: r.minX + CGFloat(x) * r.width, y: r.minY + CGFloat((18 - db) / 54) * r.height)
            }
        }
        let env = Double(raw[3]) / 64                           // -1 .. 1
        if abs(env) > 0.02 {
            g.setStrokeColor(c.withAlphaComponent(0.3).cgColor); g.setLineWidth(1.2)
            g.addLines(between: curve(max(0, min(1, n[0] + env * 0.5))))
            g.strokePath()
        }
        // 0 dB
        g.setStrokeColor(SloopStyle.s3.cgColor); g.setLineWidth(1)
        let zero = r.minY + CGFloat(18.0 / 54) * r.height
        g.move(to: CGPoint(x: r.minX, y: zero)); g.addLine(to: CGPoint(x: r.maxX, y: zero)); g.strokePath()
        let pts = curve(n[0])
        g.setFillColor(c.withAlphaComponent(0.18).cgColor)
        g.move(to: CGPoint(x: r.minX, y: r.maxY))
        for p in pts { g.addLine(to: p) }                         // (addLines would start a new subpath)
        g.addLine(to: CGPoint(x: r.maxX, y: r.maxY)); g.closePath()
        g.fillPath()
        g.setStrokeColor(c.cgColor); g.setLineWidth(2)
        g.addLines(between: pts); g.strokePath()
    }

    // MARK: AMP: attack, hold, decay, sustain, release

    private func drawEnvelope(_ g: CGContext, _ r: CGRect, _ c: NSColor, since: Double?) {
        let a = 0.01 + n[0] * n[0] * 0.5, h = n[1] * n[1] * 0.3, d = 0.02 + n[2] * n[2] * 0.6, s = n[3]
        let hold = 0.25, rel = 0.02 + n[4] * n[4] * 0.6
        let total = a + h + d + hold + rel
        let top = r.minY + r.height * CGFloat(1 - (0.35 + 0.65 * n[6]))   // LEVEL sets the peak
        func px(_ t: Double) -> CGFloat { r.minX + CGFloat(t / total) * r.width }
        func py(_ v: Double) -> CGFloat { r.maxY - CGFloat(v) * (r.maxY - top) }
        // decay is exponential: drawn in steps
        var pts = [CGPoint(x: px(0), y: py(0)), CGPoint(x: px(a), y: py(1)), CGPoint(x: px(a + h), y: py(1))]
        for k in 1...20 {
            let t = Double(k) / 20
            pts.append(CGPoint(x: px(a + h + d * t), y: py(s + (1 - s) * exp(-t * 4) * (1 - t))))
        }
        pts.append(CGPoint(x: px(a + h + d + hold), y: py(s)))
        for k in 1...20 {
            let t = Double(k) / 20
            pts.append(CGPoint(x: px(a + h + d + hold + rel * t), y: py(s * exp(-t * 4) * (1 - t))))
        }
        g.setFillColor(c.withAlphaComponent(0.18).cgColor)
        g.addLines(between: pts); g.addLine(to: CGPoint(x: px(total), y: r.maxY)); g.closePath(); g.fillPath()
        g.setStrokeColor(c.cgColor); g.setLineWidth(2)
        g.addLines(between: pts); g.strokePath()
        // PAN: a dot on a short bar, top right
        let bar = CGRect(x: r.maxX - 60, y: r.minY + 2, width: 56, height: 2)
        g.setFillColor(SloopStyle.s3.cgColor); g.fill(bar)
        g.setFillColor(SloopStyle.fg.cgColor)
        g.fillEllipse(in: CGRect(x: bar.minX + CGFloat(n[5]) * bar.width - 3, y: bar.midY - 3, width: 6, height: 6))
        if let since { runDot(g, r, x: since / 1.2, c) }
    }

    // MARK: FX: the sends and the rest, as bars

    private func drawSends(_ g: CGContext, _ r: CGRect, _ c: NSColor) {
        let names = ["DLY", "REV", "CHO", "BIT", "SRR", "LCT", "DCK", "WID"]
        let w = r.width / 8
        for i in 0..<8 {
            let bh = (r.height - 14) * CGFloat(n[i])
            let x = r.minX + CGFloat(i) * w + w * 0.2
            g.setFillColor(SloopStyle.s2.cgColor); g.fill(CGRect(x: x, y: r.minY, width: w * 0.6, height: r.height - 14))
            g.setFillColor(c.withAlphaComponent(i < 3 ? 1 : 0.6).cgColor)
            g.fill(CGRect(x: x, y: r.maxY - 14 - bh, width: w * 0.6, height: bh))
            let a = NSAttributedString(string: names[i], attributes: [.font: SloopStyle.font(10), .foregroundColor: SloopStyle.fg2])
            a.draw(at: CGPoint(x: x + w * 0.3 - a.size().width / 2, y: r.maxY - 12))
        }
    }

    // MARK: LFO: its wave, speed and depth

    private func drawLFO(_ g: CGContext, _ r: CGRect, _ c: NSColor) {
        let cycles = max(0.5, min(8, 0.5 + n[0] * 3 * (1 + Double(raw[1]) / 4)))
        let depth = Double(raw[4]) / 64, fade = Double(raw[5]) / 64, phase = n[7]
        var rng: UInt32 = 0xBEEF
        var held = 0.0, lastStep = -1
        let ys: [Double] = (0...300).map { i in
            let x = Double(i) / 300
            let p = (x * cycles + phase).truncatingRemainder(dividingBy: 1)
            var y: Double
            switch raw[2] {
            case 1: y = p < 0.5 ? p * 4 - 1 : 3 - p * 4
            case 2: y = 1 - p * 2
            case 3: y = p < 0.5 ? 1 : -1
            case 4:
                let st = Int(x * cycles * 2)
                if st != lastStep { lastStep = st; rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; held = Double(rng) / Double(UInt32.max) * 2 - 1 }
                y = held
            default: y = sin(2 * .pi * p)
            }
            let f = fade > 0 ? min(1, x / max(0.01, fade)) : fade < 0 ? max(0, 1 - x / max(0.01, -fade)) : 1
            return y * depth * f
        }
        plotWave(g, r, ys, c)
        let dest = NSAttributedString(string: "→ " + shown[3], attributes: [.font: SloopStyle.font(11), .foregroundColor: SloopStyle.fg2])
        dest.draw(at: CGPoint(x: r.maxX - dest.size().width, y: r.minY))
    }

    // MARK: MASTER: the delay's echoes, the reverb's tail

    private func drawMaster(_ g: CGContext, _ r: CGRect, _ c: NSColor) {
        // reverb: a tail as long as SIZE, darker with DAMP
        let rl = 0.15 + n[3] * 0.8
        g.setFillColor(NSColor(argb: 0xFF7A5CFF).withAlphaComponent(0.15 + 0.2 * (1 - n[4])).cgColor)
        g.move(to: CGPoint(x: r.minX, y: r.maxY))
        for k in 0...30 {
            let t = Double(k) / 30
            g.addLine(to: CGPoint(x: r.minX + CGFloat(t * rl) * r.width, y: r.maxY - CGFloat(exp(-t * 3)) * r.height * 0.6))
        }
        g.addLine(to: CGPoint(x: r.minX + CGFloat(rl) * r.width, y: r.maxY)); g.closePath(); g.fillPath()
        // delay: echoes TIME apart, each FDBK of the last
        let gap = 0.04 + Double(raw[0]) / 11 * 0.25
        var t = 0.0, lv = 1.0
        g.setFillColor(c.cgColor)
        while t <= 1 && lv > 0.03 {
            let x = r.minX + CGFloat(t) * r.width
            let h = CGFloat(lv) * r.height
            g.fill(CGRect(x: x, y: r.maxY - h, width: 4, height: h))
            t += gap
            lv *= 0.15 + n[1] * 0.83
        }
    }

    // MARK: TRIG: the note

    private func drawTrig(_ g: CGContext, _ r: CGRect, _ c: NSColor) {
        // a bar: as long as LENGTH, as high as VELOCITY; RETRIG ticks inside it
        let len = 0.05 + n[2] * 0.9, vel = n[1]
        let bar = CGRect(x: r.minX, y: r.maxY - CGFloat(vel) * r.height, width: CGFloat(len) * r.width, height: CGFloat(vel) * r.height)
        g.setFillColor(c.withAlphaComponent(0.35).cgColor); g.fill(bar)
        g.setFillColor(c.cgColor); g.fill(CGRect(x: bar.minX, y: bar.minY, width: bar.width, height: 3))
        let rt = raw[3]
        if rt > 0 {
            g.setFillColor(SloopStyle.fg.cgColor)
            for k in 0...rt {
                let x = bar.minX + bar.width * CGFloat(k) / CGFloat(rt + 1)
                g.fill(CGRect(x: x, y: bar.minY, width: 2, height: bar.height))
            }
        }
        let note = NSAttributedString(string: shown[0], attributes: [.font: SloopStyle.font(22), .foregroundColor: SloopStyle.fg])
        note.draw(at: CGPoint(x: r.maxX - note.size().width, y: r.minY))
    }
}
