// SPDX-License-Identifier: GPL-3.0-only
// Reads SLOOP's screen as text (SloopCore's text tap: every text drawn and where): what the four knobs are called
// and set to right now, the 16 tiles of a held layer, the page's title. The app prints these on the DDJ-400 controls
// that change them, so the screen and the controller say the same thing.
//
// SLOOP's screen is 240 x 240 pixels in four columns of 60 (one per knob, one per tile). Layouts (seen on SLOOP 2.8):
//   TRACKS (home) and the layers: the knob strip at the bottom, names at y ~208, values at y ~224
//   a page (ENV, LFO, FX...): the knob columns at the top, names at y ~26, values (+ units) at y ~44
//   a held layer: a title at the top left (FONT_L), 16 tiles in a 4 x 4 grid at y 40 + 36 r (r = 0..3)
//   the FX (punch) layer: white-key tiles in the lower half of each row, black-key tiles in the upper half

public struct ScreenText: Hashable, Sendable {
    public var x: Int, y: Int, w: Int, h: Int
    public var argb: UInt32
    public var text: String
    public init(x: Int, y: Int, w: Int, h: Int, argb: UInt32, text: String) {
        self.x = x; self.y = y; self.w = w; self.h = h; self.argb = argb; self.text = text
    }
    public var midX: Int { x + w / 2 }
}

/// One of SLOOP's four knobs as the screen shows it.
public struct KnobReadout: Hashable, Sendable {
    public var name: String          // "SWING", "ATK"
    public var value: String         // "0", "1.0 MS"
    public var argb: UInt32?         // the colour SLOOP gives its value (nil: the default grey)
    public init(name: String, value: String, argb: UInt32?) { self.name = name; self.value = value; self.argb = argb }
    public var caption: String { value.isEmpty ? name : name + " " + value }
}

public struct ScreenReading: Hashable, Sendable {
    public var knobs: [KnobReadout?] = [nil, nil, nil, nil]
    /// a held layer's tiles, white keys 0...15 (nil: blank)
    public var whiteTiles: [String?] = Array(repeating: nil, count: 16)
    /// the FX layer's black-key tiles, black keys 0...10
    public var blackTiles: [String?] = Array(repeating: nil, count: 11)
    public var title: String?        // a layer's title ("STEPS", "PUNCH")
    public var titleArgb: UInt32?    // its colour on SLOOP's screen
    /// where the knobs' strip is (screen pixels, for highlighting a knob's column): the top of the band and its height
    public var knobBand: (y: Int, h: Int)? = nil
    public init() {}

    public static func == (a: ScreenReading, b: ScreenReading) -> Bool {
        a.knobs == b.knobs && a.whiteTiles == b.whiteTiles && a.blackTiles == b.blackTiles && a.title == b.title &&
            a.titleArgb == b.titleArgb &&
            a.knobBand?.y == b.knobBand?.y && a.knobBand?.h == b.knobBand?.h
    }
    public func hash(into h: inout Hasher) { h.combine(knobs); h.combine(whiteTiles); h.combine(blackTiles); h.combine(title) }
}

public enum ScreenReader {
    static let grey: Set<UInt32> = [0x73757B, 0xC5C6CD, 0x838183, 0x525052, 0xE6E2E6, 0xBDBABD, 0xFFFFFF, 0x000000]

    /// - Parameters: layerShown: SloopCore says a layer's tiles are on screen; punch: the layer is FX (keyboard layout)
    public static func read(_ texts: [ScreenText], layerShown: Bool, punch: Bool) -> ScreenReading {
        var r = ScreenReading()
        r.knobs = knobs(texts)
        if let (y, _) = knobBandY(texts) { r.knobBand = (y - 4, 36) } else if r.knobs.contains(where: { $0 != nil }) {
            r.knobBand = (22, 40)
        }
        if layerShown {
            if let t = texts.filter({ $0.y < 12 && $0.h >= 32 }).min(by: { $0.x < $1.x }) {
                r.title = t.text.uppercased()
                r.titleArgb = t.argb & 0xFFFFFF
            }
            if punch { readPunch(texts, into: &r) } else { readTiles(texts, into: &r) }
        }
        return r
    }

    /// the bottom strip (home, layers) if there is one: the y of its names
    static func knobBandY(_ texts: [ScreenText]) -> (Int, Int)? {
        // the strip has its names at ~208 (a page's footer at ~222 alone is the engine and the sound, not knobs)
        guard let name = texts.first(where: { (200...214).contains($0.y) }) else { return nil }
        let value = texts.first { (218...232).contains($0.y) }
        return (name.y, value?.y ?? name.y + 16)
    }

    static func column(_ t: ScreenText) -> Int { max(0, min(3, t.midX / 60)) }

    static func knobs(_ texts: [ScreenText]) -> [KnobReadout?] {
        var out: [KnobReadout?] = [nil, nil, nil, nil]
        let nameRow: [ScreenText], valueRow: [ScreenText]
        if let (ny, vy) = knobBandY(texts) {
            nameRow = texts.filter { abs($0.y - ny) <= 3 }
            valueRow = texts.filter { abs($0.y - vy) <= 3 }
        } else {
            // a page: names at ~26, values at ~44 (the header at y < 12 and the footer at y > 200 left out)
            nameRow = texts.filter { (20...32).contains($0.y) }
            valueRow = texts.filter { (38...52).contains($0.y) }
        }
        for c in 0..<4 {
            let name = nameRow.filter { column($0) == c }.sorted { $0.x < $1.x }.map(\.text).joined(separator: " ")
            let vals = valueRow.filter { column($0) == c }.sorted { $0.x < $1.x }
            let value = vals.map(\.text).joined(separator: " ")
            guard !name.isEmpty || !value.isEmpty else { continue }
            let colour = vals.first.map { $0.argb & 0xFFFFFF }.flatMap { grey.contains($0) ? nil : $0 }
            out[c] = KnobReadout(name: name.uppercased(), value: value.uppercased(), argb: colour)
        }
        return out
    }

    /// the 4 x 4 tiles of a layer: rows at y 40 + 36 r, a tile's label near its top
    static func readTiles(_ texts: [ScreenText], into r: inout ScreenReading) {
        for t in texts where (40..<184).contains(t.y) {
            let row = (t.y - 40) / 36, col = column(t)
            let i = row * 4 + col
            guard (0..<16).contains(i) else { continue }
            r.whiteTiles[i] = (r.whiteTiles[i].map { $0 + " " } ?? "") + t.text.uppercased()
        }
    }

    /// the FX layer: per row, white-key labels in the lower half (y offset >= 17), black-key labels in the upper half
    static func readPunch(_ texts: [ScreenText], into r: inout ScreenReading) {
        var blacks: [(row: Int, x: Int, text: String)] = []
        for t in texts where (40..<184).contains(t.y) {
            let row = (t.y - 40) / 36, off = (t.y - 40) % 36
            if off >= 17 {
                let i = row * 4 + column(t)
                if (0..<16).contains(i) { r.whiteTiles[i] = t.text.uppercased() }
            } else {
                blacks.append((row, t.midX, t.text.uppercased()))
            }
        }
        // the black keys in keyboard order: row by row, left to right
        for (k, b) in blacks.sorted(by: { ($0.row, $0.x) < ($1.row, $1.x) }).prefix(11).enumerated() {
            r.blackTiles[k] = b.text
        }
    }
}
