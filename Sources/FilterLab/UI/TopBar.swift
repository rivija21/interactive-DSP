import AppKit

/// Source selection, listening controls, A/B switch, sample rate and meters.
final class TopBar: NSView {
    private unowned let model: LabModel
    private var refreshing = false

    private var playButton: NSButton!
    private var sourcePopup: NSPopUpButton!
    private let frequencyRow = SliderRow(title: "Frequency", min: 20, max: 20000, scale: .log, format: formatHz, parse: parseHz)
    private let micGainRow = SliderRow(title: "Mic gain", min: 1, max: 32, scale: .log,
                                       format: { formatSigned(20 * log10($0), "%.0f") + " dB" },
                                       parse: { s in Double(s.replacingOccurrences(of: "dB", with: "").replacingOccurrences(of: "\u{2212}", with: "-").trimmingCharacters(in: .whitespaces)).map { pow(10, $0 / 20) } })
    private let sweepLabel = makeLabel("", font: Fonts.readout, color: Theme.app.output)
    private let fileLabel = makeLabel("", font: Fonts.label, color: Theme.app.text)
    private var openButton: NSButton!
    private var abControl: NSSegmentedControl!
    private var listenButton: NSButton!
    private let volumeSlider = NSSlider()
    private var ratePopup: NSPopUpButton!
    let meter = LevelMeter()
    private let sourceExtras = NSStackView()

    init(model: LabModel) {
        self.model = model
        super.init(frame: .zero)
        wantsLayer = true
        layer?.backgroundColor = Theme.app.panel.cgColor
        build()
        model.observe { [weak self] change in
            if !change.isDisjoint(with: [.source, .output, .sampleRate]) { self?.refresh() }
        }
        refresh()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError() }

    private func build() {
        playButton = NSButton(image: NSImage(systemSymbolName: "pause.fill", accessibilityDescription: "Pause")!, target: self, action: #selector(togglePause))
        playButton.bezelStyle = .regularSquare
        playButton.isBordered = false
        playButton.contentTintColor = Theme.app.text
        playButton.toolTip = "Pause or resume the input"
        playButton.widthAnchor.constraint(equalToConstant: 24).isActive = true

        sourcePopup = NSPopUpButton(frame: .zero, pullsDown: false)
        for s in SourceKind.allCases {
            sourcePopup.addItem(withTitle: s.title)
            let item = sourcePopup.lastItem!
            item.image = NSImage(systemSymbolName: s.symbol, accessibilityDescription: nil)
            item.tag = s.rawValue
            if s == .microphone || s == .clicks { sourcePopup.menu?.addItem(.separator()) }
        }
        sourcePopup.target = self
        sourcePopup.action = #selector(sourceChanged)
        sourcePopup.widthAnchor.constraint(equalToConstant: 190).isActive = true

        frequencyRow.onChange = { [weak self] v in self?.model.frequency = v }
        frequencyRow.widthAnchor.constraint(equalToConstant: 210).isActive = true
        micGainRow.onChange = { [weak self] v in self?.model.micGain = v }
        micGainRow.widthAnchor.constraint(equalToConstant: 170).isActive = true
        openButton = NSButton(title: "Open…", target: self, action: #selector(openFile))
        openButton.controlSize = .small
        fileLabel.widthAnchor.constraint(lessThanOrEqualToConstant: 200).isActive = true

        sourceExtras.orientation = .horizontal
        sourceExtras.spacing = 10
        sourceExtras.alignment = .centerY
        for v in [frequencyRow, micGainRow, sweepLabel, fileLabel, openButton!] { sourceExtras.addArrangedSubview(v) }

        ratePopup = makePopup(LabModel.sampleRates.map { "fs = " + formatHzTick($0) + "Hz" }, target: self, action: #selector(rateChanged), size: .regular)
        ratePopup.toolTip = "Sampling rate. Nyquist (fs/2) is the top of every frequency axis."

        abControl = makeSegmented(["Original", "Filtered"], target: self, action: #selector(abChanged), size: .regular)
        abControl.toolTip = "Compare the original and filtered sound (Space)"
        abControl.setWidth(76, forSegment: 0)
        abControl.setWidth(76, forSegment: 1)

        listenButton = NSButton(title: "Listen", image: NSImage(systemSymbolName: "speaker.slash.fill", accessibilityDescription: nil)!,
                                target: self, action: #selector(toggleListen))
        listenButton.setButtonType(.pushOnPushOff)
        listenButton.bezelStyle = .push
        listenButton.imagePosition = .imageLeading
        listenButton.toolTip = "Play the result through your speakers or headphones (L)"
        listenButton.widthAnchor.constraint(equalToConstant: 92).isActive = true

        volumeSlider.minValue = 0
        volumeSlider.maxValue = 1
        volumeSlider.controlSize = .small
        volumeSlider.target = self
        volumeSlider.action = #selector(volumeChanged)
        volumeSlider.toolTip = "Volume"
        volumeSlider.widthAnchor.constraint(equalToConstant: 80).isActive = true

        let left = NSStackView(views: [playButton, sourcePopup, sourceExtras])
        left.orientation = .horizontal
        left.spacing = 10
        left.alignment = .centerY
        let right = NSStackView(views: [ratePopup, abControl, listenButton, volumeSlider, meter])
        right.orientation = .horizontal
        right.spacing = 12
        right.alignment = .centerY
        for v in [left, right] {
            v.translatesAutoresizingMaskIntoConstraints = false
            addSubview(v)
        }
        let line = NSBox()
        line.boxType = .custom
        line.borderWidth = 0
        line.fillColor = Theme.app.panelBorder
        line.translatesAutoresizingMaskIntoConstraints = false
        addSubview(line)
        NSLayoutConstraint.activate([
            left.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 14),
            left.centerYAnchor.constraint(equalTo: centerYAnchor),
            right.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -14),
            right.centerYAnchor.constraint(equalTo: centerYAnchor),
            left.trailingAnchor.constraint(lessThanOrEqualTo: right.leadingAnchor, constant: -12),
            meter.widthAnchor.constraint(equalToConstant: 120),
            meter.heightAnchor.constraint(equalToConstant: 26),
            line.leadingAnchor.constraint(equalTo: leadingAnchor),
            line.trailingAnchor.constraint(equalTo: trailingAnchor),
            line.bottomAnchor.constraint(equalTo: bottomAnchor),
            line.heightAnchor.constraint(equalToConstant: 1),
        ])
        right.setContentCompressionResistancePriority(.required, for: .horizontal)
    }

    func refresh() {
        refreshing = true
        defer { refreshing = false }
        let source = model.source
        sourcePopup.selectItem(withTag: source.rawValue)
        frequencyRow.isHidden = !source.usesFrequency
        frequencyRow.setRange(max: model.fs * 0.45)
        frequencyRow.setValue(model.frequency)
        micGainRow.isHidden = source != .microphone
        micGainRow.setValue(model.micGain)
        sweepLabel.isHidden = source != .sweep
        fileLabel.isHidden = source != .file
        openButton.isHidden = source != .file
        fileLabel.stringValue = model.audio.fileName.map { "“\($0)”" } ?? ""
        if let i = LabModel.sampleRates.firstIndex(of: model.fs) { ratePopup.selectItem(at: i) }
        abControl.selectedSegment = model.filterOn ? 1 : 0
        listenButton.state = model.listen ? .on : .off
        listenButton.image = NSImage(systemSymbolName: model.listen ? "speaker.wave.2.fill" : "speaker.slash.fill", accessibilityDescription: nil)
        listenButton.contentTintColor = model.listen ? Theme.app.output : nil
        volumeSlider.doubleValue = model.volume
        volumeSlider.isEnabled = model.listen
        playButton.image = NSImage(systemSymbolName: model.paused ? "play.fill" : "pause.fill", accessibilityDescription: model.paused ? "Play" : "Pause")
    }

    /// Called every frame for live readouts.
    func tick() {
        let p = model.audio.params
        meter.clipped = p.clipped.value != 0
        p.clipped.value = 0
        meter.update(input: p.inputPeak.value, output: p.outputPeak.value)
        if model.source == .sweep {
            sweepLabel.stringValue = "sweeping  " + formatHz(p.currentFrequency.value)
        }
    }

    @objc private func togglePause() { model.paused.toggle() }

    @objc private func sourceChanged() {
        guard !refreshing, let s = SourceKind(rawValue: sourcePopup.selectedTag()) else { return }
        model.setSource(s, window: window)
    }

    @objc private func openFile() { model.openAudioFile(window: window) }

    @objc private func rateChanged() {
        guard !refreshing else { return }
        model.setSampleRate(LabModel.sampleRates[ratePopup.indexOfSelectedItem])
    }

    @objc private func abChanged() {
        guard !refreshing else { return }
        model.filterOn = abControl.selectedSegment == 1
    }

    @objc private func toggleListen() { model.listen.toggle() }

    @objc private func volumeChanged() { model.volume = volumeSlider.doubleValue }
}

/// A one-line message strip under the top bar (tips, warnings, instability).
final class BannerView: NSView {
    private unowned let model: LabModel
    private let label = makeLabel("", font: Fonts.label, color: Theme.app.text)
    private var actionButton: NSButton!
    private var closeButton: NSButton!
    private(set) var heightConstraint: NSLayoutConstraint!

    init(model: LabModel) {
        self.model = model
        super.init(frame: .zero)
        wantsLayer = true
        label.lineBreakMode = .byTruncatingTail
        label.maximumNumberOfLines = 1
        actionButton = NSButton(title: "", target: self, action: #selector(action))
        actionButton.controlSize = .small
        closeButton = makeIconButton("xmark", tooltip: "Dismiss", target: self, action: #selector(close))
        closeButton.contentTintColor = Theme.app.secondaryText
        for v in [label, actionButton!, closeButton!] {
            v.translatesAutoresizingMaskIntoConstraints = false
            addSubview(v)
        }
        heightConstraint = heightAnchor.constraint(equalToConstant: 0)
        NSLayoutConstraint.activate([
            heightConstraint,
            label.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 16),
            label.centerYAnchor.constraint(equalTo: centerYAnchor),
            actionButton.leadingAnchor.constraint(equalTo: label.trailingAnchor, constant: 12),
            actionButton.centerYAnchor.constraint(equalTo: centerYAnchor),
            closeButton.leadingAnchor.constraint(greaterThanOrEqualTo: actionButton.trailingAnchor, constant: 12),
            closeButton.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -14),
            closeButton.centerYAnchor.constraint(equalTo: centerYAnchor),
        ])
        label.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        model.observe { [weak self] change in
            if change.contains(.banner) { self?.refresh() }
        }
        refresh()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError() }

    func refresh() {
        guard let b = model.banner else {
            heightConstraint.animator().constant = 0
            isHidden = true
            return
        }
        isHidden = false
        heightConstraint.animator().constant = 34
        label.stringValue = b.text
        label.toolTip = b.text
        let color: NSColor, ink: NSColor
        switch b.style {
        case .info: (color, ink) = (NSColor(hex: 0xE6EEF7), NSColor(hex: 0x1F3A5F))
        case .warning: (color, ink) = (NSColor(hex: 0xFBF0D2), NSColor(hex: 0x6B4A12))
        case .danger: (color, ink) = (NSColor(hex: 0xF8DEDA), NSColor(hex: 0x8A2318))
        }
        layer?.backgroundColor = color.cgColor
        label.textColor = ink
        actionButton.isHidden = b.actionTitle == nil
        actionButton.title = b.actionTitle ?? ""
        closeButton.isHidden = b.id == "unstable"
    }

    @objc private func action() { model.performBannerAction() }
    @objc private func close() { model.dismissBanner() }
}
