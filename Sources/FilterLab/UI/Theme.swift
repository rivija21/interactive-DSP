import AppKit

/// Colours for the on-screen (dark instrument) look and the exported (print) look.
struct Theme {
    var background: NSColor
    var panel: NSColor
    var panelBorder: NSColor
    var plotBackground: NSColor
    var gridMinor: NSColor
    var gridMajor: NSColor
    var axisText: NSColor
    var text: NSColor
    var secondaryText: NSColor
    var output: NSColor
    var input: NSColor
    var response: NSColor
    var phase: NSColor
    var pole: NSColor
    var zero: NSColor
    var unitCircle: NSColor
    var danger: NSColor
    var cursor: NSColor
    var marker: NSColor

    /// The on-screen look: a classic lab notebook. Warm parchment, ivory panels,
    /// graph-paper grids, ink-blue curves and crimson poles.
    static let app = Theme(
        background: NSColor(hex: 0xF3EDE2),
        panel: NSColor(hex: 0xFFFDF8),
        panelBorder: NSColor(hex: 0xE3D9C6),
        plotBackground: NSColor(hex: 0xFFFFFC),
        gridMinor: NSColor(hex: 0xEDF1EA),
        gridMajor: NSColor(hex: 0xD5DFD2),
        axisText: NSColor(hex: 0x7A705F),
        text: NSColor(hex: 0x2B2A33),
        secondaryText: NSColor(hex: 0x7C7364),
        output: NSColor(hex: 0x1C7C6A),
        input: NSColor(hex: 0xA79F92),
        response: NSColor(hex: 0x1F4E9E),
        phase: NSColor(hex: 0x7B3F9E),
        pole: NSColor(hex: 0xC0392B),
        zero: NSColor(hex: 0x2675B8),
        unitCircle: NSColor(hex: 0x3B3A44),
        danger: NSColor(hex: 0xC0392B),
        cursor: NSColor(hex: 0x2B2A33, alpha: 0.7),
        marker: NSColor(hex: 0xC27A1A))

    /// Exported figures: the same palette on pure white paper.
    static let print: Theme = {
        var t = Theme.app
        t.background = .white
        t.panel = .white
        t.plotBackground = .white
        t.gridMinor = NSColor(hex: 0xF0F1F3)
        t.gridMajor = NSColor(hex: 0xD8DBE0)
        t.axisText = NSColor(hex: 0x4B5563)
        t.text = NSColor(hex: 0x111827)
        return t
    }()

    /// Accent for headings in the theory panel.
    static let heading = NSColor(hex: 0xA0472E)
    /// Equations.
    static let ink = NSColor(hex: 0x2A2440)
}

extension NSColor {
    convenience init(hex: UInt32, alpha: CGFloat = 1) {
        self.init(srgbRed: CGFloat((hex >> 16) & 0xFF) / 255, green: CGFloat((hex >> 8) & 0xFF) / 255,
                  blue: CGFloat(hex & 0xFF) / 255, alpha: alpha)
    }
}

enum Fonts {
    static let axis = NSFont.monospacedDigitSystemFont(ofSize: 9.5, weight: .regular)
    static let readout = NSFont.monospacedDigitSystemFont(ofSize: 11, weight: .medium)
    static let small = NSFont.systemFont(ofSize: 11)
    static let smallBold = NSFont.systemFont(ofSize: 11, weight: .semibold)
    static let label = NSFont.systemFont(ofSize: 12)
    static let header = serif(11.5, weight: .semibold)
    static let title = serif(17, weight: .semibold)

    static func serif(_ size: CGFloat, weight: NSFont.Weight = .regular) -> NSFont {
        let base = NSFont.systemFont(ofSize: size, weight: weight)
        if let d = base.fontDescriptor.withDesign(.serif), let f = NSFont(descriptor: d, size: size) { return f }
        return base
    }
    static let mono = NSFont.monospacedSystemFont(ofSize: 11, weight: .regular)
    static let monoSmall = NSFont.monospacedSystemFont(ofSize: 10.5, weight: .regular)
}

// MARK: - Number formatting

private let minus = "\u{2212}"

/// "250 Hz", "1.50 kHz", "12.0 kHz".
func formatHz(_ f: Double) -> String {
    if f >= 10_000 { return String(format: "%.1f kHz", f / 1000) }
    if f >= 1000 { return String(format: "%.2f kHz", f / 1000) }
    if f >= 100 { return String(format: "%.0f Hz", f) }
    if f >= 10 { return String(format: "%.1f Hz", f) }
    return String(format: "%.2f Hz", f)
}

/// Short tick label: "50", "500", "2k", "10k".
func formatHzTick(_ f: Double) -> String {
    if f >= 1000 {
        let k = f / 1000
        return k == k.rounded() ? String(format: "%.0fk", k) : String(format: "%.1fk", k)
    }
    return f == f.rounded() ? String(format: "%.0f", f) : String(format: "%.1f", f)
}

func formatDB(_ db: Double, decimals: Int = 1) -> String {
    let s = String(format: "%.\(decimals)f dB", abs(db))
    return db < -0.00001 ? minus + s : s
}

/// A signed number with a proper minus sign.
func formatSigned(_ v: Double, _ format: String = "%.4f") -> String {
    let s = String(format: format, abs(v))
    return v < 0 ? minus + s : s
}

func formatMs(_ ms: Double) -> String {
    if ms >= 100 { return String(format: "%.0f ms", ms) }
    if ms >= 10 { return String(format: "%.1f ms", ms) }
    return String(format: "%.2f ms", ms)
}

/// Parses "1000", "1k", "1.5 kHz", "800hz".
func parseHz(_ text: String) -> Double? {
    var s = text.lowercased().replacingOccurrences(of: " ", with: "")
    s = s.replacingOccurrences(of: "hz", with: "")
    var mult = 1.0
    if s.hasSuffix("k") {
        mult = 1000
        s.removeLast()
    }
    guard let v = Double(s), v.isFinite, v > 0 else { return nil }
    return v * mult
}
