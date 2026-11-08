// SPDX-License-Identifier: GPL-3.0-only
// The drawing's layers: the static backdrop (panels, printed labels: one cached image) and, over it, the moving parts
// in a grid of small transparent tiles. Measured: Core Animation copies a layer's whole previous image before any
// partial update (CABackingStoreBeginUpdate); with one window-sized layer every pad, meter or playhead change copied
// ~15 MB at 30 frames a second, which was most of the app's CPU. A tile only copies itself.
import AppKit

@MainActor
final class DDJLayerView: NSView {
    enum Kind { case backdrop, overlay }
    let kind: Kind
    weak var owner: DDJ400View?
    /// the tile being drawn (the owner's needsToDraw asks it)
    static weak var drawing: DDJLayerView?
    static let countDraws = ProcessInfo.processInfo.environment["DDJ_COUNT_DRAWS"] != nil
    /// set while the window is drawn into a bitmap (DDJ_SNAPSHOT)
    static var snapshotting = false
    static var draws = 0, since = 0.0

    init(kind: Kind) {
        self.kind = kind
        super.init(frame: .zero)
        wantsLayer = true
        layer?.contentsFormat = .RGBA8Uint
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    override var isFlipped: Bool { true }
    override var isOpaque: Bool { false }
    /// events, tooltips and accessibility belong to the controller view itself
    override func hitTest(_ point: NSPoint) -> NSView? { nil }

    override func draw(_ dirtyRect: NSRect) {
        guard let owner else { return }
        switch kind {
        case .backdrop:
            owner.drawBackdrop(dirtyRect)
        case .overlay:
            if Self.countDraws {                         // (DDJ_COUNT_DRAWS=1: tile redraws a second, for performance work)
                Self.draws += 1
                let t = ProcessInfo.processInfo.systemUptime
                if t - Self.since > 1 {
                    FileHandle.standardError.write("tile draws/s: \(Self.draws) last dirty: \(dirtyRect.integral)\n".data(using: .utf8)!)
                    Self.draws = 0; Self.since = t
                }
            }
            // the whole tile, cleared first: a transparent layer keeps its old pixels, so a partial redraw drew new text
            // over old text (seen: "BEC" for DEC over BDY), and a text crossing the edge of a partial redraw was cut
            guard let ctx = NSGraphicsContext.current else { return }
            let g = ctx.cgContext
            // (not in a snapshot: it draws every layer into one bitmap, and clearing there would erase the backdrop)
            if !Self.snapshotting { g.clear(bounds) }
            g.saveGState()
            g.translateBy(x: -frame.minX, y: -frame.minY)   // draw in the owner's coordinates (the tile is a window onto them)
            Self.drawing = self
            owner.drawDynamic(frame)
            Self.drawing = nil
            g.restoreGState()
        }
    }
}

/// The overlay: a grid of tiles over the owner's bounds, each redrawn on its own.
@MainActor
final class DDJTiledOverlay: NSView {
    weak var owner: DDJ400View? { didSet { tiles.forEach { $0.owner = owner } } }
    private var tiles: [DDJLayerView] = []
    static let columns = 4, rows = 3

    init() {
        super.init(frame: .zero)
        for _ in 0..<(Self.columns * Self.rows) {
            let t = DDJLayerView(kind: .overlay)
            tiles.append(t)
            addSubview(t)
        }
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    override var isFlipped: Bool { true }
    override func hitTest(_ point: NSPoint) -> NSView? { nil }

    override func layout() {
        super.layout()
        let w = (bounds.width / CGFloat(Self.columns)).rounded(.up), h = (bounds.height / CGFloat(Self.rows)).rounded(.up)
        for (i, t) in tiles.enumerated() {
            let c = i % Self.columns, r = i / Self.columns
            t.frame = CGRect(x: CGFloat(c) * w, y: CGFloat(r) * h, width: w, height: h)
        }
    }

    /// a change redraws the whole tiles it touches (cleared, every control in them): never a stale or cut text
    func invalidate(_ rect: NSRect) {
        for t in tiles where t.frame.intersects(rect) { t.needsDisplay = true }
    }

    func invalidateAll() { tiles.forEach { $0.needsDisplay = true } }
    var anyNeedsDisplay: Bool { tiles.contains { $0.needsDisplay } }

    /// during a tile's draw: does this rect (owner coordinates) touch the tile being drawn?
    func needsToDraw(ownerRect r: NSRect) -> Bool {
        guard let t = DDJLayerView.drawing else { return true }
        return t.frame.intersects(r)
    }
}
