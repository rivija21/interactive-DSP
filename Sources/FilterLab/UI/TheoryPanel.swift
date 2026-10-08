import AppKit

/// The right-hand panel: the maths of the current filter, short lessons and experiments.
final class TheoryPanel: NSView, NSTextViewDelegate {
    private unowned let model: LabModel
    let pageControl: NSSegmentedControl
    private let textView = NSTextView()
    private let scroll = NSScrollView()
    private var pending = false
    var onCopied: ((String) -> Void)?

    init(model: LabModel) {
        self.model = model
        pageControl = NSSegmentedControl(labels: ["This filter", "Lessons", "Experiments"], trackingMode: .selectOne, target: nil, action: nil)
        super.init(frame: .zero)
        pageControl.controlSize = .small
        pageControl.font = NSFont.systemFont(ofSize: 11)
        pageControl.target = self
        pageControl.action = #selector(pageChanged)

        textView.isEditable = false
        textView.isSelectable = true
        textView.drawsBackground = false
        textView.textContainerInset = NSSize(width: 12, height: 12)
        textView.isVerticallyResizable = true
        textView.isHorizontallyResizable = false
        textView.autoresizingMask = [.width]
        textView.textContainer?.widthTracksTextView = true
        textView.delegate = self
        textView.linkTextAttributes = [.foregroundColor: Theme.app.output, .cursor: NSCursor.pointingHand]
        scroll.documentView = textView
        scroll.drawsBackground = false
        scroll.hasVerticalScroller = true
        scroll.autohidesScrollers = true
        scroll.translatesAutoresizingMaskIntoConstraints = false
        addSubview(scroll)
        NSLayoutConstraint.activate([
            scroll.topAnchor.constraint(equalTo: topAnchor),
            scroll.leadingAnchor.constraint(equalTo: leadingAnchor),
            scroll.trailingAnchor.constraint(equalTo: trailingAnchor),
            scroll.bottomAnchor.constraint(equalTo: bottomAnchor),
        ])
        model.observe { [weak self] change in
            if !change.isDisjoint(with: [.filter, .zeros, .theory, .sampleRate]) { self?.scheduleRefresh(scrollToTop: change.contains(.theory)) }
        }
        refresh(scrollToTop: true)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError() }

    private func scheduleRefresh(scrollToTop: Bool) {
        if scrollToTop {
            refresh(scrollToTop: true)
            return
        }
        guard !pending else { return }
        pending = true
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.12) { [weak self] in
            self?.pending = false
            self?.refresh(scrollToTop: false)
        }
    }

    func refresh(scrollToTop: Bool) {
        let content: NSAttributedString
        switch model.theoryPage {
        case .filter:
            pageControl.selectedSegment = 0
            content = filterPage()
        case .lessons:
            pageControl.selectedSegment = 1
            content = lessonsPage()
        case .lesson(let id):
            pageControl.selectedSegment = 1
            content = lessonPage(id)
        case .experiments:
            pageControl.selectedSegment = 2
            content = experimentsPage()
        }
        let visible = scroll.contentView.bounds.origin
        textView.textStorage?.setAttributedString(content)
        if scrollToTop {
            textView.scroll(.zero)
        } else {
            scroll.contentView.scroll(to: visible)
        }
    }

    @objc private func pageChanged() {
        switch pageControl.selectedSegment {
        case 0: model.theoryPage = .filter
        case 1: model.theoryPage = .lessons
        default: model.theoryPage = .experiments
        }
    }

    func textView(_ textView: NSTextView, clickedOnLink link: Any, at charIndex: Int) -> Bool {
        guard let url = link as? URL ?? (link as? String).flatMap(URL.init(string:)), url.scheme == "lab" else { return false }
        let parts = url.absoluteString.dropFirst(4).split(separator: "/").map(String.init)
        guard let kind = parts.first else { return true }
        let arg = parts.count > 1 ? parts[1] : ""
        switch kind {
        case "lesson": model.theoryPage = .lesson(arg)
        case "page":
            model.theoryPage = arg == "lessons" ? .lessons : (arg == "experiments" ? .experiments : .filter)
        case "try": model.runExperiment(arg)
        case "copy":
            if let lang = CodeLanguage(rawValue: arg) {
                let code = exportCode(lang, spec: model.spec, filter: model.filter)
                NSPasteboard.general.clearContents()
                NSPasteboard.general.setString(code, forType: .string)
                onCopied?("Copied \(lang.title) code to the clipboard")
            }
        default: break
        }
        return true
    }

    // MARK: Pages

    private func filterPage() -> NSAttributedString {
        let spec = model.spec, filter = model.filter, fs = model.fs
        let out = NSMutableAttributedString()
        out.append(T.h1(filterTitle(spec)))
        out.append(T.secondary(filterSubtitle(spec, fs: fs)))
        out.append(T.body(conceptText(spec) + " "))
        out.appendLink(conceptLinkTitle(spec), "lab:lesson/\(conceptLesson(spec))", newline: true)

        // Transfer function.
        out.append(T.h2("Transfer function"))
        switch filter.structure {
        case .iir(let sos):
            if sos.count == 1 {
                out.append(T.code(fractionString(sos[0], label: "H(z)")))
            } else {
                out.append(T.math("H(z) = H₁(z) · H₂(z) ⋯ H\(subscriptDigits(sos.count))(z)"))
                out.append(T.math("Hₖ(z) = (b₀ + b₁z⁻¹ + b₂z⁻²) / (1 + a₁z⁻¹ + a₂z⁻²)"))
                out.append(T.secondary("A cascade of \(sos.count) second-order sections (biquads), which is how the audio is actually processed:"))
                for (i, s) in sos.prefix(16).enumerated() {
                    out.append(T.code(fractionString(s, label: "H\(subscriptDigits(i + 1))")))
                }
                if sos.count > 16 { out.append(T.secondary("… and \(sos.count - 16) more sections")) }
            }
        case .fir(let h):
            out.append(T.math("H(z) = Σₖ h[k] z^{−k},  k = 0 … \(h.count - 1)"))
            let shown = h.prefix(6).map { fmt($0) }.joined(separator: ", ")
            out.append(T.code("h = [" + shown + (h.count > 6 ? ", …]" : "]")))
            out.append(T.secondary("Symmetric taps, h[k] = h[N−1−k], so the phase is exactly linear."))
        }

        // Difference equation.
        out.append(T.h2("Difference equation"))
        switch filter.structure {
        case .iir(let sos):
            let (b, a) = sosToTransferFunction(sos)
            if b.count <= 5 && a.count <= 5 {
                var eq = "y[n] = "
                var first = true
                for (k, bk) in b.enumerated() where bk != 0 {
                    eq += term(bk, "x[n\(k == 0 ? "" : "−\(k)")]", first: first)
                    first = false
                }
                for (k, ak) in a.enumerated().dropFirst() where ak != 0 {
                    eq += term(-ak, "y[n−\(k)]", first: first)
                    first = false
                }
                out.append(T.code(eq))
                out.append(T.secondary("Feedback terms (y[n−k]) are what make it IIR: the output depends on its own past."))
            } else {
                out.append(T.code("y = b₀·x + s₁\ns₁ ← b₁·x − a₁·y + s₂\ns₂ ← b₂·x − a₂·y"))
                out.append(T.secondary("Each section runs in transposed direct form II, and its output feeds the next. A single high-order polynomial would be numerically fragile, so the filter is never expanded into one."))
            }
        case .fir(let h):
            out.append(T.code("y[n] = Σₖ h[k]·x[n−k]   (\(h.count) multiplies per sample)"))
            out.append(T.secondary("No feedback, so it is always stable. The cost is length: sharp FIR filters need many taps."))
        }

        // Poles and zeros.
        out.append(T.h2("Poles and zeros"))
        if model.zerosPending {
            out.append(T.secondary("Finding the zeros of the tap polynomial…"))
        } else {
            out.append(T.code(rootTable(filter.zpk, fs: fs)))
            if filter.isStable {
                let r = filter.zpk.maxPoleRadius
                out.append(T.secondary(r > 0
                    ? String(format: "All poles are inside the unit circle (largest |p| = %.4f), so the filter is stable. It rings for about %.0f samples.", r, 1 / max(1e-9, 1 - r))
                    : "All poles are at the origin, so the filter is stable."))
            } else {
                out.append(T.body("A pole is on or outside the unit circle, so this filter is unstable.", color: Theme.app.danger))
            }
        }

        out.append(T.h2("Use it in your coursework"))
        out.append(T.secondary("Copy code that designs this filter, with the exact coefficients."))
        let line = NSMutableAttributedString()
        for (i, lang) in CodeLanguage.allCases.enumerated() {
            if i > 0 { line.append(T.inline("   ·   ", color: Theme.app.secondaryText)) }
            line.append(T.link("Copy " + lang.title, "lab:copy/\(lang.rawValue)"))
        }
        line.append(T.inline("\n"))
        out.append(line)
        return out
    }

    private func conceptLesson(_ s: DesignSpec) -> String {
        switch s.method {
        case .iir: return s.family == .bessel ? "phase" : "families"
        case .fir: return "fir"
        case .poleZero: return "zplane"
        }
    }

    private func conceptLinkTitle(_ s: DesignSpec) -> String {
        Lessons.lesson(conceptLesson(s)).map { "Lesson: \($0.title) →" } ?? ""
    }

    private func conceptText(_ s: DesignSpec) -> String {
        switch s.method {
        case .iir:
            let base = "Designed as an analog \(s.family.title) prototype, shifted to the \(s.band.title.lowercased()) band, then mapped to the z-plane with the bilinear transform."
            switch s.family {
            case .butterworth: return base + " Butterworth is as flat as possible in the passband."
            case .chebyshev1: return base + " Chebyshev I trades passband ripple for steepness."
            case .chebyshev2: return base + " Chebyshev II keeps the passband flat and puts its ripple, and its zeros, in the stopband."
            case .elliptic: return base + " Elliptic ripples in both bands to get the sharpest cut."
            case .bessel: return base + " Bessel keeps the group delay as flat as possible."
            }
        case .fir:
            return "A truncated ideal (sinc) impulse response, smoothed by a \(s.window.title) window."
        case .poleZero:
            if let p = PoleZeroPreset.allCases.first(where: { $0.title == s.presetName }) { return p.summary }
            return "Every pole and zero was placed by hand. |H| at each frequency is the product of the distances to the zeros divided by the product of the distances to the poles."
        }
    }

    /// A section written as a stacked fraction, textbook style, with the numerator's
    /// leading coefficient pulled out as a gain so the lines stay short.
    private func fractionString(_ s: Biquad, label: String) -> String {
        var b = [s.b0, s.b1, s.b2]
        var gain = 1.0
        if let lead = b.first(where: { $0 != 0 }), abs(lead - 1) > 1e-12 {
            gain = lead
            b = b.map { $0 / lead }
        }
        let num = poly(b, leadingOne: false)
        let den = poly([1, s.a1, s.a2], leadingOne: true)
        let width = max(num.count, den.count)
        let head = label + " = " + (gain == 1 ? "" : fmt(gain) + " · ")
        let pad = String(repeating: " ", count: head.count)
        func centred(_ t: String) -> String {
            String(repeating: " ", count: (width - t.count) / 2) + t
        }
        return pad + centred(num) + "\n" + head + String(repeating: "─", count: width) + "\n" + pad + centred(den)
    }

    private func sectionString(_ s: Biquad) -> String {
        let num = poly([s.b0, s.b1, s.b2], leadingOne: false)
        let den = poly([1, s.a1, s.a2], leadingOne: true)
        return "(\(num)) / (\(den))"
    }

    private func poly(_ c: [Double], leadingOne: Bool) -> String {
        var s = ""
        for (k, v) in c.enumerated() {
            if v == 0 && !(k == 0) { continue }
            let power = k == 0 ? "" : (k == 1 ? "z⁻¹" : "z⁻²")
            let coefficient = abs(abs(v) - 1) < 1e-12 && k > 0 ? "" : fmt(abs(v)) + " "
            if s.isEmpty {
                s = (k == 0 && leadingOne ? "1" : fmt(v)) + (power.isEmpty ? "" : " " + power)
            } else {
                s += (v < 0 ? " − " : " + ") + coefficient + power
            }
        }
        return s.isEmpty ? "0" : s
    }

    private func term(_ v: Double, _ name: String, first: Bool) -> String {
        if first { return fmt(v) + "·" + name }
        return (v < 0 ? " − " : " + ") + fmt(abs(v)) + "·" + name
    }

    private func rootTable(_ zpk: ZPK, fs: Double) -> String {
        var lines: [String] = []
        for (kind, roots) in [("×  pole", zpk.poles), ("○  zero", zpk.zeros)] {
            var groups: [(Complex, Int)] = []
            for r in roots where r.im >= -1e-12 || r.isReal(tolerance: 1e-9) {
                if let i = groups.firstIndex(where: { ($0.0 - r).magnitude < 1e-6 * max(1, r.magnitude) }) {
                    groups[i].1 += 1
                } else {
                    groups.append((r, 1))
                }
            }
            groups.sort { $0.0.phase < $1.0.phase }
            let limit = 10
            for (z, n) in groups.prefix(limit) {
                let mult = n > 1 ? " ×\(n)" : ""
                if z.isReal(tolerance: 1e-9) {
                    let note = z.magnitude < 1e-12 ? " (origin)" : (abs(z.re + 1) < 1e-9 ? " (Nyquist)" : (abs(z.re - 1) < 1e-9 ? " (DC)" : ""))
                    lines.append("\(kind)  z = \(formatSigned(z.re, "%.5f"))\(mult)\(note)")
                } else {
                    let f = abs(z.phase) / (2 * .pi) * fs
                    lines.append(String(format: "%@  r = %.5f  ±%6.2f°  %@%@", kind, z.magnitude, abs(z.phase) * 180 / .pi, formatHz(f), mult))
                }
            }
            if groups.count > limit { lines.append("   … \(groups.count - limit) more \(kind.dropFirst(3))s") }
        }
        return lines.isEmpty ? "No poles or zeros: H(z) is a constant." : lines.joined(separator: "\n")
    }

    private func lessonsPage() -> NSAttributedString {
        let out = NSMutableAttributedString()
        out.append(T.h1("Lessons"))
        out.append(T.secondary("Short explanations of what you're seeing. Each one links to experiments you can run."))
        for lesson in Lessons.all {
            let line = NSMutableAttributedString()
            line.append(T.link(lesson.title, "lab:lesson/\(lesson.id)", bold: true))
            line.append(T.inline("\n"))
            out.append(T.spaced(line, before: 10))
            out.append(T.secondary(lesson.summary))
        }
        return out
    }

    private func lessonPage(_ id: String) -> NSAttributedString {
        let out = NSMutableAttributedString()
        guard let lesson = Lessons.lesson(id) else { return lessonsPage() }
        out.appendLink("← All lessons", "lab:page/lessons", newline: true)
        out.append(T.h1(lesson.title))
        for p in lesson.body {
            if p.hasPrefix("• ") {
                out.append(T.bullet(String(p.dropFirst(2))))
            } else if p.hasPrefix("= ") {
                out.append(T.math(String(p.dropFirst(2))))
            } else if p.hasPrefix("Try: ") {
                out.append(T.body("Try it: " + p.dropFirst(5), color: Theme.app.output))
            } else {
                out.append(T.body(p))
            }
        }
        if !lesson.experiments.isEmpty {
            out.append(T.h2("Experiments"))
            for e in lesson.experiments.compactMap(Experiments.experiment) {
                let line = NSMutableAttributedString()
                line.append(T.link("▶ " + e.title, "lab:try/\(e.id)"))
                line.append(T.inline("\n"))
                out.append(T.spaced(line, before: 4))
            }
        }
        return out
    }

    private func experimentsPage() -> NSAttributedString {
        let out = NSMutableAttributedString()
        out.append(T.h1("Experiments"))
        out.append(T.secondary("Each one sets up the source, the filter and the displays in one click. Turn the volume down first."))
        for e in Experiments.all {
            let line = NSMutableAttributedString()
            let running = model.activeExperiment == e.id
            line.append(T.inline(e.title, font: Fonts.smallBold.withSize(12.5), color: running ? Theme.app.output : Theme.app.text))
            line.append(T.inline("   "))
            line.append(T.link(running ? "● running, set up again" : "Set it up ▸", "lab:try/\(e.id)"))
            line.append(T.inline("\n"))
            out.append(T.spaced(line, before: 12))
            out.append(T.secondary(e.description))
        }
        return out
    }
}

// MARK: - Number formatting for equations

/// Six significant digits; scientific numbers as 2.1×10⁻⁵.
func fmt(_ v: Double) -> String {
    if v == 0 { return "0" }
    let a = abs(v)
    let sign = v < 0 ? "−" : ""
    if a >= 1e-3 && a < 1e5 {
        var s = String(format: "%.5g", a)
        if s.contains("e") { s = String(format: "%.5f", a) }
        return sign + s
    }
    let e = Int(floor(log10(a)))
    let m = a / pow(10, Double(e))
    return sign + String(format: "%.4g", m) + "×10" + superscript(e)
}

func subscriptDigits(_ n: Int) -> String {
    let map: [Character: Character] = ["0": "₀", "1": "₁", "2": "₂", "3": "₃", "4": "₄", "5": "₅", "6": "₆", "7": "₇", "8": "₈", "9": "₉"]
    return String(String(n).map { map[$0] ?? $0 })
}

func superscript(_ n: Int) -> String {
    let map: [Character: Character] = ["0": "⁰", "1": "¹", "2": "²", "3": "³", "4": "⁴", "5": "⁵", "6": "⁶", "7": "⁷", "8": "⁸", "9": "⁹", "-": "⁻"]
    return String(String(n).map { map[$0] ?? $0 })
}

// MARK: - Text styles

private enum T {
    static let mathFont = Fonts.serif(13.5)

    static func paragraph(before: CGFloat = 0, after: CGFloat = 6, indent: CGFloat = 0, head: CGFloat? = nil) -> NSParagraphStyle {
        let p = NSMutableParagraphStyle()
        p.paragraphSpacingBefore = before
        p.paragraphSpacing = after
        p.lineSpacing = 2
        p.firstLineHeadIndent = indent
        p.headIndent = head ?? indent
        return p
    }

    static func h1(_ s: String) -> NSAttributedString {
        NSAttributedString(string: s + "\n", attributes: [
            .font: Fonts.title, .foregroundColor: Theme.app.text,
            .paragraphStyle: paragraph(after: 4),
        ])
    }

    static func h2(_ s: String) -> NSAttributedString {
        NSAttributedString(string: s.uppercased() + "\n", attributes: [
            .font: Fonts.header, .foregroundColor: Theme.heading, .kern: 0.9,
            .paragraphStyle: paragraph(before: 12, after: 6),
        ])
    }

    static func body(_ s: String, color: NSColor = Theme.app.text) -> NSAttributedString {
        rich(s + "\n", font: NSFont.systemFont(ofSize: 12.5), color: color, style: paragraph(after: 8))
    }

    static func secondary(_ s: String) -> NSAttributedString {
        rich(s + "\n", font: NSFont.systemFont(ofSize: 11.5), color: Theme.app.secondaryText, style: paragraph(after: 8))
    }

    static func bullet(_ s: String) -> NSAttributedString {
        let p = NSMutableParagraphStyle()
        p.paragraphSpacing = 6
        p.lineSpacing = 2
        p.firstLineHeadIndent = 2
        p.headIndent = 14
        p.tabStops = [NSTextTab(textAlignment: .left, location: 14)]
        return rich("•\t" + s + "\n", font: NSFont.systemFont(ofSize: 12.5), color: Theme.app.text, style: p)
    }

    static func math(_ s: String) -> NSAttributedString {
        rich(markup(s) + "\n", font: mathFont, color: Theme.ink, style: paragraph(before: 2, after: 10, indent: 10))
    }

    /// The serif font has no Unicode super/subscript glyphs, so turn z⁻¹ into z^{−1}
    /// and b₀ into b_{0}, which `rich` draws with a raised or lowered baseline.
    static func markup(_ s: String) -> String {
        let sup: [Character: Character] = ["⁰": "0", "¹": "1", "²": "2", "³": "3", "⁴": "4", "⁵": "5", "⁶": "6", "⁷": "7", "⁸": "8", "⁹": "9", "⁻": "−", "ⁿ": "n"]
        let sub: [Character: Character] = ["₀": "0", "₁": "1", "₂": "2", "₃": "3", "₄": "4", "₅": "5", "₆": "6", "₇": "7", "₈": "8", "₉": "9", "ₖ": "k", "ᵢ": "i"]
        var out = ""
        var run = ""
        var mode: Character? = nil
        func flush() {
            if let m = mode, !run.isEmpty { out += "\(m){\(run)}" }
            run = ""
            mode = nil
        }
        for c in s {
            if let v = sup[c] {
                if mode != "^" { flush(); mode = "^" }
                run.append(v)
            } else if let v = sub[c] {
                if mode != "_" { flush(); mode = "_" }
                run.append(v)
            } else {
                flush()
                out.append(c)
            }
        }
        flush()
        return out
    }

    static func code(_ s: String) -> NSAttributedString {
        rich(s + "\n", font: Fonts.monoSmall, color: Theme.app.text, style: paragraph(after: 4, indent: 4))
    }

    static func inline(_ s: String, font: NSFont = NSFont.systemFont(ofSize: 12.5), color: NSColor = Theme.app.text) -> NSAttributedString {
        NSAttributedString(string: s, attributes: [.font: font, .foregroundColor: color])
    }

    static func link(_ title: String, _ url: String, bold: Bool = false) -> NSAttributedString {
        NSAttributedString(string: title, attributes: [
            .font: bold ? NSFont.systemFont(ofSize: 13, weight: .semibold) : NSFont.systemFont(ofSize: 12.5, weight: .medium),
            .link: URL(string: url) as Any, .foregroundColor: Theme.app.output,
        ])
    }

    static func spaced(_ s: NSAttributedString, before: CGFloat) -> NSAttributedString {
        let m = NSMutableAttributedString(attributedString: s)
        m.addAttribute(.paragraphStyle, value: paragraph(before: before, after: 2), range: NSRange(location: 0, length: m.length))
        return m
    }

    /// Supports x^{sup} and x_{sub} markup.
    static func rich(_ s: String, font: NSFont, color: NSColor, style: NSParagraphStyle) -> NSAttributedString {
        let out = NSMutableAttributedString()
        let base: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: color, .paragraphStyle: style]
        var i = s.startIndex
        var plain = ""
        while i < s.endIndex {
            let c = s[i]
            let next = s.index(after: i)
            if (c == "^" || c == "_"), next < s.endIndex, s[next] == "{", let close = s[next...].firstIndex(of: "}") {
                out.append(NSAttributedString(string: plain, attributes: base))
                plain = ""
                let inner = String(s[s.index(after: next)..<close])
                var a = base
                a[.font] = NSFont(descriptor: font.fontDescriptor, size: font.pointSize * 0.72) ?? font
                a[.baselineOffset] = c == "^" ? font.pointSize * 0.38 : -font.pointSize * 0.18
                out.append(NSAttributedString(string: inner, attributes: a))
                i = s.index(after: close)
                continue
            }
            plain.append(c)
            i = next
        }
        out.append(NSAttributedString(string: plain, attributes: base))
        return out
    }
}

private extension NSMutableAttributedString {
    func appendLink(_ title: String, _ url: String, newline: Bool) {
        append(T.link(title, url))
        if newline { append(T.inline("\n")) }
    }
}
