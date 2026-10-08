import AppKit

/// A rounded instrument panel with a title bar for controls.
final class Panel: NSView {
    let titleLabel = NSTextField(labelWithString: "")
    let controls = NSStackView()
    let body = NSView()
    private let header = NSView()

    init(title: String) {
        super.init(frame: .zero)
        wantsLayer = true
        layer?.backgroundColor = Theme.app.panel.cgColor
        layer?.cornerRadius = 12
        layer?.borderWidth = 1
        layer?.borderColor = Theme.app.panelBorder.cgColor
        layer?.masksToBounds = true

        titleLabel.attributedStringValue = NSAttributedString(string: title.uppercased(), attributes: [
            .font: Fonts.header, .foregroundColor: Theme.app.secondaryText, .kern: 0.9,
        ])
        controls.orientation = .horizontal
        controls.spacing = 8
        controls.alignment = .centerY
        for v in [header, titleLabel, controls, body] { v.translatesAutoresizingMaskIntoConstraints = false }
        addSubview(header)
        header.addSubview(titleLabel)
        header.addSubview(controls)
        addSubview(body)
        let line = NSBox()
        line.boxType = .custom
        line.borderWidth = 0
        line.fillColor = Theme.app.panelBorder
        line.translatesAutoresizingMaskIntoConstraints = false
        addSubview(line)
        NSLayoutConstraint.activate([
            header.topAnchor.constraint(equalTo: topAnchor),
            header.leadingAnchor.constraint(equalTo: leadingAnchor),
            header.trailingAnchor.constraint(equalTo: trailingAnchor),
            header.heightAnchor.constraint(equalToConstant: 32),
            titleLabel.leadingAnchor.constraint(equalTo: header.leadingAnchor, constant: 12),
            titleLabel.centerYAnchor.constraint(equalTo: header.centerYAnchor),
            controls.trailingAnchor.constraint(equalTo: header.trailingAnchor, constant: -8),
            controls.centerYAnchor.constraint(equalTo: header.centerYAnchor),
            controls.leadingAnchor.constraint(greaterThanOrEqualTo: titleLabel.trailingAnchor, constant: 8),
            line.topAnchor.constraint(equalTo: header.bottomAnchor),
            line.leadingAnchor.constraint(equalTo: leadingAnchor),
            line.trailingAnchor.constraint(equalTo: trailingAnchor),
            line.heightAnchor.constraint(equalToConstant: 1),
            body.topAnchor.constraint(equalTo: line.bottomAnchor),
            body.leadingAnchor.constraint(equalTo: leadingAnchor),
            body.trailingAnchor.constraint(equalTo: trailingAnchor),
            body.bottomAnchor.constraint(equalTo: bottomAnchor),
        ])
        titleLabel.setContentCompressionResistancePriority(.required, for: .horizontal)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError() }

    func setBody(_ v: NSView, inset: CGFloat = 0) {
        body.subviews.forEach { $0.removeFromSuperview() }
        v.translatesAutoresizingMaskIntoConstraints = false
        body.addSubview(v)
        NSLayoutConstraint.activate([
            v.topAnchor.constraint(equalTo: body.topAnchor, constant: inset),
            v.leadingAnchor.constraint(equalTo: body.leadingAnchor, constant: inset),
            v.trailingAnchor.constraint(equalTo: body.trailingAnchor, constant: -inset),
            v.bottomAnchor.constraint(equalTo: body.bottomAnchor, constant: -inset),
        ])
    }
}

// MARK: - Small builders

func makeLabel(_ text: String, font: NSFont = Fonts.label, color: NSColor = Theme.app.secondaryText) -> NSTextField {
    let l = NSTextField(labelWithString: text)
    l.font = font
    l.textColor = color
    l.lineBreakMode = .byTruncatingTail
    return l
}

func makeWrappingLabel(_ text: String, font: NSFont = Fonts.small, color: NSColor = Theme.app.secondaryText) -> NSTextField {
    let l = NSTextField(wrappingLabelWithString: text)
    l.font = font
    l.textColor = color
    l.isSelectable = false
    return l
}

func makeSegmented(_ titles: [String], target: AnyObject, action: Selector, size: NSControl.ControlSize = .small) -> NSSegmentedControl {
    let s = NSSegmentedControl(labels: titles, trackingMode: .selectOne, target: target, action: action)
    s.controlSize = size
    s.font = size == .small ? NSFont.systemFont(ofSize: 11) : NSFont.systemFont(ofSize: 12)
    s.segmentStyle = .rounded
    return s
}

func makePopup(_ titles: [String], target: AnyObject, action: Selector, size: NSControl.ControlSize = .small) -> NSPopUpButton {
    let p = NSPopUpButton(frame: .zero, pullsDown: false)
    p.addItems(withTitles: titles)
    p.controlSize = size
    p.font = size == .small ? NSFont.systemFont(ofSize: 11) : NSFont.systemFont(ofSize: 12)
    p.target = target
    p.action = action
    return p
}

func makeIconButton(_ symbol: String, tooltip: String, target: AnyObject, action: Selector) -> NSButton {
    let image = NSImage(systemSymbolName: symbol, accessibilityDescription: tooltip) ?? NSImage()
    let b = NSButton(image: image, target: target, action: action)
    b.bezelStyle = .accessoryBarAction
    b.isBordered = false
    b.toolTip = tooltip
    b.contentTintColor = Theme.app.secondaryText
    b.setButtonType(.momentaryChange)
    return b
}

/// Label, editable value and slider. The slider position can map linearly, on a log scale
/// (frequencies) or to integers.
final class SliderRow: NSView, NSTextFieldDelegate {
    enum Scale { case linear, log, integer }

    let slider = NSSlider()
    let valueField = NSTextField()
    private let titleLabel: NSTextField
    private let scale: Scale
    private let minValue: Double
    var maxLimit: Double
    private let format: (Double) -> String
    private let parse: (String) -> Double?
    var onChange: ((Double) -> Void)?
    private(set) var value: Double = 0

    init(title: String, min: Double, max: Double, scale: Scale, format: @escaping (Double) -> String,
         parse: @escaping (String) -> Double? = { Double($0.trimmingCharacters(in: .whitespaces)) }) {
        titleLabel = makeLabel(title, font: Fonts.label, color: Theme.app.text)
        self.scale = scale
        minValue = min
        maxLimit = max
        self.format = format
        self.parse = parse
        super.init(frame: .zero)
        slider.minValue = 0
        slider.maxValue = 1
        slider.controlSize = .small
        slider.isContinuous = true
        slider.target = self
        slider.action = #selector(sliderMoved)
        valueField.font = Fonts.readout
        valueField.alignment = .right
        valueField.isBordered = false
        valueField.drawsBackground = false
        valueField.textColor = Theme.app.response
        valueField.focusRingType = .none
        valueField.delegate = self
        valueField.target = self
        valueField.action = #selector(fieldEdited)
        valueField.toolTip = "Click to type a value"
        for v in [titleLabel, slider, valueField] {
            v.translatesAutoresizingMaskIntoConstraints = false
            addSubview(v)
        }
        NSLayoutConstraint.activate([
            titleLabel.topAnchor.constraint(equalTo: topAnchor),
            titleLabel.leadingAnchor.constraint(equalTo: leadingAnchor),
            valueField.firstBaselineAnchor.constraint(equalTo: titleLabel.firstBaselineAnchor),
            valueField.trailingAnchor.constraint(equalTo: trailingAnchor),
            valueField.widthAnchor.constraint(equalToConstant: 96),
            valueField.leadingAnchor.constraint(greaterThanOrEqualTo: titleLabel.trailingAnchor, constant: 4),
            slider.topAnchor.constraint(equalTo: titleLabel.bottomAnchor, constant: 3),
            slider.leadingAnchor.constraint(equalTo: leadingAnchor, constant: -1),
            slider.trailingAnchor.constraint(equalTo: trailingAnchor, constant: 1),
            slider.bottomAnchor.constraint(equalTo: bottomAnchor),
        ])
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError() }

    var title: String {
        get { titleLabel.stringValue }
        set { titleLabel.stringValue = newValue }
    }

    private func toSlider(_ v: Double) -> Double {
        switch scale {
        case .log: return log(v / minValue) / log(maxLimit / minValue)
        case .linear, .integer: return (v - minValue) / (maxLimit - minValue)
        }
    }

    private func fromSlider(_ t: Double) -> Double {
        switch scale {
        case .log: return minValue * pow(maxLimit / minValue, t)
        case .linear: return minValue + t * (maxLimit - minValue)
        case .integer: return (minValue + t * (maxLimit - minValue)).rounded()
        }
    }

    func setValue(_ v: Double, max newMax: Double? = nil) {
        value = v
        slider.doubleValue = Swift.max(0, Swift.min(1, toSlider(v)))
        if valueField.currentEditor() == nil { valueField.stringValue = format(v) }
    }

    @objc private func sliderMoved() {
        let v = fromSlider(slider.doubleValue)
        guard v != value else { return }
        value = v
        valueField.stringValue = format(v)
        onChange?(v)
    }

    @objc private func fieldEdited() {
        if let v = parse(valueField.stringValue) {
            let c = Swift.max(minValue, Swift.min(maxLimit, scale == .integer ? v.rounded() : v))
            value = c
            slider.doubleValue = toSlider(c)
            onChange?(c)
        }
        valueField.stringValue = format(value)
        window?.makeFirstResponder(nil)
    }

    func controlTextDidEndEditing(_ obj: Notification) {
        valueField.stringValue = format(value)
    }
}

/// A label on the left, a control on the right.
func makeFormRow(_ title: String, _ control: NSView, labelWidth: CGFloat = 78) -> NSView {
    let row = NSView()
    let label = makeLabel(title, font: Fonts.label, color: Theme.app.text)
    for v in [label, control] {
        v.translatesAutoresizingMaskIntoConstraints = false
        row.addSubview(v)
    }
    NSLayoutConstraint.activate([
        label.leadingAnchor.constraint(equalTo: row.leadingAnchor),
        label.centerYAnchor.constraint(equalTo: control.centerYAnchor),
        label.widthAnchor.constraint(equalToConstant: labelWidth),
        control.leadingAnchor.constraint(equalTo: label.trailingAnchor, constant: 4),
        control.trailingAnchor.constraint(equalTo: row.trailingAnchor),
        control.topAnchor.constraint(equalTo: row.topAnchor),
        control.bottomAnchor.constraint(equalTo: row.bottomAnchor),
    ])
    return row
}

/// Peak meters for input and output with a short hold, like a mixing desk.
final class LevelMeter: NSView {
    private var levels: [Double] = [0, 0]
    private var holds: [Double] = [0, 0]
    var clipped = false

    override var intrinsicContentSize: NSSize { NSSize(width: 120, height: 26) }

    func update(input: Double, output: Double) {
        for (i, v) in [input, output].enumerated() {
            let db = 20 * log10(max(v, 1e-6))
            let t = max(0, min(1, (db + 60) / 60))
            levels[i] = max(t, levels[i] * 0.86)
            holds[i] = max(levels[i], holds[i] - 0.006)
        }
        needsDisplay = true
    }

    override func draw(_ dirtyRect: NSRect) {
        let theme = Theme.app
        for (i, name) in ["IN", "OUT"].enumerated() {
            let y = CGFloat(i) * 13 + 1
            let attrs: [NSAttributedString.Key: Any] = [.font: NSFont.systemFont(ofSize: 8.5, weight: .bold), .foregroundColor: theme.secondaryText]
            NSAttributedString(string: name, attributes: attrs).draw(at: CGPoint(x: 0, y: bounds.height - y - 11))
            let bar = CGRect(x: 26, y: bounds.height - y - 9, width: bounds.width - 26, height: 7)
            NSColor(hex: 0xEDE6D8).setFill()
            NSBezierPath(roundedRect: bar, xRadius: 2, yRadius: 2).fill()
            let w = bar.width * CGFloat(levels[i])
            let color = i == 0 ? theme.input : theme.output
            color.setFill()
            NSBezierPath(roundedRect: CGRect(x: bar.minX, y: bar.minY, width: w, height: bar.height), xRadius: 2, yRadius: 2).fill()
            let hx = bar.minX + bar.width * CGFloat(holds[i])
            (holds[i] > 0.97 || (i == 1 && clipped) ? theme.danger : theme.text).setFill()
            NSRect(x: hx - 1, y: bar.minY, width: 2, height: bar.height).fill()
        }
    }
}
