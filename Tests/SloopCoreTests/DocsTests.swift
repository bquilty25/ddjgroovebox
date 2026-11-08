// SPDX-License-Identifier: GPL-3.0-only
// The FAQ shown in the app (Sources/DDJGroovebox/Resources/FAQ.md) is docs/FAQ.md: edit docs/FAQ.md, then copy it
// (scripts/build_app.sh copies it too).
import Foundation
import XCTest

final class DocsTests: XCTestCase {
    func testInAppFAQMatchesDocs() throws {
        let root = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        let docs = try String(contentsOf: root.appendingPathComponent("docs/FAQ.md"), encoding: .utf8)
        let app = try String(contentsOf: root.appendingPathComponent("Sources/DDJGroovebox/Resources/FAQ.md"), encoding: .utf8)
        XCTAssertEqual(app, docs, "copy docs/FAQ.md to Sources/DDJGroovebox/Resources/FAQ.md")
        let guide = try String(contentsOf: root.appendingPathComponent("docs/TRIGDECK.md"), encoding: .utf8)
        let appGuide = try String(contentsOf: root.appendingPathComponent("Sources/DDJGroovebox/Resources/TRIGDECK.md"), encoding: .utf8)
        XCTAssertEqual(appGuide, guide, "copy docs/TRIGDECK.md to Sources/DDJGroovebox/Resources/TRIGDECK.md")
    }
}
