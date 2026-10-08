import AppKit

/// Lays out the whole window and drives the live displays.
final class MainView: NSView {
    let model: LabModel
    let topBar: TopBar
    let banner: BannerView
    let design: DesignPanel
    let zPlane: ZPlaneView
    let response: ResponseView
    let spectrum: SpectrumView
    let spectrogram: SpectrogramView
    let scope: ScopeView
    let theory: TheoryPanel

    private let designPanel = Panel(title: "Design")
    private let zPanel = Panel(title: "Z-plane")
    private let responsePanel = Panel(title: "Response")
    private let livePanel = Panel(title: "Live")
    private let theoryPanel = Panel(title: "Theory")

    private var toolControl: NSSegmentedControl!
    private var mapButton: NSButton!
    private var responseControl: NSSegmentedControl!
    private var liveControl: NSSegmentedControl!
    private var axisControl: NSSegmentedControl!
    private var predictionBox: NSButton!
    private var spectrogramSource: NSSegmentedControl!
    private var scopeWindow: NSPopUpButton!
    private var holdButton: NSButton!
    private let toast = NSTextField(labelWithString: "")
    private var timer: Timer?
    private var lastMarkerFrequency = 0.0
    private var theoryWidth: NSLayoutConstraint!

    static let scopeWindows: [Double] = [2, 5, 10, 20, 50, 100, 500]

    init(model: LabModel) {
        self.model = model
        topBar = TopBar(model: model)
        banner = BannerView(model: model)
        design = DesignPanel(model: model)
        zPlane = ZPlaneView(model: model)
        response = ResponseView(model: model)
        spectrum = SpectrumView(model: model)
        spectrogram = SpectrogramView(model: model)
        scope = ScopeView(model: model)
        theory = TheoryPanel(model: model)
        super.init(frame: NSRect(x: 0, y: 0, width: 1440, height: 880))
        wantsLayer = true
        layer?.backgroundColor = Theme.app.background.cgColor
        buildHeaders()
        buildLayout()
        registerForDraggedTypes([.fileURL])
        model.observe { [weak self] change in self?.modelChanged(change) }
        modelChanged(.all)
        theory.onCopied = { [weak self] message in self?.showToast(message) }
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError() }

    // MARK: Building

    private func buildHeaders() {
        toolControl = makeSegmented(["Move", "+ Zero", "+ Pole"], target: self, action: #selector(toolChanged))
        toolControl.toolTip = "Move: drag poles and zeros. + Zero / + Pole: click in the plane to add one (near the real axis adds a real one)."
        mapButton = NSButton(checkboxWithTitle: "|H| map", target: self, action: #selector(mapToggled))
        mapButton.controlSize = .small
        mapButton.font = Fonts.small
        mapButton.toolTip = "Shade the plane by |H(z)|: blue valleys at zeros, red peaks at poles (roots at the origin, which only add delay, are left out)"
        let reset = makeIconButton("arrow.up.left.and.down.right.magnifyingglass", tooltip: "Reset zoom (scroll or pinch to zoom, drag empty space to pan)",
                                   target: self, action: #selector(resetZoom))
        let zExport = makeIconButton("square.and.arrow.up", tooltip: "Export this plot as a PNG figure", target: self, action: #selector(exportZ))
        for v in [toolControl!, mapButton!, reset, zExport] { zPanel.controls.addArrangedSubview(v) }
        zPanel.setBody(zPlane)

        responseControl = makeSegmented(ResponseMode.allCases.map(\.title), target: self, action: #selector(responseModeChanged))
        responseControl.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        let rExport = makeIconButton("square.and.arrow.up", tooltip: "Export this plot as a PNG figure", target: self, action: #selector(exportResponse))
        for v in [responseControl!, rExport] { responsePanel.controls.addArrangedSubview(v) }
        responsePanel.setBody(response)

        liveControl = makeSegmented(LiveView.allCases.map(\.title), target: self, action: #selector(liveChanged))
        axisControl = makeSegmented(["Log f", "Linear f"], target: self, action: #selector(axisChanged))
        axisControl.toolTip = "Frequency axis scale (all plots)"
        predictionBox = NSButton(checkboxWithTitle: "Predicted", target: self, action: #selector(predictionToggled))
        predictionBox.controlSize = .small
        predictionBox.font = Fonts.small
        predictionBox.toolTip = "Overlay input spectrum + |H| (dB): what theory says the output should be"
        spectrogramSource = makeSegmented(["Input", "Output"], target: self, action: #selector(spectrogramSourceChanged))
        scopeWindow = makePopup(Self.scopeWindows.map { "\(Int($0)) ms" }, target: self, action: #selector(scopeWindowChanged))
        scopeWindow.toolTip = "Time shown across the screen"
        holdButton = NSButton(checkboxWithTitle: "Hold", target: self, action: #selector(holdToggled))
        holdButton.controlSize = .small
        holdButton.font = Fonts.small
        holdButton.toolTip = "Freeze the live display"
        let lExport = makeIconButton("square.and.arrow.up", tooltip: "Export this view as a PNG figure", target: self, action: #selector(exportLive))
        for v in [liveControl!, predictionBox!, spectrogramSource!, scopeWindow!, holdButton!, axisControl!, lExport] {
            livePanel.controls.addArrangedSubview(v)
        }
        let liveBody = NSView()
        for v in [spectrum, spectrogram, scope] as [NSView] {
            v.translatesAutoresizingMaskIntoConstraints = false
            liveBody.addSubview(v)
            NSLayoutConstraint.activate([
                v.topAnchor.constraint(equalTo: liveBody.topAnchor),
                v.leadingAnchor.constraint(equalTo: liveBody.leadingAnchor),
                v.trailingAnchor.constraint(equalTo: liveBody.trailingAnchor),
                v.bottomAnchor.constraint(equalTo: liveBody.bottomAnchor),
            ])
        }
        livePanel.setBody(liveBody)

        theoryPanel.controls.addArrangedSubview(theory.pageControl)
        theoryPanel.setBody(theory)
        designPanel.setBody(design)
    }

    private func buildLayout() {
        let views: [NSView] = [topBar, banner, designPanel, zPanel, responsePanel, livePanel, theoryPanel, toast]
        for v in views {
            v.translatesAutoresizingMaskIntoConstraints = false
            addSubview(v)
        }
        let gap: CGFloat = 10
        theoryWidth = theoryPanel.widthAnchor.constraint(equalToConstant: 340)
        let topRow = zPanel.heightAnchor.constraint(equalTo: livePanel.heightAnchor, multiplier: 1.0)
        topRow.priority = .defaultHigh
        // Roughly square plot area (width = body height), given up first when space is short.
        let zWidth = zPanel.widthAnchor.constraint(equalTo: zPanel.heightAnchor, constant: -26)
        zWidth.priority = NSLayoutConstraint.Priority(740)
        NSLayoutConstraint.activate([
            topBar.topAnchor.constraint(equalTo: safeAreaLayoutGuide.topAnchor),
            topBar.leadingAnchor.constraint(equalTo: leadingAnchor),
            topBar.trailingAnchor.constraint(equalTo: trailingAnchor),
            topBar.heightAnchor.constraint(equalToConstant: 54),
            banner.topAnchor.constraint(equalTo: topBar.bottomAnchor),
            banner.leadingAnchor.constraint(equalTo: leadingAnchor),
            banner.trailingAnchor.constraint(equalTo: trailingAnchor),

            designPanel.topAnchor.constraint(equalTo: banner.bottomAnchor, constant: gap),
            designPanel.leadingAnchor.constraint(equalTo: leadingAnchor, constant: gap),
            designPanel.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -gap),
            designPanel.widthAnchor.constraint(equalToConstant: 272),

            theoryPanel.topAnchor.constraint(equalTo: designPanel.topAnchor),
            theoryPanel.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -gap),
            theoryPanel.bottomAnchor.constraint(equalTo: designPanel.bottomAnchor),
            theoryWidth,

            zPanel.topAnchor.constraint(equalTo: designPanel.topAnchor),
            zPanel.leadingAnchor.constraint(equalTo: designPanel.trailingAnchor, constant: gap),
            zWidth,
            zPanel.widthAnchor.constraint(greaterThanOrEqualToConstant: 240),
            responsePanel.topAnchor.constraint(equalTo: zPanel.topAnchor),
            responsePanel.leadingAnchor.constraint(equalTo: zPanel.trailingAnchor, constant: gap),
            responsePanel.trailingAnchor.constraint(equalTo: theoryPanel.leadingAnchor, constant: -gap),
            responsePanel.bottomAnchor.constraint(equalTo: zPanel.bottomAnchor),
            responsePanel.widthAnchor.constraint(greaterThanOrEqualToConstant: 380),

            livePanel.topAnchor.constraint(equalTo: zPanel.bottomAnchor, constant: gap),
            livePanel.leadingAnchor.constraint(equalTo: zPanel.leadingAnchor),
            livePanel.trailingAnchor.constraint(equalTo: responsePanel.trailingAnchor),
            livePanel.bottomAnchor.constraint(equalTo: designPanel.bottomAnchor),
            topRow,
            livePanel.heightAnchor.constraint(greaterThanOrEqualToConstant: 220),

            toast.centerXAnchor.constraint(equalTo: livePanel.centerXAnchor),
            toast.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -28),
        ])
        toast.font = NSFont.systemFont(ofSize: 12.5, weight: .medium)
        toast.textColor = .white
        toast.wantsLayer = true
        toast.drawsBackground = true
        toast.backgroundColor = NSColor(hex: 0x2B2A33)
        toast.layer?.cornerRadius = 8
        toast.alphaValue = 0
        toast.alignment = .center
    }

    // MARK: Model changes

    private func modelChanged(_ change: ModelChange) {
        if !change.isDisjoint(with: [.filter, .zeros, .sampleRate]) {
            zPlane.invalidate()
            response.invalidate()
        }
        if !change.isDisjoint(with: [.filter, .source, .sampleRate]) {
            spectrum.resetSmoothing()
        }
        if !change.isDisjoint(with: [.display, .sampleRate]) {
            response.invalidate()
            zPlane.invalidate()
            spectrum.needsDisplay = true
            spectrogram.reset()
        }
        if !change.isDisjoint(with: [.cursor, .selection]) {
            zPlane.needsDisplay = true
            response.needsDisplay = true
        }
        if !change.isDisjoint(with: [.display, .selection, .source]) {
            refreshHeaders()
        }
        if change.contains(.filter) {
            window?.title = "Filter Lab: " + filterTitle(model.spec)
        }
    }

    private func refreshHeaders() {
        toolControl.selectedSegment = model.zTool.rawValue
        mapButton.state = model.showHMap ? .on : .off
        responseControl.selectedSegment = model.responseMode.rawValue
        liveControl.selectedSegment = model.liveView.rawValue
        axisControl.selectedSegment = model.logAxis ? 0 : 1
        predictionBox.state = model.showPrediction ? .on : .off
        spectrogramSource.selectedSegment = model.spectrogramShowsInput ? 0 : 1
        if let i = Self.scopeWindows.firstIndex(of: model.scopeWindowMs) { scopeWindow.selectItem(at: i) }
        holdButton.state = model.hold ? .on : .off
        let live = model.liveView
        spectrum.isHidden = live != .spectrum
        spectrogram.isHidden = live != .spectrogram
        scope.isHidden = live != .scope
        predictionBox.isHidden = live != .spectrum
        spectrogramSource.isHidden = live != .spectrogram
        scopeWindow.isHidden = live != .scope
        axisControl.isHidden = live == .scope
    }

    // MARK: Animation

    func startTimer() {
        let t = Timer(timeInterval: 1.0 / 60, repeats: true) { [weak self] _ in self?.tick() }
        RunLoop.main.add(t, forMode: .common)
        timer = t
    }

    func stopTimer() {
        timer?.invalidate()
        timer = nil
    }

    private func tick() {
        model.audio.tick()
        topBar.tick()
        guard window?.isVisible == true, window?.occlusionState.contains(.visible) == true else { return }
        switch model.liveView {
        case .spectrum: spectrum.tick()
        case .spectrogram: spectrogram.tick()
        case .scope: scope.tick()
        }
        if [.sine, .square, .sweep].contains(model.source) {
            let f = model.audio.params.currentFrequency.value
            if abs(f - lastMarkerFrequency) > 0.05 {
                lastMarkerFrequency = f
                zPlane.needsDisplay = true
                response.needsDisplay = true
            }
        }
    }

    func showToast(_ message: String) {
        toast.stringValue = "   " + message + "   "
        NSAnimationContext.runAnimationGroup { ctx in
            ctx.duration = 0.15
            toast.animator().alphaValue = 1
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.8) { [weak self] in
            NSAnimationContext.runAnimationGroup { ctx in
                ctx.duration = 0.4
                self?.toast.animator().alphaValue = 0
            }
        }
    }

    // MARK: Actions

    @objc private func toolChanged() { model.zTool = ZTool(rawValue: toolControl.selectedSegment) ?? .move }
    @objc private func mapToggled() { model.showHMap = mapButton.state == .on }
    @objc private func resetZoom() { zPlane.resetView() }
    @objc private func responseModeChanged() { model.responseMode = ResponseMode(rawValue: responseControl.selectedSegment) ?? .magnitude }
    @objc private func liveChanged() { model.liveView = LiveView(rawValue: liveControl.selectedSegment) ?? .spectrum }
    @objc private func axisChanged() { model.logAxis = axisControl.selectedSegment == 0 }
    @objc private func predictionToggled() { model.showPrediction = predictionBox.state == .on }
    @objc private func spectrogramSourceChanged() { model.spectrogramShowsInput = spectrogramSource.selectedSegment == 0 }
    @objc private func scopeWindowChanged() { model.scopeWindowMs = Self.scopeWindows[scopeWindow.indexOfSelectedItem] }
    @objc private func holdToggled() { model.hold = holdButton.state == .on }

    @objc func exportZ() { savePNG(zPlane.pngData(), suggestedName: "z-plane.png", window: window) }
    @objc func exportResponse() {
        savePNG(response.pngData(), suggestedName: "\(model.responseMode.title.lowercased().replacingOccurrences(of: " ", with: "-"))-response.png", window: window)
    }
    @objc func exportLive() {
        let view: PlotView
        switch model.liveView {
        case .spectrum: view = spectrum
        case .spectrogram: view = spectrogram
        case .scope: view = scope
        }
        savePNG(view.pngData(light: model.liveView != .spectrogram), suggestedName: "\(model.liveView.title.lowercased()).png", window: window)
    }

    // MARK: Drag and drop audio files

    private func audioURL(_ info: NSDraggingInfo) -> URL? {
        let options: [NSPasteboard.ReadingOptionKey: Any] = [.urlReadingContentsConformToTypes: ["public.audio"]]
        return (info.draggingPasteboard.readObjects(forClasses: [NSURL.self], options: options) as? [URL])?.first
    }

    override func draggingEntered(_ sender: NSDraggingInfo) -> NSDragOperation {
        audioURL(sender) == nil ? [] : .copy
    }

    override func performDragOperation(_ sender: NSDraggingInfo) -> Bool {
        guard let url = audioURL(sender) else { return false }
        model.loadFile(url)
        return true
    }

    func setTheoryVisible(_ visible: Bool) {
        theoryPanel.isHidden = !visible
        theoryWidth.constant = visible ? 340 : 0
    }

    var theoryVisible: Bool { !theoryPanel.isHidden }
}
