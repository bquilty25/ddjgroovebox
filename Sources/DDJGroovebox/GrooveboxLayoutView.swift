// SPDX-License-Identifier: GPL-3.0-only
// The window's content: the instrument's strip directly on top of the drawn DDJ-400, the same width, one device (the
// drawing's body is open at the top). Also the computer keyboard as a controller, so everything can be played with
// no DDJ-400 attached (see KeyboardMap).
import AppKit
import DDJMapping

@MainActor
final class GrooveboxLayoutView: NSView {
    private(set) var strip: TopStrip
    let ddj: DDJ400View
    static let ddjAspect: CGFloat = 1240.0 / 700.0
    private let margin: CGFloat = 12
    /// keys pressed on the Mac keyboard, as DDJ events
    var onKeyEvent: ((DDJEvent) -> Void)?
    private var keysDown: [UInt16: DDJEvent] = [:]
    private var shiftDown = false

    init(strip: TopStrip, ddj: DDJ400View) {
        self.strip = strip
        self.ddj = ddj
        super.init(frame: .zero)
        addSubview(strip)
        addSubview(ddj)
        Self.use8Bit(self)
        Self.use8Bit(strip)
        Self.use8Bit(ddj)
    }

    /// 8-bit sRGB backing stores: on an HDR-capable display AppKit gave these views 16-bit float surfaces, and
    /// compositing the drawing into them (in parallel worker threads) was most of the app's CPU
    static func use8Bit(_ v: NSView) {
        v.wantsLayer = true
        v.layer?.contentsFormat = .RGBA8Uint
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    func setStrip(_ s: TopStrip) {
        strip.removeFromSuperview()
        strip = s
        addSubview(s)
        Self.use8Bit(s)
        needsLayout = true
    }

    override var isFlipped: Bool { true }

    override func layout() {
        super.layout()
        let backing = window?.backingScaleFactor ?? 2
        let availW = bounds.width - 2 * margin, availH = bounds.height - 2 * margin
        // the widest layout that fits: the drawing keeps its shape, the strip asks for its height
        var w = availW
        while w > 200 && strip.preferredHeight(forWidth: w, backing: backing) + w / Self.ddjAspect > availH { w -= 4 }
        let sh = strip.preferredHeight(forWidth: w, backing: backing).rounded()
        let dh = (w / Self.ddjAspect).rounded(.down)
        let x = ((bounds.width - w) / 2).rounded()
        let y = max(margin, ((bounds.height - sh - dh) / 2).rounded())
        strip.frame = CGRect(x: x, y: y, width: w, height: sh)
        ddj.frame = CGRect(x: x, y: y + sh, width: w, height: dh)
    }

    // MARK: the computer keyboard

    override var acceptsFirstResponder: Bool { true }

    override func keyDown(with event: NSEvent) {
        guard !event.isARepeat, !event.modifierFlags.contains(.command) else { return super.keyDown(with: event) }
        guard let e = KeyboardMap.event(keyCode: event.keyCode, shifted: shiftDown, padModes: ddj.padModes) else {
            return super.keyDown(with: event)
        }
        keysDown[event.keyCode] = e
        onKeyEvent?(e)
    }

    override func keyUp(with event: NSEvent) {
        guard let e = keysDown.removeValue(forKey: event.keyCode) else { return super.keyUp(with: event) }
        if case .press = e.value { onKeyEvent?(DDJEvent(e.control, .press(false), padMode: e.padMode, shifted: e.shifted)) }
    }

    override func flagsChanged(with event: NSEvent) {
        let s = event.modifierFlags.contains(.shift)
        guard s != shiftDown else { return }
        shiftDown = s
        onKeyEvent?(DDJEvent(.shift(.left), .press(s)))
    }

    /// let every key go (the window lost focus)
    func releaseKeys() {
        for (_, e) in keysDown { if case .press = e.value { onKeyEvent?(DDJEvent(e.control, .press(false), padMode: e.padMode, shifted: e.shifted)) } }
        keysDown = [:]
        if shiftDown { shiftDown = false; onKeyEvent?(DDJEvent(.shift(.left), .press(false))) }
    }
}

/// The Mac keyboard laid out like the DDJ-400 (US key positions; works with any layout by position):
///   1-8 and Q-I       the left and right deck's 8 pads (in each deck's pad mode)
///   A S D F / J K L ; LOOP IN, LOOP OUT, RELOOP, BEAT SYNC of the left / right deck
///   Z X / N M         CUE/LOOP CALL left, right of the left / right deck
///   C / V             CUE left / right       space: PLAY        , .: LOAD left / right
///   ← → / return      BROWSE turn / press    tab: BEAT FX ON/OFF   G H: headphone CUE 1 / 2
///   F1-F4 / F5-F8     the left / right deck's pad mode (HOT CUE, BEAT LOOP, BEAT JUMP, SAMPLER)
///   shift             SHIFT
enum KeyboardMap {
    static let pads: [UInt16: (Deck, Int)] = [
        18: (.left, 0), 19: (.left, 1), 20: (.left, 2), 21: (.left, 3), 23: (.left, 4), 22: (.left, 5), 26: (.left, 6), 28: (.left, 7),
        12: (.right, 0), 13: (.right, 1), 14: (.right, 2), 15: (.right, 3), 17: (.right, 4), 16: (.right, 5), 32: (.right, 6), 34: (.right, 7),
    ]
    static let buttons: [UInt16: DDJControl] = [
        0: .loopIn(.left), 1: .loopOut(.left), 2: .reloop(.left), 3: .sync(.left),
        38: .loopIn(.right), 40: .loopOut(.right), 37: .reloop(.right), 41: .sync(.right),
        6: .callLeft(.left), 7: .callRight(.left), 45: .callLeft(.right), 46: .callRight(.right),
        8: .cue(.left), 9: .cue(.right), 49: .play(.left), 43: .load(.left), 47: .load(.right),
        36: .browsePress, 48: .fxOnOff, 5: .headphoneCue(.left), 4: .headphoneCue(.right),
    ]
    static let modes: [UInt16: (Deck, PadMode)] = [
        122: (.left, .hotCue), 120: (.left, .beatLoop), 99: (.left, .beatJump), 118: (.left, .sampler),
        96: (.right, .hotCue), 97: (.right, .beatLoop), 98: (.right, .beatJump), 100: (.right, .sampler),
    ]

    static func event(keyCode k: UInt16, shifted: Bool, padModes: [Deck: PadMode]) -> DDJEvent? {
        if let (d, i) = pads[k] { return DDJEvent(.pad(d, i), .press(true), padMode: padModes[d] ?? .hotCue, shifted: shifted) }
        if let c = buttons[k] { return DDJEvent(c, .press(true), shifted: shifted) }
        if let (d, m) = modes[k] { return DDJEvent(.padModeButton(d, m), .press(true), shifted: shifted) }
        if k == 123 { return DDJEvent(.browse, .relative(-1)) }      // ←
        if k == 124 { return DDJEvent(.browse, .relative(1)) }       // →
        return nil
    }
}
