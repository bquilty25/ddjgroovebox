// SPDX-License-Identifier: GPL-3.0-only
// Draws the app icon: the jog's ring of eight track colours around a dark platter, the jog's white marker, and four
// lit steps in the middle. Usage: swift scripts/make_icon.swift <out.iconset dir>  (then: iconutil -c icns <dir>)
import AppKit

let track: [UInt32] = [0xFFFF4A3D, 0xFFFF9F1C, 0xFFFFD60A, 0xFF7CDB3C, 0xFF2EC4F2, 0xFF4D7CFF, 0xFFA66BFF, 0xFFFF5FB4]
func color(_ v: UInt32) -> CGColor {
    CGColor(srgbRed: CGFloat((v >> 16) & 0xFF) / 255, green: CGFloat((v >> 8) & 0xFF) / 255, blue: CGFloat(v & 0xFF) / 255, alpha: 1)
}

func draw(size: Int) -> CGImage? {
    let s = CGFloat(size)
    guard let g = CGContext(data: nil, width: size, height: size, bitsPerComponent: 8, bytesPerRow: 0,
                            space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)
    else { return nil }
    // the tile: macOS icon grid (824 of 1024, centred), rounded, near-black with a faint top light
    let inset = s * 100 / 1024
    let tile = CGRect(x: inset, y: inset, width: s - 2 * inset, height: s - 2 * inset)
    let tilePath = CGPath(roundedRect: tile, cornerWidth: tile.width * 0.225, cornerHeight: tile.width * 0.225, transform: nil)
    g.setShadow(offset: CGSize(width: 0, height: -s * 0.01), blur: s * 0.03, color: CGColor(gray: 0, alpha: 0.5))
    g.addPath(tilePath); g.setFillColor(CGColor(srgbRed: 0.043, green: 0.043, blue: 0.055, alpha: 1)); g.fillPath()
    g.setShadow(offset: .zero, blur: 0, color: nil)
    g.saveGState()
    g.addPath(tilePath); g.clip()
    let grad = CGGradient(colorsSpace: CGColorSpace(name: CGColorSpace.sRGB)!,
                          colors: [CGColor(gray: 1, alpha: 0.07), CGColor(gray: 1, alpha: 0)] as CFArray, locations: [0, 1])!
    g.drawLinearGradient(grad, start: CGPoint(x: 0, y: tile.maxY), end: CGPoint(x: 0, y: tile.midY), options: [])
    g.restoreGState()

    let c = CGPoint(x: tile.midX, y: tile.midY)
    let R = tile.width * 0.38
    // the grip ring
    g.setStrokeColor(CGColor(gray: 1, alpha: 0.10)); g.setLineWidth(max(1, s * 0.006))
    for i in 0..<72 {
        let a = CGFloat(i) * .pi / 36
        g.move(to: CGPoint(x: c.x + cos(a) * R * 1.06, y: c.y + sin(a) * R * 1.06))
        g.addLine(to: CGPoint(x: c.x + cos(a) * R * 1.13, y: c.y + sin(a) * R * 1.13))
    }
    g.strokePath()
    // the ring: eight arcs in the track colours, track 1 at the top left, clockwise (as on the jogs)
    let band = R * 0.22, gap: CGFloat = 0.06
    g.setLineCap(.butt)
    g.setLineWidth(band)
    for i in 0..<8 {
        let mid = 3 * CGFloat.pi / 4 - CGFloat(i) * .pi / 4      // (y up here: clockwise is decreasing angle)
        g.setStrokeColor(color(track[i]))
        g.addArc(center: c, radius: R - band / 2, startAngle: mid + .pi / 8 - gap, endAngle: mid - .pi / 8 + gap, clockwise: true)
        g.strokePath()
    }
    // the platter
    let pr = R * 0.70
    g.setFillColor(CGColor(srgbRed: 0.07, green: 0.07, blue: 0.085, alpha: 1))
    g.fillEllipse(in: CGRect(x: c.x - pr, y: c.y - pr, width: 2 * pr, height: 2 * pr))
    g.setStrokeColor(CGColor(gray: 1, alpha: 0.12)); g.setLineWidth(max(1, s * 0.004))
    g.strokeEllipse(in: CGRect(x: c.x - pr, y: c.y - pr, width: 2 * pr, height: 2 * pr))
    // the jog's marker: a white notch on the ring, on track 1
    let ma = 3 * CGFloat.pi / 4
    g.setStrokeColor(CGColor(gray: 1, alpha: 1)); g.setLineWidth(s * 0.022); g.setLineCap(.round)
    g.move(to: CGPoint(x: c.x + cos(ma) * R * 1.02, y: c.y + sin(ma) * R * 1.02))
    g.addLine(to: CGPoint(x: c.x + cos(ma) * R * 1.16, y: c.y + sin(ma) * R * 1.16))
    g.strokePath()
    // four steps in the middle: a beat, lit in the colours of the tracks that play it
    let cell = pr * 0.42, sp = cell * 0.18
    let lit: [(Int, Int, UInt32?)] = [(0, 0, track[0]), (1, 0, nil), (0, 1, nil), (1, 1, track[4])]
    for (x, y, col) in lit {
        let r = CGRect(x: c.x - cell - sp / 2 + CGFloat(x) * (cell + sp), y: c.y - cell - sp / 2 + CGFloat(1 - y) * (cell + sp),
                       width: cell, height: cell)
        let p = CGPath(roundedRect: r, cornerWidth: cell * 0.18, cornerHeight: cell * 0.18, transform: nil)
        g.addPath(p)
        g.setFillColor(col.map(color) ?? CGColor(gray: 1, alpha: 0.14))
        g.fillPath()
    }
    return g.makeImage()
}

let out = URL(fileURLWithPath: CommandLine.arguments.dropFirst().first ?? "AppIcon.iconset")
try? FileManager.default.createDirectory(at: out, withIntermediateDirectories: true)
for (pt, scales) in [(16, [1, 2]), (32, [1, 2]), (128, [1, 2]), (256, [1, 2]), (512, [1, 2])] {
    for sc in scales {
        guard let img = draw(size: pt * sc) else { exit(1) }
        let name = "icon_\(pt)x\(pt)" + (sc == 2 ? "@2x" : "") + ".png"
        let rep = NSBitmapImageRep(cgImage: img)
        try? rep.representation(using: .png, properties: [:])?.write(to: out.appendingPathComponent(name))
    }
}
print("wrote \(out.path)")
