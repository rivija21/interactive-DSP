import AppKit

/// The left sidebar: choose the design method and its parameters.
final class DesignPanel: NSView {
    private unowned let model: LabModel
    private var refreshing = false

    private let stack = NSStackView()
    private var methodControl: NSSegmentedControl!

    // IIR
    private let iirGroup = NSStackView()
    private var iirBand: NSPopUpButton!
    private var family: NSPopUpButton!
    private let order = SliderRow(title: "Order", min: 1, max: 12, scale: .integer, format: { String(format: "%.0f", $0) })
    private let ripple = SliderRow(title: "Passband ripple", min: 0.05, max: 6, scale: .log, format: { String(format: "%.2f dB", $0) },
                                   parse: { Double($0.replacingOccurrences(of: "dB", with: "").trimmingCharacters(in: .whitespaces)) })
    private let stopband = SliderRow(title: "Stopband attenuation", min: 10, max: 120, scale: .integer, format: { String(format: "%.0f dB", $0) },
                                     parse: { Double($0.replacingOccurrences(of: "dB", with: "").trimmingCharacters(in: .whitespaces)) })
    private let iirHint = makeWrappingLabel("")

    // FIR
    private let firGroup = NSStackView()
    private var firBand: NSPopUpButton!
    private var windowPopup: NSPopUpButton!
    private let taps = SliderRow(title: "Taps (length N)", min: 3, max: 255, scale: .integer, format: { String(format: "%.0f", $0) })
    private let beta = SliderRow(title: "Kaiser β", min: 0, max: 16, scale: .linear, format: { String(format: "%.1f", $0) })
    private let firHint = makeWrappingLabel("")

    // Shared cutoff rows (moved between the IIR and FIR groups).
    private let cutoff = SliderRow(title: "Cutoff", min: 10, max: 24000, scale: .log, format: formatHz, parse: parseHz)
    private let edge1 = SliderRow(title: "Low edge f₁", min: 10, max: 24000, scale: .log, format: formatHz, parse: parseHz)
    private let edge2 = SliderRow(title: "High edge f₂", min: 10, max: 24000, scale: .log, format: formatHz, parse: parseHz)
    private let cutoffGroup = NSStackView()

    // Pole–zero
    private let pzGroup = NSStackView()
    private var presetPopup: NSPopUpButton!
    private let presetInfo = makeWrappingLabel("")
    private let pzHelp = makeWrappingLabel("")
    private let selectedTitle = makeLabel("", font: Fonts.smallBold, color: Theme.app.text)
    private let radius = SliderRow(title: "Radius r", min: 0, max: 1.5, scale: .linear, format: { String(format: "%.4f", $0) })
    private let angle = SliderRow(title: "Angle (frequency)", min: 0, max: 24000, scale: .linear, format: formatHz, parse: parseHz)
    private let selectedBox = NSStackView()
    private var deleteButton: NSButton!
    private var clearButton: NSButton!

    // Facts
    private let facts = NSTextField(labelWithString: "")

    init(model: LabModel) {
        self.model = model
        super.init(frame: .zero)
        build()
        model.observe { [weak self] change in
            if !change.isDisjoint(with: [.filter, .zeros, .selection, .sampleRate]) { self?.refresh() }
        }
        refresh()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError() }

    private func section(_ title: String) -> NSTextField {
        let l = NSTextField(labelWithString: "")
        l.attributedStringValue = NSAttributedString(string: title.uppercased(), attributes: [
            .font: Fonts.header, .foregroundColor: Theme.app.secondaryText, .kern: 0.9,
        ])
        return l
    }

    private func vstack(_ s: NSStackView, spacing: CGFloat = 12) {
        s.orientation = .vertical
        s.alignment = .leading
        s.spacing = spacing
    }

    private func fill(_ views: [NSView], in s: NSStackView) {
        for v in views {
            s.addArrangedSubview(v)
            v.translatesAutoresizingMaskIntoConstraints = false
            v.widthAnchor.constraint(equalTo: s.widthAnchor).isActive = true
        }
    }

    private func build() {
        vstack(stack, spacing: 14)
        stack.edgeInsets = NSEdgeInsets(top: 12, left: 14, bottom: 14, right: 14)
        stack.translatesAutoresizingMaskIntoConstraints = false

        methodControl = makeSegmented(DesignMethod.allCases.map(\.title), target: self, action: #selector(methodChanged), size: .regular)
        methodControl.segmentDistribution = .fillEqually

        let bandTitles = BandType.allCases.map(\.title)
        iirBand = makePopup(bandTitles, target: self, action: #selector(bandChanged(_:)), size: .regular)
        firBand = makePopup(bandTitles, target: self, action: #selector(bandChanged(_:)), size: .regular)
        family = makePopup(IIRFamily.allCases.map(\.title), target: self, action: #selector(familyChanged), size: .regular)
        windowPopup = makePopup(WindowType.allCases.map(\.title), target: self, action: #selector(windowChanged), size: .regular)

        order.onChange = { [weak self] v in self?.model.updateSpec { $0.order = Int(v) } }
        ripple.onChange = { [weak self] v in self?.model.updateSpec { $0.rippleDB = v } }
        stopband.onChange = { [weak self] v in self?.model.updateSpec { $0.stopDB = v } }
        taps.onChange = { [weak self] v in
            self?.model.updateSpec { s in
                var n = Int(v)
                if firNeedsOddTaps(s.band) && n % 2 == 0 { n += n > s.taps ? 1 : -1 }
                s.taps = max(3, n)
            }
        }
        beta.onChange = { [weak self] v in self?.model.updateSpec { $0.kaiserBeta = v } }
        cutoff.onChange = { [weak self] v in self?.model.updateSpec { $0.f1 = v } }
        edge1.onChange = { [weak self] v in self?.model.updateSpec { s in s.f1 = min(v, s.f2 / 1.02) } }
        edge2.onChange = { [weak self] v in self?.model.updateSpec { s in s.f2 = max(v, s.f1 * 1.02) } }

        vstack(cutoffGroup, spacing: 12)
        fill([cutoff, edge1, edge2], in: cutoffGroup)

        vstack(iirGroup)
        fill([makeFormRow("Response", iirBand), makeFormRow("Family", family), order, ripple, stopband, iirHint], in: iirGroup)

        vstack(firGroup)
        fill([makeFormRow("Response", firBand), taps, makeFormRow("Window", windowPopup), beta, firHint], in: firGroup)

        presetPopup = NSPopUpButton(frame: .zero, pullsDown: true)
        presetPopup.addItem(withTitle: "Load a preset…")
        for p in PoleZeroPreset.allCases {
            presetPopup.addItem(withTitle: p.title)
            presetPopup.lastItem?.representedObject = p.rawValue
        }
        presetPopup.target = self
        presetPopup.action = #selector(presetChosen)
        pzHelp.stringValue = "Drag poles (×) and zeros (○) in the z-plane. Use + Zero / + Pole above the plot to add; right-click one to delete. Zeros snap onto the unit circle (hold ⌥ to stop snapping)."

        radius.onChange = { [weak self] v in self?.editSelected(radius: v) }
        angle.onChange = { [weak self] v in self?.editSelected(frequency: v) }
        deleteButton = NSButton(title: "Delete", target: self, action: #selector(deleteSelected))
        deleteButton.controlSize = .small
        vstack(selectedBox, spacing: 10)
        let selHeader = NSStackView(views: [selectedTitle, NSView(), deleteButton])
        selHeader.orientation = .horizontal
        fill([selHeader, radius, angle], in: selectedBox)

        clearButton = NSButton(title: "Clear All", target: self, action: #selector(clearAll))
        clearButton.controlSize = .small
        vstack(pzGroup)
        fill([presetPopup, presetInfo, pzHelp, selectedBox, clearButton], in: pzGroup)
        clearButton.widthAnchor.constraint(equalToConstant: 90).isActive = true

        facts.font = Fonts.monoSmall
        facts.textColor = Theme.app.text
        facts.maximumNumberOfLines = 0
        facts.lineBreakMode = .byWordWrapping
        facts.isSelectable = true

        let factsBox = NSStackView()
        vstack(factsBox, spacing: 6)
        fill([section("Filter facts"), facts], in: factsBox)

        fill([section("Design method"), methodControl, iirGroup, firGroup, pzGroup, cutoffGroup, makeSeparator(), factsBox], in: stack)

        let scroll = NSScrollView()
        scroll.drawsBackground = false
        scroll.hasVerticalScroller = true
        scroll.autohidesScrollers = true
        scroll.translatesAutoresizingMaskIntoConstraints = false
        let doc = FlippedView()
        doc.translatesAutoresizingMaskIntoConstraints = false
        doc.addSubview(stack)
        scroll.documentView = doc
        addSubview(scroll)
        NSLayoutConstraint.activate([
            scroll.topAnchor.constraint(equalTo: topAnchor),
            scroll.leadingAnchor.constraint(equalTo: leadingAnchor),
            scroll.trailingAnchor.constraint(equalTo: trailingAnchor),
            scroll.bottomAnchor.constraint(equalTo: bottomAnchor),
            doc.leadingAnchor.constraint(equalTo: scroll.contentView.leadingAnchor),
            doc.trailingAnchor.constraint(equalTo: scroll.contentView.trailingAnchor),
            doc.topAnchor.constraint(equalTo: scroll.contentView.topAnchor),
            stack.topAnchor.constraint(equalTo: doc.topAnchor),
            stack.leadingAnchor.constraint(equalTo: doc.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: doc.trailingAnchor),
            stack.bottomAnchor.constraint(equalTo: doc.bottomAnchor),
        ])
    }

    private func makeSeparator() -> NSView {
        let b = NSBox()
        b.boxType = .custom
        b.borderWidth = 0
        b.fillColor = Theme.app.panelBorder
        b.heightAnchor.constraint(equalToConstant: 1).isActive = true
        return b
    }

    // MARK: Refresh from the model

    func refresh() {
        refreshing = true
        defer { refreshing = false }
        let spec = model.spec
        let fs = model.fs
        methodControl.selectedSegment = DesignMethod.allCases.firstIndex(of: spec.method) ?? 0
        iirGroup.isHidden = spec.method != .iir
        firGroup.isHidden = spec.method != .fir
        pzGroup.isHidden = spec.method != .poleZero
        cutoffGroup.isHidden = spec.method == .poleZero

        let bandIndex = BandType.allCases.firstIndex(of: spec.band) ?? 0
        iirBand.selectItem(at: bandIndex)
        firBand.selectItem(at: bandIndex)
        family.selectItem(at: IIRFamily.allCases.firstIndex(of: spec.family) ?? 0)
        windowPopup.selectItem(at: WindowType.allCases.firstIndex(of: spec.window) ?? 0)

        order.setValue(Double(spec.order))
        order.title = spec.band.isBand ? "Order (×2 for band filters)" : "Order"
        ripple.isHidden = !spec.family.usesPassbandRipple
        stopband.isHidden = !spec.family.usesStopbandAttenuation
        ripple.setValue(spec.rippleDB)
        stopband.setValue(spec.stopDB)
        iirHint.stringValue = iirHintText(spec)

        taps.setValue(Double(spec.taps))
        beta.isHidden = spec.window != .kaiser
        beta.setValue(spec.kaiserBeta)
        firHint.stringValue = firHintText(spec)

        let single = !spec.band.isBand
        cutoff.isHidden = !single
        edge1.isHidden = single
        edge2.isHidden = single
        let meaning = spec.method == .iir ? " (\(spec.family.cutoffMeaning))" : (spec.method == .fir ? " (−6 dB point)" : "")
        cutoff.title = "Cutoff" + meaning
        for row in [cutoff, edge1, edge2] { row.setRange(max: fs / 2 * 0.98) }
        cutoff.setValue(spec.f1)
        edge1.setValue(spec.f1)
        edge2.setValue(spec.f2)

        refreshPoleZero(spec, fs: fs)
        facts.attributedStringValue = factsText()
    }

    private func iirHintText(_ s: DesignSpec) -> String {
        let n = s.band.isBand ? 2 * s.order : s.order
        let slope = 20 * s.order
        switch s.family {
        case .butterworth:
            return "Maximally flat passband. \(n) poles; the response falls by \(slope) dB per decade past the cutoff."
        case .chebyshev1:
            return "Ripple in the passband buys a steeper roll-off than Butterworth at the same order."
        case .chebyshev2:
            return "Flat passband, ripple in the stopband. Zeros sit on the unit circle and put notches in the stopband."
        case .elliptic:
            return "Ripple in both bands gives the sharpest possible transition for a given order."
        case .bessel:
            return "Nearly constant group delay, so waveforms keep their shape. The roll-off is gentle."
        }
    }

    private func firHintText(_ s: DesignSpec) -> String {
        let delay = Double(s.taps - 1) / 2
        var t = String(format: "Linear phase: every frequency is delayed by (N−1)/2 = %g samples (%@).", delay, formatMs(delay / model.fs * 1000))
        if let a = s.window.typicalStopbandDB {
            t += String(format: " %@ window: about %.0f dB of stopband attenuation.", s.window.title, a)
        } else {
            t += String(format: " Kaiser β = %.1f gives about %.0f dB of stopband attenuation.", s.kaiserBeta, kaiserAttenuation(beta: s.kaiserBeta))
        }
        if firNeedsOddTaps(s.band) { t += " High-pass and band-stop need an odd N." }
        return t
    }

    private func refreshPoleZero(_ spec: DesignSpec, fs: Double) {
        if let name = spec.presetName {
            let preset = PoleZeroPreset.allCases.first { $0.title == name }
            presetInfo.stringValue = preset.map { "\(name): \($0.summary)" } ?? name
            presetInfo.isHidden = false
        } else {
            presetInfo.isHidden = true
        }
        angle.setRange(max: fs / 2)
        if let id = model.selectedItem, let item = spec.items.first(where: { $0.id == id }) {
            selectedBox.isHidden = false
            let kind = item.kind == .pole ? "pole" : "zero"
            selectedTitle.stringValue = item.paired ? "Selected \(kind) pair" : "Selected real \(kind)"
            let r = item.paired ? item.position.magnitude : abs(item.position.re)
            radius.setValue(r)
            radius.title = item.paired ? "Radius r" : "Position on the real axis"
            angle.isHidden = !item.paired
            angle.setValue(abs(item.position.phase) / (2 * .pi) * fs)
        } else {
            selectedBox.isHidden = true
        }
        clearButton.isEnabled = !spec.items.isEmpty
    }

    private func factsText() -> NSAttributedString {
        let f = model.filter
        let s = NSMutableAttributedString()
        let key: [NSAttributedString.Key: Any] = [.font: Fonts.monoSmall, .foregroundColor: Theme.app.secondaryText]
        let val: [NSAttributedString.Key: Any] = [.font: Fonts.monoSmall, .foregroundColor: Theme.app.text]
        func line(_ k: String, _ v: String, color: NSColor? = nil) {
            s.append(NSAttributedString(string: k.padding(toLength: 13, withPad: " ", startingAt: 0), attributes: key))
            var a = val
            if let color { a[.foregroundColor] = color }
            s.append(NSAttributedString(string: v + "\n", attributes: a))
        }
        line("Order", "\(f.order)")
        if f.isStable {
            let r = f.zpk.maxPoleRadius
            line("Stability", r > 0 ? String(format: "stable, max |p| = %.4f", r) : "stable (FIR)", color: Theme.app.output)
        } else {
            line("Stability", String(format: "UNSTABLE, |p| = %.3f", f.zpk.maxPoleRadius), color: Theme.app.danger)
        }
        let m = model.measurements
        if !m.minus3dB.isEmpty {
            line("−3 dB at", m.minus3dB.prefix(2).map(formatHz).joined(separator: ", "))
        }
        if f.isStable {
            line("Delay", String(format: "%@ in passband", formatMs(m.groupDelayMs)))
        }
        line("Cost", "\(f.multipliesPerSample) multiplies/sample")
        line("Sample rate", formatHz(model.fs))
        return s
    }

    // MARK: Actions

    @objc private func methodChanged() {
        guard !refreshing else { return }
        let method = DesignMethod.allCases[methodControl.selectedSegment]
        if method == .poleZero && model.spec.items.isEmpty {
            model.convertToPoleZero()
        } else {
            model.updateSpec { $0.method = method }
        }
    }

    @objc private func bandChanged(_ sender: NSPopUpButton) {
        guard !refreshing else { return }
        let band = BandType.allCases[sender.indexOfSelectedItem]
        model.updateSpec { s in
            if band.isBand && !s.band.isBand {
                // Make a sensible band around the old cutoff.
                s.f2 = min(model.fs * 0.45, s.f1 * 3)
            }
            s.band = band
        }
    }

    @objc private func familyChanged() {
        guard !refreshing else { return }
        model.updateSpec { $0.family = IIRFamily.allCases[family.indexOfSelectedItem] }
    }

    @objc private func windowChanged() {
        guard !refreshing else { return }
        model.updateSpec { $0.window = WindowType.allCases[windowPopup.indexOfSelectedItem] }
    }

    @objc private func presetChosen() {
        guard let raw = presetPopup.selectedItem?.representedObject as? String, let p = PoleZeroPreset(rawValue: raw) else { return }
        model.applyPreset(p)
    }

    private func editSelected(radius r: Double? = nil, frequency f: Double? = nil) {
        guard !refreshing, let id = model.selectedItem else { return }
        var items = model.spec.items
        guard let i = items.firstIndex(where: { $0.id == id }) else { return }
        if items[i].paired {
            let curR = r ?? items[i].position.magnitude
            let curA = f.map { 2 * .pi * $0 / model.fs } ?? abs(items[i].position.phase)
            let z = Complex(polar: curR, curA)
            items[i].position = Complex(z.re, abs(z.im))
        } else if let r {
            items[i].position = Complex(items[i].position.re < 0 ? -r : r)
        }
        model.setItems(items)
    }

    @objc private func deleteSelected() { model.deleteSelectedItem() }

    @objc private func clearAll() {
        model.setItems([])
        model.selectedItem = nil
    }
}

extension SliderRow {
    /// Changes the upper end of a frequency slider (it depends on the sample rate).
    func setRange(max newMax: Double) {
        if abs(maxLimit - newMax) > 1e-9 { maxLimit = newMax }
    }
}

final class FlippedView: NSView {
    override var isFlipped: Bool { true }
}
