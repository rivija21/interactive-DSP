import AppKit

/// The window. Keys nobody else handled end up here: Space is the A/B switch.
final class LabWindow: NSWindow {
    weak var model: LabModel?

    override func keyDown(with event: NSEvent) {
        guard let model, event.modifierFlags.intersection([.command, .control, .option]).isEmpty else {
            super.keyDown(with: event)
            return
        }
        switch event.charactersIgnoringModifiers?.lowercased() {
        case " ": model.filterOn.toggle()
        case "l": model.listen.toggle()
        case "\u{7f}", "\u{f728}": model.deleteSelectedItem()
        case "\u{1b}": model.zTool = .move
        default: super.keyDown(with: event)
        }
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate, NSMenuItemValidation {
    let model = LabModel()
    private var window: LabWindow!
    private var mainView: MainView!

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.appearance = NSAppearance(named: .aqua)
        buildMenu()
        let visible = NSScreen.main?.visibleFrame ?? NSRect(x: 0, y: 0, width: 1440, height: 900)
        let size = NSSize(width: min(1480, visible.width - 20), height: min(940, visible.height - 10))
        window = LabWindow(contentRect: NSRect(origin: .zero, size: size),
                           styleMask: [.titled, .closable, .miniaturizable, .resizable],
                           backing: .buffered, defer: false)
        window.model = model
        window.title = "Filter Lab"
        window.titlebarAppearsTransparent = true
        window.backgroundColor = Theme.app.panel
        window.contentMinSize = NSSize(width: 1200, height: 740)
        window.collectionBehavior = [.fullScreenPrimary]
        window.delegate = self
        mainView = MainView(model: model)
        window.contentView = mainView
        window.center()
        window.setFrameAutosaveName("FilterLabWindow")
        window.makeKeyAndOrderFront(nil)
        model.start()
        mainView.startTimer()
        NSApp.activate(ignoringOtherApps: true)
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    func applicationWillTerminate(_ notification: Notification) {
        model.save()
        mainView?.stopTimer()
        model.audio.shutdown()
    }

    func windowWillReturnUndoManager(_ window: NSWindow) -> UndoManager? { model.undoManager }

    func application(_ sender: NSApplication, openFile filename: String) -> Bool {
        model.loadFile(URL(fileURLWithPath: filename))
        return true
    }

    // MARK: Menu

    private func item(_ title: String, _ action: Selector?, _ key: String = "", _ mods: NSEvent.ModifierFlags = .command, tag: Int = 0) -> NSMenuItem {
        let i = NSMenuItem(title: title, action: action, keyEquivalent: key)
        i.keyEquivalentModifierMask = mods
        i.tag = tag
        if let action, responds(to: action) { i.target = self }
        return i
    }

    private func buildMenu() {
        let main = NSMenu()

        let app = NSMenu()
        app.addItem(item("About Filter Lab", #selector(about)))
        app.addItem(.separator())
        app.addItem(item("Hide Filter Lab", #selector(NSApplication.hide(_:)), "h"))
        app.addItem(item("Hide Others", #selector(NSApplication.hideOtherApplications(_:)), "h", [.command, .option]))
        app.addItem(item("Show All", #selector(NSApplication.unhideAllApplications(_:))))
        app.addItem(.separator())
        app.addItem(item("Quit Filter Lab", #selector(NSApplication.terminate(_:)), "q"))
        addSubmenu(main, "Filter Lab", app)

        let file = NSMenu(title: "File")
        file.addItem(item("Open Audio File…", #selector(openAudio), "o"))
        file.addItem(item("Export Filtered Audio…", #selector(exportAudio), "e"))
        file.addItem(.separator())
        file.addItem(item("Export Response Figure…", #selector(exportResponseFigure), "r", [.command, .shift]))
        file.addItem(item("Export Z-plane Figure…", #selector(exportZFigure), "p", [.command, .shift]))
        file.addItem(item("Export Live View Figure…", #selector(exportLiveFigure), "l", [.command, .shift]))
        file.addItem(.separator())
        file.addItem(item("Close Window", #selector(NSWindow.performClose(_:)), "w"))
        addSubmenu(main, "File", file)

        let edit = NSMenu(title: "Edit")
        edit.addItem(item("Undo", Selector(("undo:")), "z"))
        edit.addItem(item("Redo", Selector(("redo:")), "z", [.command, .shift]))
        edit.addItem(.separator())
        edit.addItem(item("Cut", #selector(NSText.cut(_:)), "x"))
        edit.addItem(item("Copy", #selector(NSText.copy(_:)), "c"))
        edit.addItem(item("Paste", #selector(NSText.paste(_:)), "v"))
        edit.addItem(item("Select All", #selector(NSText.selectAll(_:)), "a"))
        edit.addItem(.separator())
        for (i, lang) in CodeLanguage.allCases.enumerated() {
            let key = ["m", "y", "k"][i]
            edit.addItem(item("Copy Filter as \(lang.title)", #selector(copyCode(_:)), key, [.command, .option], tag: i))
        }
        edit.addItem(.separator())
        edit.addItem(item("Delete Selected Pole/Zero", #selector(deleteSelected)))
        addSubmenu(main, "Edit", edit)

        let view = NSMenu(title: "View")
        for (i, mode) in LiveView.allCases.enumerated() {
            view.addItem(item(mode.title, #selector(chooseLive(_:)), "\(i + 1)", .command, tag: i))
        }
        view.addItem(.separator())
        for (i, mode) in ResponseMode.allCases.enumerated() {
            view.addItem(item("Response: \(mode.title)", #selector(chooseResponse(_:)), "\(i + 1)", [.command, .option], tag: i))
        }
        view.addItem(.separator())
        view.addItem(item("Logarithmic Frequency Axis", #selector(toggleLogAxis), "g"))
        view.addItem(item("|H(z)| Map in Z-plane", #selector(toggleMap)))
        view.addItem(item("Predicted Output on Spectrum", #selector(togglePrediction)))
        view.addItem(item("Hold Live Display", #selector(toggleHold), "h", [.command, .shift]))
        view.addItem(item("Reset Z-plane Zoom", #selector(resetZoom), "0"))
        view.addItem(.separator())
        view.addItem(item("Show Theory Panel", #selector(toggleTheory), "t"))
        view.addItem(item("Enter Full Screen", #selector(NSWindow.toggleFullScreen(_:)), "f", [.command, .control]))
        addSubmenu(main, "View", view)

        let audio = NSMenu(title: "Audio")
        audio.addItem(item("Listen", #selector(toggleListen), "l"))
        audio.addItem(item("Filter On (Space switches A/B)", #selector(toggleFilter), "b"))
        audio.addItem(item("Pause Input", #selector(togglePause), "."))
        audio.addItem(.separator())
        let sources = NSMenu(title: "Source")
        for s in SourceKind.allCases {
            sources.addItem(item(s.title, #selector(chooseSource(_:)), tag: s.rawValue))
        }
        let sourceItem = NSMenuItem(title: "Source", action: nil, keyEquivalent: "")
        sourceItem.submenu = sources
        audio.addItem(sourceItem)
        let rates = NSMenu(title: "Sample Rate")
        for (i, r) in LabModel.sampleRates.enumerated() {
            rates.addItem(item(formatHz(r), #selector(chooseRate(_:)), tag: i))
        }
        let rateItem = NSMenuItem(title: "Sample Rate", action: nil, keyEquivalent: "")
        rateItem.submenu = rates
        audio.addItem(rateItem)
        addSubmenu(main, "Audio", audio)

        let learn = NSMenu(title: "Learn")
        learn.addItem(item("All Lessons", #selector(showLessons)))
        learn.addItem(.separator())
        for (i, lesson) in Lessons.all.enumerated() {
            learn.addItem(item(lesson.title, #selector(showLesson(_:)), tag: i))
        }
        learn.addItem(.separator())
        let experiments = NSMenu(title: "Experiments")
        for (i, e) in Experiments.all.enumerated() {
            experiments.addItem(item(e.title, #selector(runExperiment(_:)), tag: i))
        }
        let expItem = NSMenuItem(title: "Experiments", action: nil, keyEquivalent: "")
        expItem.submenu = experiments
        learn.addItem(expItem)
        addSubmenu(main, "Learn", learn)

        let windowMenu = NSMenu(title: "Window")
        windowMenu.addItem(item("Minimize", #selector(NSWindow.performMiniaturize(_:)), "m"))
        windowMenu.addItem(item("Zoom", #selector(NSWindow.performZoom(_:))))
        addSubmenu(main, "Window", windowMenu)
        NSApp.windowsMenu = windowMenu

        let help = NSMenu(title: "Help")
        help.addItem(item("Filter Lab Lessons", #selector(showLessons), "?"))
        addSubmenu(main, "Help", help)
        NSApp.helpMenu = help

        NSApp.mainMenu = main
    }

    private func addSubmenu(_ main: NSMenu, _ title: String, _ menu: NSMenu) {
        let i = NSMenuItem(title: title, action: nil, keyEquivalent: "")
        menu.title = title
        i.submenu = menu
        main.addItem(i)
    }

    func validateMenuItem(_ menuItem: NSMenuItem) -> Bool {
        switch menuItem.action {
        case #selector(toggleLogAxis): menuItem.state = model.logAxis ? .on : .off
        case #selector(toggleMap): menuItem.state = model.showHMap ? .on : .off
        case #selector(togglePrediction): menuItem.state = model.showPrediction ? .on : .off
        case #selector(toggleHold): menuItem.state = model.hold ? .on : .off
        case #selector(toggleListen): menuItem.state = model.listen ? .on : .off
        case #selector(toggleFilter): menuItem.state = model.filterOn ? .on : .off
        case #selector(togglePause): menuItem.state = model.paused ? .on : .off
        case #selector(toggleTheory): menuItem.state = mainView.theoryVisible ? .on : .off
        case #selector(chooseLive(_:)): menuItem.state = model.liveView.rawValue == menuItem.tag ? .on : .off
        case #selector(chooseResponse(_:)): menuItem.state = model.responseMode.rawValue == menuItem.tag ? .on : .off
        case #selector(chooseSource(_:)): menuItem.state = model.source.rawValue == menuItem.tag ? .on : .off
        case #selector(chooseRate(_:)): menuItem.state = LabModel.sampleRates[menuItem.tag] == model.fs ? .on : .off
        case #selector(deleteSelected): return model.selectedItem != nil && model.spec.method == .poleZero
        case #selector(exportAudio): return model.source != .microphone
        default: break
        }
        return true
    }

    // MARK: Actions

    @objc private func about() {
        let credits = NSAttributedString(string: "Design digital filters and hear them live.\nIIR (Butterworth, Chebyshev, Elliptic, Bessel), FIR window method and hand-placed poles and zeros. Results match SciPy.",
                                         attributes: [.font: NSFont.systemFont(ofSize: 11), .foregroundColor: NSColor.secondaryLabelColor])
        NSApp.orderFrontStandardAboutPanel(options: [.credits: credits, .applicationName: "Filter Lab"])
    }

    @objc private func openAudio() { model.openAudioFile(window: window) }
    @objc private func exportAudio() { exportFilteredAudio(model: model, window: window) }
    @objc private func exportResponseFigure() { mainView.exportResponse() }
    @objc private func exportZFigure() { mainView.exportZ() }
    @objc private func exportLiveFigure() { mainView.exportLive() }

    @objc private func copyCode(_ sender: NSMenuItem) {
        let lang = CodeLanguage.allCases[sender.tag]
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(exportCode(lang, spec: model.spec, filter: model.filter), forType: .string)
        mainView.showToast("Copied \(lang.title) code to the clipboard")
    }

    @objc private func deleteSelected() { model.deleteSelectedItem() }
    @objc private func chooseLive(_ sender: NSMenuItem) { model.liveView = LiveView(rawValue: sender.tag) ?? .spectrum }
    @objc private func chooseResponse(_ sender: NSMenuItem) { model.responseMode = ResponseMode(rawValue: sender.tag) ?? .magnitude }
    @objc private func toggleLogAxis() { model.logAxis.toggle() }
    @objc private func toggleMap() { model.showHMap.toggle() }
    @objc private func togglePrediction() { model.showPrediction.toggle() }
    @objc private func toggleHold() { model.hold.toggle() }
    @objc private func resetZoom() { mainView.zPlane.resetView() }
    @objc private func toggleTheory() { mainView.setTheoryVisible(!mainView.theoryVisible) }
    @objc private func toggleListen() { model.listen.toggle() }
    @objc private func toggleFilter() { model.filterOn.toggle() }
    @objc private func togglePause() { model.paused.toggle() }
    @objc private func chooseSource(_ sender: NSMenuItem) {
        if let s = SourceKind(rawValue: sender.tag) { model.setSource(s, window: window) }
    }
    @objc private func chooseRate(_ sender: NSMenuItem) { model.setSampleRate(LabModel.sampleRates[sender.tag]) }
    @objc private func showLessons() {
        if !mainView.theoryVisible { mainView.setTheoryVisible(true) }
        model.theoryPage = .lessons
    }
    @objc private func showLesson(_ sender: NSMenuItem) {
        if !mainView.theoryVisible { mainView.setTheoryVisible(true) }
        model.theoryPage = .lesson(Lessons.all[sender.tag].id)
    }
    @objc private func runExperiment(_ sender: NSMenuItem) {
        if !mainView.theoryVisible { mainView.setTheoryVisible(true) }
        model.theoryPage = .experiments
        model.runExperiment(Experiments.all[sender.tag].id)
    }
}
