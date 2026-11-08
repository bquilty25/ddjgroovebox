// SPDX-License-Identifier: GPL-3.0-only
// SLOOP's look, as its web editor (vendor/sloop/web/editor.html) and the FM-1 screen draw it: black, a few greys,
// the four track colours, red for REC, and Terminus Bold ("SLOOP Terminus", SIL OFL) in capitals.
import AppKit
import CoreText

enum SloopStyle {
    static func rgb(_ hex: UInt32) -> NSColor {
        NSColor(srgbRed: CGFloat((hex >> 16) & 0xFF) / 255, green: CGFloat((hex >> 8) & 0xFF) / 255,
                blue: CGFloat(hex & 0xFF) / 255, alpha: 1)
    }

    // editor.html :root
    static let bg = rgb(0x000000)
    static let s1 = rgb(0x0B0B0E)      // panels
    static let s2 = rgb(0x1A1A1E)      // controls
    static let s3 = rgb(0x36363C)      // raised / borders
    static let line = rgb(0x232329)
    static let dim = rgb(0x76767E)     // secondary labels
    static let fg2 = rgb(0xC4C4CC)
    static let fg = rgb(0xF2F2F2)
    static let red = rgb(0xFF2C34)     // REC
    /// the four tracks: 1 blue, 2 green, 3 yellow, 4 (drums) orange
    static let track: [NSColor] = [rgb(0x287CFF), rgb(0x1ECC70), rgb(0xFFC618), rgb(0xFF621A)]

    static let fontName = "SLOOP Terminus"
    private static let registered: Bool = {
        // inside the .app (scripts/build_app.sh copies it to Contents/Resources), else SwiftPM's resource bundle
        let url = Bundle.main.url(forResource: "SLOOPTerminus", withExtension: "ttf")
            ?? Bundle.module.url(forResource: "SLOOPTerminus", withExtension: "ttf")
        guard let url else { return false }
        return CTFontManagerRegisterFontsForURL(url as CFURL, .process, nil) || NSFont(name: fontName, size: 12) != nil
    }()

    /// the pixel font at a size (16 pt is its native pixel size; multiples stay crisp). Falls back to a monospaced font.
    static func font(_ size: CGFloat) -> NSFont {
        _ = registered
        return NSFont(name: fontName, size: size) ?? .monospacedSystemFont(ofSize: size, weight: .bold)
    }
}
