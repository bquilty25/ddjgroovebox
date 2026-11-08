// SPDX-License-Identifier: GPL-3.0-only
import AppKit

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    var controller: AppController?

    func applicationDidFinishLaunching(_ notification: Notification) {
        if ProcessInfo.processInfo.environment["DDJ_PROBE"] != nil { HardwareProbe.run(); return }
        if let path = ProcessInfo.processInfo.environment["DDJ_FAQ_SNAPSHOT"] {   // the FAQ (or DDJ_DOC=GUIDE) to a PNG, then quit
            let doc = ProcessInfo.processInfo.environment["DDJ_DOC"] == "GUIDE" ? FAQWindowController.guide : FAQWindowController.shared
            doc.show()
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
                if let v = doc.window?.contentView, let rep = v.bitmapImageRepForCachingDisplay(in: v.bounds) {
                    v.cacheDisplay(in: v.bounds, to: rep)
                    try? rep.representation(using: .png, properties: [:])?.write(to: URL(fileURLWithPath: path))
                }
                exit(0)
            }
            return
        }
        buildMenu()
        // the scripted modes (snapshot, demo) play into a throwaway flash image: never into the user's project
        let env = ProcessInfo.processInfo.environment
        let scripted = env["DDJ_SNAPSHOT"] != nil || env["DDJ_DEMO"] != nil
        let tmp = FileManager.default.temporaryDirectory
        flashURL = scripted ? tmp.appendingPathComponent("ddjgroovebox-demo-\(getpid()).bin") : Groovebox.defaultFlashURL
        let project = scripted ? tmp.appendingPathComponent("trigdeck-demo-\(getpid()).project") : TrigEngine.defaultProjectURL
        let trig = TrigInstrument(projectURL: project)
        trigdeck = trig
        let c: AppController
        if env["DDJ_INSTRUMENT"]?.lowercased() == "sloop" {
            let s = sloopInstrument()
            c = AppController(instrument: s, io: s.groovebox)
        } else {
            c = AppController(instrument: trig, io: trig.engine)
        }
        controller = c
        c.start()
        NSApp.activate(ignoringOtherApps: true)
    }

    private var flashURL = Groovebox.defaultFlashURL
    private var trigdeck: TrigInstrument?
    private var sloop: SloopInstrument?
    /// SLOOP boots when first chosen (its firmware is one instance per process: kept for the session)
    private func sloopInstrument() -> SloopInstrument {
        if let sloop { return sloop }
        let s = SloopInstrument(flashURL: flashURL)
        sloop = s
        return s
    }

    @objc func playTrigdeck(_ sender: Any?) {
        guard let t = trigdeck else { return }
        controller?.switchTo(t, io: t.engine)
    }

    @objc func playSloop(_ sender: Any?) {
        let s = sloopInstrument()
        controller?.switchTo(s, io: s.groovebox)
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    func applicationWillTerminate(_ notification: Notification) { controller?.shutdown() }

    @objc func panic(_ sender: Any?) { controller?.panic() }

    @objc func revealFlash(_ sender: Any?) {
        guard let gb = controller?.groovebox else { return }
        gb.save()
        NSWorkspace.shared.activateFileViewerSelecting([gb.flashURL])
    }

    @objc func openSloopManual(_ sender: Any?) {
        NSWorkspace.shared.open(URL(string: "https://github.com/isod89/sloop-fm1/blob/main/SLOOP.md")!)
    }

    @objc func openSource(_ sender: Any?) {
        NSWorkspace.shared.open(URL(string: "https://github.com/isod89/sloop-fm1")!)
    }

    private func buildMenu() {
        let main = NSMenu()
        func submenu(_ title: String) -> NSMenu {
            let item = NSMenuItem()
            main.addItem(item)
            let m = NSMenu(title: title)
            item.submenu = m
            return m
        }
        let app = submenu("DDJ Groovebox")
        app.addItem(withTitle: "About DDJ Groovebox", action: #selector(NSApplication.orderFrontStandardAboutPanel(_:)), keyEquivalent: "")
        app.addItem(.separator())
        app.addItem(withTitle: "Hide DDJ Groovebox", action: #selector(NSApplication.hide(_:)), keyEquivalent: "h")
        let others = app.addItem(withTitle: "Hide Others", action: #selector(NSApplication.hideOtherApplications(_:)), keyEquivalent: "h")
        others.keyEquivalentModifierMask = [.command, .option]
        app.addItem(withTitle: "Show All", action: #selector(NSApplication.unhideAllApplications(_:)), keyEquivalent: "")
        app.addItem(.separator())
        app.addItem(withTitle: "Quit DDJ Groovebox", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")

        let edit = submenu("Edit")                        // (copy and select in the FAQ)
        edit.addItem(withTitle: "Copy", action: #selector(NSText.copy(_:)), keyEquivalent: "c")
        edit.addItem(withTitle: "Select All", action: #selector(NSText.selectAll(_:)), keyEquivalent: "a")
        let find = edit.addItem(withTitle: "Find…", action: #selector(NSTextView.performFindPanelAction(_:)), keyEquivalent: "f")
        find.tag = Int(NSFindPanelAction.showFindPanel.rawValue)

        let inst = submenu("Instrument")
        inst.addItem(withTitle: "TRIGDECK", action: #selector(playTrigdeck(_:)), keyEquivalent: "1").target = self
        inst.addItem(.separator())
        let cart = inst.addItem(withTitle: "Cartridges (FM-1 firmwares)", action: nil, keyEquivalent: "")
        cart.isEnabled = false
        inst.addItem(withTitle: "SLOOP 2.8.5 by isod89", action: #selector(playSloop(_:)), keyEquivalent: "2").target = self

        let gb = submenu("Groovebox")
        gb.addItem(withTitle: "Panic (all notes off, stop)", action: #selector(panic(_:)), keyEquivalent: ".").target = self
        gb.addItem(withTitle: "Show Saved Data in Finder", action: #selector(revealFlash(_:)), keyEquivalent: "").target = self

        let view = submenu("View")
        let fs = view.addItem(withTitle: "Enter Full Screen", action: #selector(NSWindow.toggleFullScreen(_:)), keyEquivalent: "f")
        fs.keyEquivalentModifierMask = [.command, .control]

        let window = submenu("Window")
        window.addItem(withTitle: "Close", action: #selector(NSWindow.performClose(_:)), keyEquivalent: "w")
        window.addItem(withTitle: "Minimize", action: #selector(NSWindow.performMiniaturize(_:)), keyEquivalent: "m")
        window.addItem(withTitle: "Zoom", action: #selector(NSWindow.performZoom(_:)), keyEquivalent: "")
        NSApp.windowsMenu = window

        let help = submenu("Help")
        help.addItem(withTitle: "TRIGDECK Guide", action: #selector(openGuide(_:)), keyEquivalent: "?").target = self
        let faq = help.addItem(withTitle: "DDJ Groovebox FAQ", action: #selector(openFAQ(_:)), keyEquivalent: "?")
        faq.target = self
        faq.keyEquivalentModifierMask = [.command, .shift]
        help.addItem(withTitle: "SLOOP Manual (online)", action: #selector(openSloopManual(_:)), keyEquivalent: "").target = self
        help.addItem(withTitle: "SLOOP Source Code (GPL-3.0)", action: #selector(openSource(_:)), keyEquivalent: "").target = self
        NSApp.mainMenu = main
        NSApp.helpMenu = help
    }

    @objc func openFAQ(_ sender: Any?) { FAQWindowController.shared.show() }
    @objc func openGuide(_ sender: Any?) { FAQWindowController.guide.show() }
}

MainActor.assumeIsolated {
    let app = NSApplication.shared
    app.setActivationPolicy(.regular)
    let delegate = AppDelegate()
    app.delegate = delegate
    app.run()
}
