// SPDX-License-Identifier: GPL-3.0-only
// The on-screen DDJ-400, the augmented controller: a flat drawing of the controller's top panel in the app's style
// (black, greys, track colours, the Terminus pixel font) where every control shows on itself its job and live value
// (faces: title, value, arc, colour, light, badge, outline), the jogs show their deck's tracks, and a thin header in
// the mixer carries the global state. It moves as the hardware is played (show(_:), meters) and turns clicks, drags
// and scrolls into the same DDJEvents the hardware sends, so the app can be played without a controller.
// The older caption / light / accent API (the SLOOP cartridge) still works where a control has no face.
//
// This file: the API, state, layout, invalidation and interaction. Drawing: DDJ400View+Drawing.swift.
// Accessibility: DDJ400View+Accessibility.swift.

import AppKit
import QuartzCore
import DDJMapping

/// One of a deck's tracks, as its jog wheel shows it.
public struct DeckTrackFace: Equatable {
    public var name: String
    public var color: NSColor
    /// output level, 0...1
    public var level: Double
    /// the selected track
    public var active: Bool
    public var muted: Bool
    public init(name: String, color: NSColor, level: Double = 0, active: Bool = false, muted: Bool = false) {
        self.name = name; self.color = color; self.level = level; self.active = active; self.muted = muted
    }
}

@MainActor public final class DDJ400View: NSView {

    // MARK: - API

    /// Clicks, drags and scrolls on the drawing, as DDJ events.
    public var onEvent: ((DDJEvent) -> Void)?

    /// The control under the mouse pointer (nil when none), on every change.
    public var onHover: ((DDJControl?) -> Void)?

    /// Help for a control: tooltips and the accessibility help.
    public var helpText: ((DDJControl) -> String?)? {
        didSet { refreshToolTips() }
    }

    /// What each control shows: its job (title), live value, arc, colour, light, badge and outline. A control with a
    /// face shows it instead of its caption, light and accent. Set it as often as needed: only changed controls redraw.
    public var faces: [DDJControl: ControlFace] = [:] {
        didSet {
            guard faces != oldValue else { return }
            let changed = Self.changedKeys(faces, oldValue)
            let t = now()
            for c in changed where faces[c]?.value != nil && faces[c]?.value != oldValue[c]?.value {
                // the value changed because this control was just moved: show it big for a moment
                if let touched = lastTouched[c], t - touched < Self.flashAttribution { flash(c, at: t) }
            }
            invalidate(changed)
        }
    }

    /// Each deck's tracks, drawn around its jog wheel (level meters update often: only that jog redraws).
    public var deckTracks: [Deck: [DeckTrackFace]] = [:] {
        didSet {
            for d in Deck.allCases where deckTracks[d] != oldValue[d] {
                // only the levels moved (every few frames while music plays): just the meter ring around the centre
                // (a whole jog redrawn 20 times a second per deck was the drawing's largest cost)
                let old = oldValue[d] ?? [], new = deckTracks[d] ?? []
                let levelsOnly = old.count == new.count && zip(old, new).allSatisfy {
                    $0.name == $1.name && $0.color == $1.color && $0.active == $1.active && $0.muted == $1.muted
                }
                if levelsOnly, let it = items.first(where: { $0.control == .jog(d) }) {
                    let R = it.rect.width / 2, meterR = R * 0.42 + 9 + 5
                    invalidateDesign(CGRect(x: it.rect.midX - meterR, y: it.rect.midY - meterR, width: 2 * meterR, height: 2 * meterR))
                } else {
                    invalidate([.jog(d)])
                }
            }
        }
    }

    /// Where each deck's track-ring marker points, in quadrants of its ring (0: the centre of the first track's, 1 the
    /// second's ...). Without one, the marker turns with the platter.
    public var jogDials: [Deck: Double] = [:] {
        didSet { for d in Deck.allCases where jogDials[d] != oldValue[d] { invalidate([.jog(d)]) } }
    }

    /// The global state shown in the mixer's top strip (nil: the connection status line).
    public var header: TrigHeader? {
        didSet { if header != oldValue { invalidateDesign(Self.statusRect) } }
    }

    /// LED state per control; missing = .off.
    public var lights: [DDJControl: LightLevel] = [:] {
        didSet { if lights != oldValue { invalidate(Self.changedKeys(lights, oldValue)) } }
    }

    /// The live job printed on each control; missing = nothing printed.
    public var captions: [DDJControl: String] = [:] {
        didSet { if captions != oldValue { invalidate(Self.changedKeys(captions, oldValue)) } }
    }

    /// A colour per control: a knob's value ring, pointer and caption take it (any control's caption does, and a
    /// fader's takeover mark). Missing: the default colours.
    public var knobAccents: [DDJControl: NSColor] = [:] {
        didSet { if knobAccents != oldValue { invalidate(Self.changedKeys(knobAccents, oldValue)) } }
    }

    /// Each deck's pad mode: its mode buttons light and its pads send it.
    public var padModes: [Deck: PadMode] = [.left: .hotCue, .right: .hotCue] {
        didSet {
            var changed = Set<DDJControl>()
            for d in Deck.allCases where padModes[d] != oldValue[d] { changed.formUnion(deckPadControls(d)) }
            invalidate(changed)
        }
    }

    /// Both decks' pad mode (reads the left deck's; setting it sets both).
    public var padMode: PadMode {
        get { padModes[.left] ?? .hotCue }
        set { padModes = [.left: newValue, .right: newValue] }
    }

    /// Level meters for channel 1 and 2, 0...1.
    public var meters: (Double, Double) = (0, 0) {
        didSet {   // only when a segment lights or goes out, and only the meters' area
            if Self.segments(meters.0) != Self.segments(oldValue.0) || Self.segments(meters.1) != Self.segments(oldValue.1) {
                invalidateDesign(Self.meterRect)
            }
        }
    }

    /// Whether a real controller is connected (a dot in the header, or the status line).
    public var connected = false {
        didSet { if connected != oldValue { invalidateDesign(Self.statusRect) } }
    }

    /// The selected track, 0...3: lit pads, the jog touch ring and default takeover marks take its colour when no
    /// face gives one.
    public var selectedTrack = 0 {
        didSet { if selectedTrack != oldValue { needsDisplay = true } }
    }

    /// Soft takeover per fader (or knob): `target` is where the value really is (a mark on the travel); while
    /// `active` is false the cap is drawn hollow: it does nothing until it is moved past the mark.
    public var takeovers: [DDJControl: (target: Double, active: Bool)] = [:] {
        didSet {
            var changed = Set<DDJControl>()
            for (k, v) in takeovers {
                if let o = oldValue[k], o.target == v.target, o.active == v.active { continue }
                changed.insert(k)
            }
            for k in oldValue.keys where takeovers[k] == nil { changed.insert(k) }
            invalidate(changed)
        }
    }

    /// The control holding the current layer: drawn filled with `heldColor`, outlined and tagged "HELD".
    public var heldButton: DDJControl? {
        didSet { if heldButton != oldValue { invalidate(Set([heldButton, oldValue].compactMap { $0 })) } }
    }

    /// The held layer's colour.
    public var heldColor: NSColor = SloopStyle.fg {
        didSet { if heldColor != oldValue, let h = heldButton { invalidate([h]) } }
    }

    /// Controls to point at from elsewhere: drawn with a bright outline.
    public var highlighted: Set<DDJControl> = [] {
        didSet { if highlighted != oldValue { invalidate(highlighted.symmetricDifference(oldValue)) } }
    }

    /// Reflect the hardware: presses highlight until released, absolute values move knobs and faders,
    /// relative ticks spin the jog's marker (and BROWSE's).
    public func show(_ event: DDJEvent) {
        let c = event.control
        switch event.value {
        case .press(let down):
            if down { hwPressed.insert(c) } else { hwPressed.remove(c) }
        case .absolute(let v):
            values[c] = Self.clamp(v)
            explicitPosition.insert(c)
            lastTouched[c] = now()
        case .relative(let n):
            switch c {
            case .jog(let d): spinJog(d, by: CGFloat(n) * Self.hwJogRadPerTick)
            case .browse: browseAngle += CGFloat(n) * Self.browseRadPerTick
            default: break
            }
            lastTouched[c] = now()
        }
        invalidate([c])
    }

    /// Put a fader, the tempo slider, the crossfader or a knob where the app's value is (no event is sent). Without
    /// it, a control whose face has an arc is drawn at the arc.
    public func setPosition(_ control: DDJControl, _ value: Double) {
        let v = Self.clamp(value)
        explicitPosition.insert(control)
        guard values[control] != v else { return }
        values[control] = v
        invalidate([control])
    }

    /// Forget the hardware presses (the controller went away).
    public func clearPressed() {
        guard !hwPressed.isEmpty else { return }
        let old = hwPressed
        hwPressed.removeAll()
        invalidate(old)
    }

    /// Where the columns start, as fractions of the width: left deck 0, mixer, right deck. The body is open at the
    /// top so a view above with the same columns (s1 fill, 1 pt `line` borders) joins it.
    public nonisolated static let columnFractions: (mixerStart: CGFloat, rightStart: CGFloat) = (mixerX / designW, rightDeckX / designW)

    // MARK: - Invalidation: only what changed is redrawn (meters, playhead and levels change many times a second)

    nonisolated static let meterSegmentCount = 15
    nonisolated static let meterRect = CGRect(x: mixerMid - 10, y: 222, width: 20, height: 178)
    /// the header (or status line) at the top of the mixer
    nonisolated static let statusRect = CGRect(x: mixerX + 2, y: 0, width: rightDeckX - mixerX - 4, height: 46)

    nonisolated static func segments(_ v: Double) -> Int {
        Int((clamp(v) * Double(meterSegmentCount)).rounded(.toNearestOrAwayFromZero))
    }

    nonisolated static func changedKeys<V: Equatable>(_ new: [DDJControl: V], _ old: [DDJControl: V]) -> Set<DDJControl> {
        var out = Set<DDJControl>()
        for (k, v) in new where old[k] != v { out.insert(k) }
        for k in old.keys where new[k] == nil { out.insert(k) }
        return out
    }

    /// the part of the design an item paints: the control with its outlines, its title and value, its labels
    func area(_ it: Item) -> CGRect {
        var r = it.rect.insetBy(dx: -14, dy: -14)   // arcs, press, held and highlight outlines
        if let c = captionRect(it) { r = r.union(c.insetBy(dx: -2, dy: -2)) }
        if it.valuePos == .underTitle, let c = captionRect(it) {
            r = r.union(CGRect(x: c.minX, y: c.maxY, width: c.width, height: lineHeight(capSize) + 4))
        }
        if it.labelPos == .below || it.valuePos == .below || isMode(it) {
            r = r.union(CGRect(x: it.rect.midX - 60, y: it.rect.maxY, width: 120, height: 46))
        }
        if it.labelPos == .left { r = r.union(CGRect(x: it.rect.minX - 110, y: it.rect.minY, width: 110, height: it.rect.height)) }
        if case .vfader = it.kind { r = r.insetBy(dx: -4, dy: 0) }
        return r
    }

    private func isMode(_ it: Item) -> Bool { if case .mode = it.kind { return true }; return false }

    func viewRect(_ design: CGRect) -> CGRect {
        let f = fit
        return CGRect(x: f.origin.x + design.minX * f.scale, y: f.origin.y + design.minY * f.scale,
                      width: design.width * f.scale, height: design.height * f.scale).insetBy(dx: -2, dy: -2)
    }

    func invalidateDesign(_ r: CGRect) { setNeedsDisplay(viewRect(r)) }

    func invalidate(_ controls: Set<DDJControl>) {
        guard !controls.isEmpty else { return }
        for c in controls {
            if let i = itemIndex(for: c) { invalidateDesign(area(items[i])) }
        }
    }

    private func deckPadControls(_ d: Deck) -> Set<DDJControl> {
        var s = Set<DDJControl>((0..<8).map { DDJControl.pad(d, $0) })
        for m in [PadMode.hotCue, .beatLoop, .beatJump, .sampler] { s.insert(.padModeButton(d, m)) }
        return s
    }

    // MARK: - Value flashes: a value grows for a moment while its control is being moved

    nonisolated static let flashDuration: CFTimeInterval = 0.8
    /// a face's value change counts as caused by the control if it was moved this recently
    nonisolated static let flashAttribution: CFTimeInterval = 0.5

    func flash(_ c: DDJControl, at t: CFTimeInterval? = nil) {
        let t = t ?? now()
        let was = isFlashing(c)
        flashStart[c] = t
        if !was { invalidate([c]) }
        scheduleFlashTimer()
    }

    func isFlashing(_ c: DDJControl) -> Bool {
        guard let s = flashStart[c] else { return false }
        return now() - s < Self.flashDuration
    }

    private func scheduleFlashTimer() {
        guard flashTimer == nil, let first = flashStart.values.min() else { return }
        let delay = max(0.01, first + Self.flashDuration - now())
        flashTimer = Timer.scheduledTimer(withTimeInterval: delay, repeats: false) { [weak self] _ in
            MainActor.assumeIsolated { self?.flashTick() }
        }
    }

    /// ends the flashes that are over (redrawing only those controls) and waits for the next
    func flashTick() {
        flashTimer?.invalidate()
        flashTimer = nil
        let t = now()
        let ended = Set(flashStart.filter { t - $0.value >= Self.flashDuration - 0.005 }.keys)
        for c in ended { flashStart[c] = nil }
        invalidate(ended)
        scheduleFlashTimer()
    }

    // MARK: - Init

    public override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        commonInit()
    }

    public required init?(coder: NSCoder) {
        super.init(coder: coder)
        commonInit()
    }

    public convenience init() {
        self.init(frame: NSRect(x: 0, y: 0, width: Self.designW, height: Self.designH))
    }

    private func commonInit() {
        setAccessibilityRole(.group)
        setAccessibilityLabel("DDJ-400 controller")
        // two layers: the static backdrop, and the moving parts over it (see drawDynamic)
        backdrop.owner = self
        overlay.owner = self
        for v in [backdrop, overlay] as [NSView] {
            v.frame = bounds
            v.autoresizingMask = [.width, .height]
            addSubview(v)
        }
    }

    private let backdrop = DDJLayerView(kind: .backdrop)
    private let overlay = DDJTiledOverlay()

    // every redraw request goes to the overlay's tiles; the backdrop redraws only when the size or scale changes
    public override var needsDisplay: Bool {
        get { overlay.anyNeedsDisplay }
        set { if newValue { overlay.invalidateAll() } }
    }
    public override func setNeedsDisplay(_ invalidRect: NSRect) { overlay.invalidate(invalidRect) }
    public override func needsToDraw(_ rect: NSRect) -> Bool { overlay.needsToDraw(ownerRect: rect) }
    public override func draw(_ dirtyRect: NSRect) {}
    func backdropNeedsDisplay() { backdrop.needsDisplay = true }

    public override var isFlipped: Bool { true }
    public override var isOpaque: Bool { false }
    public override var mouseDownCanMoveWindow: Bool { false }
    public override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    public override func setFrameSize(_ newSize: NSSize) {
        super.setFrameSize(newSize)
        geometryChanged()
    }

    public override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        geometryChanged()
    }

    private func geometryChanged() {
        refreshToolTips()
        updateAccessibilityFrames()
    }

    // MARK: - Layout (design space 1240 x 700, y down: the DDJ-400's 482 x 272 mm; decks 440 wide, mixer 360)

    nonisolated static let designW: CGFloat = 1240
    nonisolated static let designH: CGFloat = 700
    nonisolated static let mixerX: CGFloat = 440
    nonisolated static let rightDeckX: CGFloat = 800
    /// the mixer's columns: left (the name, HEADPHONES) 440-500, middle (the channels) 500-710, right (MASTER, BEAT FX)
    nonisolated static let leftColX: CGFloat = 500
    nonisolated static let rightColX: CGFloat = 710
    nonisolated static let mixerMid: CGFloat = (leftColX + rightColX) / 2
    nonisolated static let leftColMid: CGFloat = (mixerX + leftColX) / 2
    nonisolated static let rightColMid: CGFloat = (rightColX + rightDeckX) / 2
    nonisolated static let beatFXBox = CGRect(x: rightColX + 4, y: 222, width: rightDeckX - rightColX - 8, height: 350)

    struct Item: Sendable {
        enum Kind: Sendable {
            case button, shiftButton, round, pad, mode(PadMode, PadMode), knob, vfader, hfader, jog, browse, segment
        }
        /// the DDJ's printed label: on the control, under it, to its left, or none (side blocks carry it)
        enum LabelPos: Sendable { case inside, below, left, none }
        /// where the title goes
        enum CaptionPos: Sendable {
            /// one line centred at a point, so a row of titles lines up
            case at(CGPoint, width: CGFloat)
            /// one line just above the control
            case above(width: CGFloat)
            /// a block beside the control: the title, the value and the DDJ label, aligned to the control
            case side(left: Bool, width: CGFloat)
            /// on the control (pads, the jog's centre, the channel switch)
            case inside
        }
        /// where the value goes
        enum ValuePos: Sendable { case inside, below, underTitle, inline, block }
        var control: DDJControl
        var kind: Kind
        var rect: CGRect
        var label: String
        var labelPos: LabelPos
        var caption: CaptionPos
        var valuePos: ValuePos
    }

    let items: [Item] = DDJ400View.makeLayout()
    private(set) lazy var indexByControl: [DDJControl: Int] = {
        var d: [DDJControl: Int] = [:]
        for (i, it) in items.enumerated() { d[it.control] = i }
        return d
    }()

    /// the item that draws a control (aliases: BROWSE's press, a jog's touch, a mode button's shifted mode)
    func itemIndex(for c: DDJControl) -> Int? {
        switch c {
        case .browsePress: return indexByControl[.browse]
        case .jogTouch(let d): return indexByControl[.jog(d)]
        case .padModeButton(let d, let m): return indexByControl[.padModeButton(d, Self.baseMode(m))]
        default: return indexByControl[c]
        }
    }

    nonisolated static func baseMode(_ m: PadMode) -> PadMode {
        switch m {
        case .keyboard: return .hotCue
        case .padFX1: return .beatLoop
        case .padFX2: return .beatJump
        case .keyShift: return .sampler
        default: return m
        }
    }

    nonisolated static func makeLayout() -> [Item] {
        var out: [Item] = []
        func centered(_ x: CGFloat, _ y: CGFloat, _ w: CGFloat, _ h: CGFloat) -> CGRect {
            CGRect(x: x - w / 2, y: y - h / 2, width: w, height: h)
        }
        func circle(_ x: CGFloat, _ y: CGFloat, _ r: CGFloat) -> CGRect {
            CGRect(x: x - r, y: y - r, width: 2 * r, height: 2 * r)
        }
        func add(_ c: DDJControl, _ k: Item.Kind, _ r: CGRect, _ label: String, _ lp: Item.LabelPos, _ cp: Item.CaptionPos,
                 value vp: Item.ValuePos? = nil) {
            let v: Item.ValuePos
            if let vp { v = vp } else {
                switch (k, cp) {
                case (_, .side): v = .block
                case (.knob, _), (.browse, _): v = .below
                case (.vfader, _): v = .underTitle
                case (.hfader, _): v = .inline
                default: v = .inside
                }
            }
            out.append(Item(control: c, kind: k, rect: r, label: label, labelPos: lp, caption: cp, valuePos: v))
        }
        for deck in Deck.allCases {
            let x0: CGFloat = deck == .left ? 0 : rightDeckX
            let capY: CGFloat = 36
            // top row: loop section and BEAT SYNC
            add(.loopIn(deck), .round, circle(x0 + 44, 72, 19), "IN/4BEAT", .below, .at(CGPoint(x: x0 + 44, y: capY), width: 54), value: .inside)
            add(.loopOut(deck), .round, circle(x0 + 100, 72, 19), "OUT", .below, .at(CGPoint(x: x0 + 100, y: capY), width: 54), value: .inside)
            add(.reloop(deck), .round, circle(x0 + 154, 72, 14), "RELOOP/EXIT", .below, .at(CGPoint(x: x0 + 154, y: capY), width: 50), value: .inside)
            add(.callLeft(deck), .button, centered(x0 + 212, 72, 26, 24), "\u{25C0}", .inside, .at(CGPoint(x: x0 + 212, y: capY), width: 58))
            add(.callRight(deck), .button, centered(x0 + 274, 72, 26, 24), "\u{25B6}", .inside, .at(CGPoint(x: x0 + 274, y: capY), width: 58))
            add(.sync(deck), .round, circle(x0 + 398, 72, 19), "BEAT SYNC", .below, .at(CGPoint(x: x0 + 398, y: capY), width: 80), value: .inside)
            // jog and tempo
            add(.jog(deck), .jog, circle(x0 + 200, 258, 128), "JOG", .none, .inside, value: .inside)
            add(.tempo(deck), .vfader, CGRect(x: x0 + 395, y: 288, width: 28, height: 330), "TEMPO", .below,
                .at(CGPoint(x: x0 + 409, y: 250), width: 62))
            // transport column
            add(.shift(deck), .shiftButton, centered(x0 + 46, 452, 52, 22), "SHIFT", .inside, .above(width: 84))
            add(.cue(deck), .round, circle(x0 + 46, 530, 30), "CUE", .inside, .above(width: 86))
            add(.play(deck), .round, circle(x0 + 46, 626, 30), "\u{25B6}/II", .inside, .above(width: 86))
            // mode buttons and pads
            let padX0 = x0 + 97, padW: CGFloat = 66, gap: CGFloat = 8
            let modes: [(PadMode, PadMode)] = [(.hotCue, .keyboard), (.beatLoop, .padFX1), (.beatJump, .padFX2), (.sampler, .keyShift)]
            for (i, m) in modes.enumerated() {
                let r = CGRect(x: padX0 + CGFloat(i) * (padW + gap), y: 428, width: padW, height: 20)
                add(.padModeButton(deck, m.0), .mode(m.0, m.1), r, m.0.ddjLabel, .inside, .above(width: padW + 4))
            }
            for i in 0..<8 {
                let col = CGFloat(i % 4), rowY: CGFloat = i < 4 ? 470 : 560
                add(.pad(deck, i), .pad, CGRect(x: padX0 + col * (padW + gap), y: rowY, width: padW, height: 84), "", .none, .inside)
            }
        }

        // the mixer, as on the DDJ-400: three columns. Left: the name and HEADPHONES; middle: the two channel strips
        // (the meter between them), the channel faders and the crossfader; right: MASTER and BEAT FX. BROWSE and the
        // LOAD buttons sit above the channel strips.
        let mid = mixerMid, chL = mixerMid - 50, chR = mixerMid + 50
        add(.load(.left), .button, centered(chL - 10, 80, 46, 22), "LOAD", .inside, .at(CGPoint(x: chL - 10, y: 56), width: 70))
        add(.browse, .browse, circle(mid, 80, 18), "BROWSE", .below, .at(CGPoint(x: mid, y: 56), width: 70))
        add(.load(.right), .button, centered(chR + 10, 80, 46, 22), "LOAD", .inside, .at(CGPoint(x: chR + 10, y: 56), width: 70))

        // channel strips: a title above each knob, its value under it
        for deck in Deck.allCases {
            let left = deck == .left
            let x = left ? chL : chR
            let n = left ? "1" : "2"
            let knobs: [(DDJControl, String, CGFloat)] = [
                (.trim(deck), "TRIM", 166), (.eqHi(deck), "HI", 238), (.eqMid(deck), "MID", 310),
                (.eqLow(deck), "LOW", 382), (.cfx(deck), "FILTER", 454),
            ]
            for (c, label, y) in knobs {
                add(c, .knob, circle(x, y, 14), label, .below, .above(width: 92))
            }
            add(.headphoneCue(deck), .button, centered(x, 524, 42, 20), "CUE", .inside, .above(width: 92))
            add(.channelFader(deck), .vfader, CGRect(x: x - 14, y: 562, width: 28, height: 86), n, .none,
                .at(CGPoint(x: x, y: 549), width: 92), value: .inline)
        }
        add(.crossfader, .hfader, CGRect(x: mid - 56, y: 676, width: 112, height: 18), "CROSSFADER", .none,
            .at(CGPoint(x: mid, y: 662), width: 200))

        // left column: HEADPHONES MIXING (the LEVEL beside it is analogue: printed only)
        add(.headphoneLevel, .knob, circle(leftColMid, 360, 13), "MIXING", .below, .above(width: 58))

        // right column: BEAT FX (MASTER above it is analogue: printed only)
        let rx = rightColMid
        add(.beatLeft, .button, centered(rx - 21, 262, 36, 20), "\u{25C0}", .inside, .above(width: 44))
        add(.beatRight, .button, centered(rx + 21, 262, 36, 20), "\u{25B6}", .inside, .above(width: 44))
        add(.fxSelect, .button, centered(rx, 330, 70, 20), "FX SELECT", .inside, .above(width: 86))
        for n in 0..<3 {
            add(.fxChannel(n), .segment, CGRect(x: rx - 38, y: 358 + CGFloat(n) * 18, width: 76, height: 16),
                ["CH 1", "CH 2", "MASTER"][n], .inside, .inside, value: .inside)
        }
        add(.fxLevel, .knob, circle(rx, 452, 14), "LEVEL/DEPTH", .below, .above(width: 86))
        add(.fxOnOff, .round, circle(rx, 530, 18), "ON/OFF", .below, .above(width: 86), value: .inside)
        return out
    }

    // MARK: - State

    var values: [DDJControl: Double] = [:]
    /// controls whose position was given (hardware, setPosition, the mouse): drawn there, not at their face's arc
    var explicitPosition: Set<DDJControl> = []
    var hwPressed: Set<DDJControl> = []
    var mouseHeld: DDJControl?
    var hovered: DDJControl?
    var shiftLatched: Set<Deck> = []
    var jogAngle: [CGFloat] = [-.pi / 2, -.pi / 2]
    var jogTouched: Deck?
    var browseAngle: CGFloat = 0
    private var browseScrollAccumulator: CGFloat = 0
    private var trackingArea: NSTrackingArea?
    var accessibilityItems: [DDJAccessibilityElement]?
    /// the static parts (panels, printed labels, scales) drawn once per size and backing scale
    var staticCache: (size: CGSize, scale: CGFloat, image: CGImage)?
    var fontCache: [CGFloat: NSFont] = [:]
    var colorCache: [UInt32: NSColor] = [:]
    var advanceRatio: CGFloat = 0
    var flashStart: [DDJControl: CFTimeInterval] = [:]
    var lastTouched: [DDJControl: CFTimeInterval] = [:]
    private var flashTimer: Timer?
    /// the clock (tests replace it)
    var now: () -> CFTimeInterval = { CACurrentMediaTime() }

    private enum Drag {
        case press(DDJControl, shifted: Bool, padMode: PadMode?)
        case value(DDJControl, start: Double, from: NSPoint, horizontal: Bool, shifted: Bool)
        case jog(Deck, last: CGFloat, acc: CGFloat, shifted: Bool)
        case browse(from: NSPoint, lastY: CGFloat, acc: CGFloat, moved: Bool, shifted: Bool)
    }
    private var drag: Drag?

    /// the DDJ-400's jog: 720 ticks a turn (Mixxx's setting). Drags on the drawn jog send ticks at the same rate, so a
    /// quarter turn of the mouse is a quarter turn of the platter.
    static let hwJogRadPerTick: CGFloat = 2 * .pi / 720
    static let jogTicksPerScroll = 15                     // per scroll unit (a wheel notch: 2 units, 15 degrees)
    static let jogTicksPerStep = 45                       // an accessibility increment: 1/16 of a turn
    static let browseRadPerTick: CGFloat = .pi / 10
    static let dragRange: CGFloat = 200                   // points of drag for the full 0...1

    nonisolated static func clamp(_ v: Double) -> Double { min(1, max(0, v)) }

    /// the control's own position (hardware, mouse, setPosition), or its default
    func value(_ c: DDJControl) -> Double {
        if let v = values[c] { return v }
        if case .channelFader = c { return 0.8 }
        return 0.5
    }

    /// where a knob's pointer or a fader's cap is drawn: its given position, else its face's arc, else its default
    func position(_ c: DDJControl) -> Double {
        if !explicitPosition.contains(c), let a = faces[c]?.arc { return Self.clamp(a) }
        return value(c)
    }

    func mode(_ d: Deck) -> PadMode { padModes[d] ?? .hotCue }

    private var anyShift: Bool { !shiftLatched.isEmpty }

    private func spinJog(_ d: Deck, by a: CGFloat) {
        jogAngle[d.rawValue] = (jogAngle[d.rawValue] + a).truncatingRemainder(dividingBy: 2 * .pi)
    }

    /// a knob or fader moved from the drawing: it now sits where it was put, and its value shows big for a moment
    private func moved(_ c: DDJControl, to v: Double) {
        values[c] = v
        explicitPosition.insert(c)
        lastTouched[c] = now()
        flash(c)
    }

    // MARK: - Geometry

    var fit: (scale: CGFloat, origin: CGPoint) {
        let s = max(0.01, min(bounds.width / Self.designW, bounds.height / Self.designH))
        return (s, CGPoint(x: bounds.minX + (bounds.width - Self.designW * s) / 2,
                           y: bounds.minY + (bounds.height - Self.designH * s) / 2))
    }

    func toDesign(_ p: NSPoint) -> CGPoint {
        let f = fit
        return CGPoint(x: (p.x - f.origin.x) / f.scale, y: (p.y - f.origin.y) / f.scale)
    }

    func toView(_ p: CGPoint) -> NSPoint {
        let f = fit
        return NSPoint(x: f.origin.x + p.x * f.scale, y: f.origin.y + p.y * f.scale)
    }

    /// the clickable part of an item, in design space
    func hitRect(_ it: Item) -> CGRect {
        switch it.kind {
        case .knob, .round, .browse, .jog: return it.rect.insetBy(dx: -4, dy: -4)
        case .vfader: return it.rect.insetBy(dx: -6, dy: -8)
        case .hfader: return it.rect.insetBy(dx: -8, dy: -4)
        case .segment: return it.rect.insetBy(dx: -1, dy: -3)
        default: return it.rect.insetBy(dx: -3, dy: -3)
        }
    }

    func itemIndex(at viewPoint: NSPoint) -> Int? {
        let p = toDesign(viewPoint)
        for (i, it) in items.enumerated() {
            switch it.kind {
            case .knob, .round, .browse, .jog:
                if hypot(p.x - it.rect.midX, p.y - it.rect.midY) <= it.rect.width / 2 + 4 { return i }
            default:
                if hitRect(it).contains(p) { return i }
            }
        }
        return nil
    }

    private func item(at viewPoint: NSPoint) -> Item? { itemIndex(at: viewPoint).map { items[$0] } }

    /// The control under a point in view coordinates.
    func control(at point: NSPoint) -> DDJControl? { item(at: point)?.control }

    /// The centre of a control in view coordinates (for tests).
    func center(of control: DDJControl) -> NSPoint? {
        guard let i = itemIndex(for: control) else { return nil }
        return toView(CGPoint(x: items[i].rect.midX, y: items[i].rect.midY))
    }

    // MARK: - Mouse

    public override func mouseDown(with event: NSEvent) {
        pointerDown(at: convert(event.locationInWindow, from: nil), shiftKey: event.modifierFlags.contains(.shift))
    }

    public override func mouseDragged(with event: NSEvent) {
        pointerDragged(to: convert(event.locationInWindow, from: nil))
    }

    public override func mouseUp(with event: NSEvent) {
        pointerUp()
    }

    public override func mouseMoved(with event: NSEvent) {
        pointerMoved(to: convert(event.locationInWindow, from: nil))
    }

    public override func mouseExited(with event: NSEvent) {
        setHovered(nil)
    }

    public override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let t = trackingArea { removeTrackingArea(t) }
        let t = NSTrackingArea(rect: .zero, options: [.mouseMoved, .mouseEnteredAndExited, .activeInActiveApp, .inVisibleRect],
                               owner: self, userInfo: nil)
        addTrackingArea(t)
        trackingArea = t
    }

    public override func scrollWheel(with event: NSEvent) {
        if !scroll(at: convert(event.locationInWindow, from: nil), deltaY: event.deltaY,
                   precise: event.hasPreciseScrollingDeltas, preciseDeltaY: event.scrollingDeltaY) {
            super.scrollWheel(with: event)
        }
    }

    private func emit(_ e: DDJEvent) { onEvent?(e) }

    func pointerMoved(to p: NSPoint) { setHovered(control(at: p)) }

    private func setHovered(_ c: DDJControl?) {
        guard c != hovered else { return }
        let old = hovered
        hovered = c
        invalidate(Set([old, c].compactMap { $0 }))
        onHover?(c)
    }

    func pointerDown(at p: NSPoint, shiftKey: Bool = false) {
        if drag != nil { pointerUp() }
        guard let it = item(at: p) else { return }
        let shifted = shiftKey || anyShift
        let c = it.control
        switch it.kind {
        case .shiftButton:
            guard case .shift(let deck) = c else { return }
            if shiftLatched.contains(deck) {
                shiftLatched.remove(deck)
                emit(DDJEvent(c, .press(false)))
            } else {
                shiftLatched.insert(deck)
                emit(DDJEvent(c, .press(true)))
            }
        case .button, .round, .segment:
            mouseHeld = c
            drag = .press(c, shifted: shifted, padMode: nil)
            emit(DDJEvent(c, .press(true), shifted: shifted))
        case .pad:
            guard case .pad(let deck, _) = c else { return }
            let m = mode(deck)
            mouseHeld = c
            drag = .press(c, shifted: shifted, padMode: m)
            emit(DDJEvent(c, .press(true), padMode: m, shifted: shifted))
        case .mode(let base, let alt):
            guard case .padModeButton(let deck, _) = c else { return }
            let m = shifted ? alt : base
            let ctl = DDJControl.padModeButton(deck, m)
            mouseHeld = ctl
            drag = .press(ctl, shifted: shifted, padMode: m)
            padModes[deck] = m
            emit(DDJEvent(ctl, .press(true), padMode: m, shifted: shifted))
        case .knob, .vfader:
            drag = .value(c, start: position(c), from: p, horizontal: false, shifted: shifted)
            mouseHeld = c
        case .hfader:
            drag = .value(c, start: position(c), from: p, horizontal: true, shifted: shifted)
            mouseHeld = c
        case .jog:
            guard case .jog(let deck) = c else { return }
            let d = toDesign(p)
            drag = .jog(deck, last: atan2(d.y - it.rect.midY, d.x - it.rect.midX), acc: 0, shifted: shifted)
            jogTouched = deck
        case .browse:
            drag = .browse(from: p, lastY: p.y, acc: 0, moved: false, shifted: shifted)
            mouseHeld = .browse
        }
        invalidate([c])
    }

    func pointerDragged(to p: NSPoint) {
        guard let dr = drag else { return }
        switch dr {
        case .press:
            break
        case .value(let c, let start, let from, let horizontal, let shifted):
            var d = horizontal ? (p.x - from.x) : (from.y - p.y)
            if case .tempo = c { d = -d }                // (its value grows downwards, as on the hardware)
            let v = Self.clamp(start + Double(d / Self.dragRange))
            if v != position(c) || !explicitPosition.contains(c) {
                moved(c, to: v)
                emit(DDJEvent(c, .absolute(v), shifted: shifted))
                invalidate([c])
            }
        case .jog(let deck, let last, var acc, let shifted):
            guard let i = itemIndex(for: .jog(deck)) else { return }
            let r = items[i].rect
            let q = toDesign(p)
            let a = atan2(q.y - r.midY, q.x - r.midX)
            var delta = a - last
            if delta > .pi { delta -= 2 * .pi }
            if delta < -.pi { delta += 2 * .pi }
            spinJog(deck, by: delta)
            acc += delta
            let ticks = Int(acc / Self.hwJogRadPerTick)
            if ticks != 0 {
                acc -= CGFloat(ticks) * Self.hwJogRadPerTick
                lastTouched[.jog(deck)] = now()
                emit(DDJEvent(.jog(deck), .relative(ticks), shifted: shifted))
            }
            drag = .jog(deck, last: a, acc: acc, shifted: shifted)
            invalidate([.jog(deck)])
        case .browse(let from, let lastY, var acc, var moved, let shifted):
            if hypot(p.x - from.x, p.y - from.y) > 3 { moved = true }
            acc += lastY - p.y
            let step: CGFloat = 12
            while abs(acc) >= step {
                let n = acc > 0 ? 1 : -1
                acc -= CGFloat(n) * step
                browseAngle += CGFloat(n) * Self.browseRadPerTick
                lastTouched[.browse] = now()
                emit(DDJEvent(.browse, .relative(n), shifted: shifted))
                invalidate([.browse])
            }
            drag = .browse(from: from, lastY: p.y, acc: acc, moved: moved, shifted: shifted)
        }
    }

    /// Release whatever the mouse holds (the app also calls this when the window loses focus).
    func pointerUp() {
        guard let dr = drag else { return }
        drag = nil
        if let h = mouseHeld { invalidate([h]) }
        mouseHeld = nil
        switch dr {
        case .press(let c, let shifted, let m):
            emit(DDJEvent(c, .press(false), padMode: m, shifted: shifted))
        case .browse(_, _, _, let moved, let shifted):
            if !moved {
                emit(DDJEvent(.browsePress, .press(true), shifted: shifted))
                emit(DDJEvent(.browsePress, .press(false), shifted: shifted))
            }
        case .jog(let d, _, _, _):
            jogTouched = nil
            invalidate([.jog(d)])
        case .value:
            break
        }
    }

    /// Scroll over a jog, BROWSE, a knob or a fader. Returns false when nothing under the pointer takes scrolls.
    @discardableResult
    func scroll(at p: NSPoint, deltaY: CGFloat, precise: Bool = false, preciseDeltaY: CGFloat = 0) -> Bool {
        guard deltaY != 0 || preciseDeltaY != 0, let it = item(at: p) else { return false }
        let shifted = anyShift
        switch it.kind {
        case .jog:
            guard case .jog(let deck) = it.control, deltaY != 0 else { return true }
            var units = Int(deltaY * 2)
            if units == 0 { units = deltaY > 0 ? 1 : -1 }
            let ticks = units * Self.jogTicksPerScroll
            spinJog(deck, by: CGFloat(ticks) * Self.hwJogRadPerTick)
            lastTouched[it.control] = now()
            emit(DDJEvent(it.control, .relative(ticks), shifted: shifted))
        case .browse:
            var steps: [Int] = []
            if precise {
                browseScrollAccumulator += preciseDeltaY
                while abs(browseScrollAccumulator) >= 12 {
                    let n = browseScrollAccumulator > 0 ? 1 : -1
                    browseScrollAccumulator -= CGFloat(n) * 12
                    steps.append(n)
                }
            } else if deltaY != 0 {
                steps.append(deltaY > 0 ? 1 : -1)
            }
            for n in steps {
                browseAngle += CGFloat(n) * Self.browseRadPerTick
                lastTouched[.browse] = now()
                emit(DDJEvent(.browse, .relative(n), shifted: shifted))
            }
        case .knob, .vfader, .hfader:
            let d = precise ? preciseDeltaY / Self.dragRange : deltaY * 0.02
            let dir: Double = { if case .tempo = it.control { return -1 }; return 1 }()   // (scroll up: the cap goes up)
            let v = Self.clamp(position(it.control) + dir * Double(d))
            if v != position(it.control) {
                moved(it.control, to: v)
                emit(DDJEvent(it.control, .absolute(v), shifted: shifted))
            }
        default:
            return false
        }
        invalidate([it.control])
        return true
    }

    /// One step of a knob, fader or endless control, as from the keyboard or VoiceOver (+1 up / clockwise).
    func step(_ c: DDJControl, by dir: Int) -> Bool {
        guard let i = itemIndex(for: c) else { return false }
        let it = items[i]
        switch it.kind {
        case .knob, .vfader, .hfader:
            let v = Self.clamp(position(it.control) + 0.05 * Double(dir))
            guard v != position(it.control) else { return true }
            moved(it.control, to: v)
            emit(DDJEvent(it.control, .absolute(v), shifted: anyShift))
        case .jog:
            guard case .jog(let d) = it.control else { return false }
            spinJog(d, by: CGFloat(dir * Self.jogTicksPerStep) * Self.hwJogRadPerTick)
            lastTouched[it.control] = now()
            emit(DDJEvent(it.control, .relative(dir * Self.jogTicksPerStep), shifted: anyShift))
        case .browse:
            browseAngle += CGFloat(dir) * Self.browseRadPerTick
            lastTouched[.browse] = now()
            emit(DDJEvent(.browse, .relative(dir), shifted: anyShift))
        default:
            return false
        }
        invalidate([it.control])
        return true
    }

    /// Press and release a button or pad, as from VoiceOver (SHIFT toggles its latch).
    func press(_ c: DDJControl) -> Bool {
        guard let i = itemIndex(for: c) else { return false }
        switch items[i].kind {
        case .button, .shiftButton, .round, .pad, .mode, .browse, .segment:
            let p = toView(CGPoint(x: items[i].rect.midX, y: items[i].rect.midY))
            pointerDown(at: p)
            pointerUp()
            return true
        default:
            return false
        }
    }

    // MARK: - Tooltips

    private func refreshToolTips() {
        removeAllToolTips()
        guard helpText != nil else { return }
        for it in items { addToolTip(viewRect(hitRect(it)), owner: self, userData: nil) }
    }
}

extension DDJ400View: NSViewToolTipOwner {
    public func view(_ view: NSView, stringForToolTip tag: NSView.ToolTipTag, point: NSPoint,
                     userData data: UnsafeMutableRawPointer?) -> String {
        guard let c = control(at: point), let s = helpText?(c) else { return "" }
        return s
    }
}
