// SPDX-License-Identifier: GPL-3.0-only
// VoiceOver for the on-screen DDJ-400: every control is an accessibility element (buttons and pads: press; knobs and
// faders: sliders with a value, increment and decrement; jogs and BROWSE: incrementors). Label: the live caption and
// the DDJ's name for the control; help: the app's help text.

import AppKit
import DDJMapping

final class DDJAccessibilityElement: NSAccessibilityElement {
    let control: DDJControl
    weak var owner: DDJ400View?

    init(control: DDJControl, owner: DDJ400View) {
        self.control = control
        self.owner = owner
        super.init()
    }

    // AppKit calls these on the main thread; NSAccessibilityElement itself is not main-actor isolated
    override func accessibilityLabel() -> String? { MainActor.assumeIsolated { owner?.accessibilityLabel(for: control) } }
    override func accessibilityHelp() -> String? { MainActor.assumeIsolated { owner?.helpText?(control) } }
    override func accessibilityValue() -> Any? { MainActor.assumeIsolated { owner?.accessibilityValue(for: control) } }
    override func accessibilityPerformPress() -> Bool { MainActor.assumeIsolated { owner?.press(control) ?? false } }
    override func accessibilityPerformIncrement() -> Bool { MainActor.assumeIsolated { owner?.step(control, by: 1) ?? false } }
    override func accessibilityPerformDecrement() -> Bool { MainActor.assumeIsolated { owner?.step(control, by: -1) ?? false } }
}

extension DDJ400View {

    public override func isAccessibilityElement() -> Bool { true }

    public override func accessibilityChildren() -> [Any]? {
        ensureAccessibilityItems()
        return accessibilityItems
    }

    public override func accessibilityHitTest(_ point: NSPoint) -> Any? {
        guard let w = window else { return self }
        let p = convert(w.convertPoint(fromScreen: point), from: nil)
        guard let i = itemIndex(at: p) else { return self }
        ensureAccessibilityItems()
        return accessibilityItems?[i]
    }

    private func ensureAccessibilityItems() {
        guard accessibilityItems == nil else { return }
        accessibilityItems = items.map { it in
            let el = DDJAccessibilityElement(control: it.control, owner: self)
            el.setAccessibilityParent(self)
            switch it.kind {
            case .knob, .vfader, .hfader: el.setAccessibilityRole(.slider)
            case .jog, .browse: el.setAccessibilityRole(.incrementor)
            default: el.setAccessibilityRole(.button)
            }
            return el
        }
        updateAccessibilityFrames()
    }

    func updateAccessibilityFrames() {
        guard let els = accessibilityItems else { return }
        for (i, el) in els.enumerated() { el.setAccessibilityFrameInParentSpace(viewRect(hitRect(items[i]))) }
    }

    /// the control's job (its face's title, or its caption) and the DDJ's name for it
    func accessibilityLabel(for c: DDJControl) -> String {
        let lk = look(c)
        let job = lk.title.flatMap { $0.isEmpty ? nil : $0 }
        return [job, Self.ddjName(c)].compactMap { $0 }.joined(separator: ", ")
    }

    /// the face's value when there is one; else a knob's or fader's position, or a button's light
    func accessibilityValue(for c: DDJControl) -> String? {
        guard let i = itemIndex(for: c) else { return nil }
        let lk = look(c)
        var parts: [String] = []
        if let v = lk.value { parts.append(v) }
        if let b = lk.badge { parts.append(b) }
        switch items[i].kind {
        case .knob, .vfader, .hfader:
            if parts.isEmpty { parts.append("\(Int((position(c) * 100).rounded())) percent") }
        case .jog:
            if let tracks = deckTracks[items[i].control.deck ?? .left], let t = tracks.first(where: { $0.active }) {
                parts.append("active track " + t.name)
            }
        case .browse:
            break
        default:
            switch lk.light {
            case .on: parts.append("lit")
            case .dim: parts.append("glowing")
            case .off: break
            }
            if lk.outlined { parts.append("playhead") }
        }
        return parts.isEmpty ? nil : parts.joined(separator: ", ")
    }

    /// the DDJ-400's name for a control, as printed on it
    nonisolated static func ddjName(_ c: DDJControl) -> String {
        func deck(_ d: Deck) -> String { d == .left ? "deck 1" : "deck 2" }
        func ch(_ d: Deck) -> String { d == .left ? "channel 1" : "channel 2" }
        switch c {
        case .play(let d): return "PLAY/PAUSE, " + deck(d)
        case .cue(let d): return "CUE, " + deck(d)
        case .sync(let d): return "BEAT SYNC, " + deck(d)
        case .shift(let d): return "SHIFT, " + deck(d)
        case .loopIn(let d): return "LOOP IN, " + deck(d)
        case .loopOut(let d): return "LOOP OUT, " + deck(d)
        case .reloop(let d): return "RELOOP/EXIT, " + deck(d)
        case .callLeft(let d): return "CUE/LOOP CALL left, " + deck(d)
        case .callRight(let d): return "CUE/LOOP CALL right, " + deck(d)
        case .pad(let d, let i): return "pad \(i + 1), " + deck(d)
        case .padModeButton(let d, let m): return m.ddjLabel + ", " + deck(d)
        case .jog(let d), .jogTouch(let d): return "jog wheel, " + deck(d)
        case .tempo(let d): return "TEMPO slider, " + deck(d)
        case .channelFader(let d): return "channel fader, " + ch(d)
        case .trim(let d): return "TRIM, " + ch(d)
        case .eqHi(let d): return "EQ HI, " + ch(d)
        case .eqMid(let d): return "EQ MID, " + ch(d)
        case .eqLow(let d): return "EQ LOW, " + ch(d)
        case .cfx(let d): return "FILTER, " + ch(d)
        case .headphoneCue(let d): return "headphone CUE, " + ch(d)
        case .load(let d): return "LOAD, " + deck(d)
        case .crossfader: return "crossfader"
        case .browse, .browsePress: return "BROWSE"
        case .beatLeft: return "BEAT left"
        case .beatRight: return "BEAT right"
        case .fxSelect: return "BEAT FX SELECT"
        case .fxOnOff: return "BEAT FX ON/OFF"
        case .fxLevel: return "BEAT FX LEVEL/DEPTH"
        case .headphoneLevel: return "headphones LEVEL"
        case .fxChannel(let n): return "BEAT FX channel switch, " + ["1", "2", "MASTER"][min(2, max(0, n))]
        }
    }
}

extension DDJControl {
    /// the deck a deck control belongs to
    var deck: Deck? {
        switch self {
        case .play(let d), .cue(let d), .sync(let d), .shift(let d), .loopIn(let d), .loopOut(let d), .reloop(let d),
             .callLeft(let d), .callRight(let d), .pad(let d, _), .padModeButton(let d, _), .jog(let d), .jogTouch(let d),
             .tempo(let d), .channelFader(let d), .trim(let d), .eqHi(let d), .eqMid(let d), .eqLow(let d), .cfx(let d),
             .headphoneCue(let d), .load(let d):
            return d
        default:
            return nil
        }
    }
}
