// SPDX-License-Identifier: GPL-3.0-only
// The FAQ (docs/FAQ.md, bundled as a resource) in a window, in SLOOP's style: Terminus, black, the track colours.
// A small Markdown reader: "#" title, "##" sections, paragraphs, and inline bold, code and links (web links open in the
// browser; links to files of the source tree are shown as text).
import AppKit

@MainActor
final class FAQWindowController: NSWindowController {
    static let shared = FAQWindowController(resource: "FAQ", title: "DDJ Groovebox FAQ")
    static let guide = FAQWindowController(resource: "TRIGDECK", title: "TRIGDECK Guide")
    let resource: String

    private init(resource: String, title: String) {
        self.resource = resource
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 820, height: 860),
                              styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: true)
        window.title = title
        window.setFrameAutosaveName("DDJGroovebox." + resource)
        window.appearance = NSAppearance(named: .darkAqua)
        window.backgroundColor = SloopStyle.bg
        window.minSize = NSSize(width: 420, height: 300)
        super.init(window: window)

        let scroll = NSScrollView()
        scroll.hasVerticalScroller = true
        scroll.drawsBackground = true
        scroll.backgroundColor = SloopStyle.bg
        let text = NSTextView()
        text.isEditable = false
        text.isSelectable = true
        text.drawsBackground = true
        text.backgroundColor = SloopStyle.bg
        text.textContainerInset = NSSize(width: 28, height: 24)
        text.isVerticallyResizable = true
        text.autoresizingMask = [.width]
        text.textContainer?.widthTracksTextView = true
        text.linkTextAttributes = [.foregroundColor: SloopStyle.track[0], .underlineStyle: NSUnderlineStyle.single.rawValue,
                                   .cursor: NSCursor.pointingHand]
        text.textStorage?.setAttributedString(Self.render(Self.loadMarkdown(resource)))
        scroll.documentView = text
        window.contentView = scroll
        window.center()
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    func show() {
        showWindow(nil)
        window?.makeKeyAndOrderFront(nil)
    }

    static func loadMarkdown(_ name: String = "FAQ") -> String {
        let url = Bundle.main.url(forResource: name, withExtension: "md")
            ?? Bundle.module.url(forResource: name, withExtension: "md")
        return url.flatMap { try? String(contentsOf: $0, encoding: .utf8) } ?? "# \(name)\n\n\(name) could not be found in the app."
    }

    // ---- Markdown, SLOOP style

    static func render(_ md: String) -> NSAttributedString {
        let out = NSMutableAttributedString()
        var section = 0
        var paragraph: [String] = []

        func flush() {
            guard !paragraph.isEmpty else { return }
            var body = paragraph.joined(separator: " ")
            // a question (a paragraph opening in bold): on its own line, in white
            if body.hasPrefix("**"), let end = body.range(of: "**", range: body.index(body.startIndex, offsetBy: 2)..<body.endIndex) {
                let q = String(body[body.index(body.startIndex, offsetBy: 2)..<end.lowerBound])
                out.append(NSAttributedString(string: q + "\n", attributes: [.font: SloopStyle.font(16), .foregroundColor: SloopStyle.fg,
                                                                             .paragraphStyle: spacing(before: 0, after: 4)]))
                body = String(body[end.upperBound...]).trimmingCharacters(in: .whitespaces)
            }
            out.append(inline(body, base: SloopStyle.fg2))
            out.append(NSAttributedString(string: "\n\n", attributes: [.font: SloopStyle.font(16)]))
            paragraph = []
        }

        var inTable = false
        for raw in md.components(separatedBy: "\n") {
            let line = raw.trimmingCharacters(in: .whitespaces)
            if !line.hasPrefix("|") {
                if inTable { out.append(NSAttributedString(string: "\n", attributes: [.font: SloopStyle.font(16)])) }
                inTable = false
            }
            if line.hasPrefix("## ") {
                flush()
                let colour = SloopStyle.track[section % 4]
                section += 1
                out.append(NSAttributedString(string: String(line.dropFirst(3)).uppercased() + "\n",
                                              attributes: [.font: SloopStyle.font(32), .foregroundColor: colour,
                                                           .paragraphStyle: spacing(before: 18, after: 10)]))
            } else if line.hasPrefix("# ") {
                flush()
                let title = NSMutableAttributedString(string: String(line.dropFirst(2)).uppercased() + "\n",
                                                      attributes: [.font: SloopStyle.font(32), .foregroundColor: SloopStyle.fg])
                title.addAttribute(.paragraphStyle, value: spacing(before: 0, after: 8), range: NSRange(location: 0, length: title.length))
                out.append(title)
            } else if line.hasPrefix("|") {
                // a table row: the first cell in white, the rest after it; the header row dim; the rule row skipped
                flush()
                let cells = line.split(separator: "|").map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }
                guard !cells.allSatisfy({ $0.allSatisfy { $0 == "-" || $0 == ":" } }) else { continue }
                let header = !inTable
                inTable = true
                let first = inline(cells.first ?? "", base: header ? SloopStyle.dim : SloopStyle.fg)
                let row = NSMutableAttributedString(attributedString: first)
                if cells.count > 1 {
                    row.append(NSAttributedString(string: "   ", attributes: [.font: SloopStyle.font(16)]))
                    row.append(inline(cells.dropFirst().joined(separator: "  ·  "), base: header ? SloopStyle.dim : SloopStyle.fg2))
                }
                row.append(NSAttributedString(string: "\n", attributes: [.font: SloopStyle.font(16)]))
                row.addAttribute(.paragraphStyle, value: spacing(before: 0, after: 6), range: NSRange(location: 0, length: row.length))
                out.append(row)
                continue
            } else if (line.first?.isNumber == true && line.contains(". ")) || line.hasPrefix("- ") {
                flush()                                  // a numbered step or a bullet: its own paragraph
                paragraph.append(line.hasPrefix("- ") ? "• " + line.dropFirst(2) : line)
            } else if line.isEmpty {
                flush()
            } else {
                paragraph.append(line)
            }
        }
        flush()
        return out
    }

    private static func spacing(before: CGFloat, after: CGFloat) -> NSParagraphStyle {
        let p = NSMutableParagraphStyle()
        p.paragraphSpacingBefore = before
        p.paragraphSpacing = after
        return p
    }

    /// bold (the questions) in white, `code` in green, links: web ones clickable, others plain
    static func inline(_ s: String, base: NSColor) -> NSAttributedString {
        let para = NSMutableParagraphStyle()
        para.lineSpacing = 4
        let attrs: [NSAttributedString.Key: Any] = [.font: SloopStyle.font(16), .foregroundColor: base, .paragraphStyle: para]
        guard let parsed = try? AttributedString(markdown: s, options: .init(interpretedSyntax: .inlineOnlyPreservingWhitespace)) else {
            return NSAttributedString(string: s, attributes: attrs)
        }
        let out = NSMutableAttributedString()
        for run in parsed.runs {
            var a = attrs
            let piece = String(parsed[run.range].characters)
            if let intent = run.inlinePresentationIntent {
                if intent.contains(.stronglyEmphasized) { a[.foregroundColor] = SloopStyle.fg }
                if intent.contains(.code) { a[.foregroundColor] = SloopStyle.track[1] }
            }
            if let link = run.link, let scheme = link.scheme, scheme.hasPrefix("http") {
                a[.link] = link
            }
            out.append(NSAttributedString(string: piece, attributes: a))
        }
        return out
    }
}
