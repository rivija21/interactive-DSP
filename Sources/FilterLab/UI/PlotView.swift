import AppKit

/// Base class for the plots: flipped coordinates, theme, axes and text helpers.
class PlotView: NSView {
    unowned let model: LabModel
    /// When true the view draws with the light print theme (used for figure export).
    var exporting = false
    var theme: Theme { exporting ? .print : .app }
    var insets = NSEdgeInsets(top: 12, left: 46, bottom: 24, right: 14)

    init(model: LabModel) {
        self.model = model
        super.init(frame: .zero)
        wantsLayer = true
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError() }

    override var isFlipped: Bool { true }
    override var isOpaque: Bool { true }

    var plotRect: CGRect {
        CGRect(x: bounds.minX + insets.left, y: bounds.minY + insets.top,
               width: max(10, bounds.width - insets.left - insets.right),
               height: max(10, bounds.height - insets.top - insets.bottom))
    }

    func fillBackground(_ ctx: CGContext) {
        ctx.setFillColor(theme.plotBackground.cgColor)
        ctx.fill(bounds)
    }

    // MARK: Text

    enum HAlign { case left, center, right }
    enum VAlign { case top, middle, bottom }

    @discardableResult
    func drawText(_ s: String, at p: CGPoint, h: HAlign = .left, v: VAlign = .middle,
                  font: NSFont = Fonts.axis, color: NSColor? = nil) -> CGRect {
        let attrs: [NSAttributedString.Key: Any] = [.font: font, .foregroundColor: color ?? theme.axisText]
        let str = NSAttributedString(string: s, attributes: attrs)
        let size = str.size()
        var o = p
        switch h {
        case .left: break
        case .center: o.x -= size.width / 2
        case .right: o.x -= size.width
        }
        switch v {
        case .top: break
        case .middle: o.y -= size.height / 2
        case .bottom: o.y -= size.height
        }
        str.draw(at: o)
        return CGRect(origin: o, size: size)
    }

    /// A rounded readout box (for hover values).
    func drawReadout(_ lines: [String], near p: CGPoint, in rect: CGRect) {
        let attrs: [NSAttributedString.Key: Any] = [.font: Fonts.readout, .foregroundColor: theme.text]
        let strs = lines.map { NSAttributedString(string: $0, attributes: attrs) }
        let w = (strs.map { $0.size().width }.max() ?? 0) + 14
        let lineH: CGFloat = 15
        let h = CGFloat(lines.count) * lineH + 8
        var o = CGPoint(x: p.x + 12, y: p.y - h - 8)
        if o.x + w > rect.maxX { o.x = p.x - w - 12 }
        if o.y < rect.minY { o.y = p.y + 12 }
        let box = CGRect(x: o.x, y: o.y, width: w, height: h)
        let path = NSBezierPath(roundedRect: box, xRadius: 6, yRadius: 6)
        NSColor(hex: 0xFFFFFF, alpha: 0.96).setFill()
        path.fill()
        theme.panelBorder.setStroke()
        path.lineWidth = 1
        path.stroke()
        for (i, s) in strs.enumerated() {
            s.draw(at: CGPoint(x: box.minX + 7, y: box.minY + 4 + CGFloat(i) * lineH))
        }
    }

    // MARK: Frequency axis

    var fMin: Double { model.logAxis ? 10 : 0 }
    var fMax: Double { model.fs / 2 }

    func x(forFrequency f: Double, in r: CGRect) -> CGFloat {
        if model.logAxis {
            let t = log(max(f, fMin) / fMin) / log(fMax / fMin)
            return r.minX + CGFloat(t) * r.width
        }
        return r.minX + CGFloat((f - fMin) / (fMax - fMin)) * r.width
    }

    func frequency(forX x: CGFloat, in r: CGRect) -> Double {
        let t = Double((x - r.minX) / r.width)
        if model.logAxis { return fMin * pow(fMax / fMin, t) }
        return fMin + t * (fMax - fMin)
    }

    /// Frequencies at evenly spaced x positions across the plot (log or linear).
    func frequencyGrid(count: Int) -> [Double] {
        (0..<count).map { i in
            let t = Double(i) / Double(count - 1)
            return model.logAxis ? fMin * pow(fMax / fMin, t) : max(0.5, fMin + t * (fMax - fMin))
        }
    }

    func drawFrequencyGrid(_ ctx: CGContext, in r: CGRect, labels: Bool = true) {
        var major: [Double] = []
        var minor: [Double] = []
        if model.logAxis {
            var decade = 1.0
            while decade < fMax * 10 {
                for m in 1...9 {
                    let f = decade * Double(m)
                    guard f >= fMin && f <= fMax else { continue }
                    if m == 1 || m == 2 || m == 5 { major.append(f) } else { minor.append(f) }
                }
                decade *= 10
            }
        } else {
            let step = niceStep((fMax - fMin) / 7)
            var f = 0.0
            while f <= fMax + 1e-9 {
                major.append(f)
                minor.append(f + step / 2)
                f += step
            }
        }
        ctx.setLineWidth(1)
        ctx.setStrokeColor(theme.gridMinor.cgColor)
        for f in minor where f <= fMax {
            let x = floor(x(forFrequency: f, in: r)) + 0.5
            ctx.move(to: CGPoint(x: x, y: r.minY))
            ctx.addLine(to: CGPoint(x: x, y: r.maxY))
        }
        ctx.strokePath()
        ctx.setStrokeColor(theme.gridMajor.cgColor)
        for f in major {
            let x = floor(x(forFrequency: f, in: r)) + 0.5
            ctx.move(to: CGPoint(x: x, y: r.minY))
            ctx.addLine(to: CGPoint(x: x, y: r.maxY))
        }
        ctx.strokePath()
        guard labels else { return }
        var lastRight = -CGFloat.infinity
        for f in major {
            let x = x(forFrequency: f, in: r)
            let label = formatHzTick(f)
            let w = (label as NSString).size(withAttributes: [.font: Fonts.axis]).width
            guard x - w / 2 > lastRight + 4, x + w / 2 < bounds.maxX - 2 else { continue }
            drawText(label, at: CGPoint(x: x, y: r.maxY + 4), h: .center, v: .top)
            lastRight = x + w / 2
        }
        drawText("Hz", at: CGPoint(x: r.minX - 12, y: r.maxY + 4), h: .right, v: .top)
    }

    /// Horizontal grid with labels; returns the y mapping.
    func drawValueGrid(_ ctx: CGContext, in r: CGRect, min lo: Double, max hi: Double, step: Double,
                       format: (Double) -> String, rightLabels: ((Double) -> String)? = nil) {
        ctx.setLineWidth(1)
        var v = (lo / step).rounded(.up) * step
        while v <= hi + step * 1e-6 {
            let y = floor(y(forValue: v, min: lo, max: hi, in: r)) + 0.5
            ctx.setStrokeColor((abs(v) < step * 1e-6 ? theme.gridMajor.blended(withFraction: 0.35, of: theme.axisText) ?? theme.gridMajor : theme.gridMajor).cgColor)
            ctx.move(to: CGPoint(x: r.minX, y: y))
            ctx.addLine(to: CGPoint(x: r.maxX, y: y))
            ctx.strokePath()
            drawText(format(v), at: CGPoint(x: r.minX - 5, y: y), h: .right)
            if let rightLabels {
                drawText(rightLabels(v), at: CGPoint(x: r.maxX + 5, y: y), h: .left)
            }
            v += step
        }
    }

    func y(forValue v: Double, min lo: Double, max hi: Double, in r: CGRect) -> CGFloat {
        r.maxY - CGFloat((v - lo) / (hi - lo)) * r.height
    }

    func drawFrame(_ ctx: CGContext, _ r: CGRect) {
        ctx.setStrokeColor(theme.gridMajor.cgColor)
        ctx.setLineWidth(1)
        ctx.stroke(r.insetBy(dx: -0.5, dy: -0.5))
    }

    /// Draws a polyline through points, breaking where values are not finite.
    func strokeCurve(_ ctx: CGContext, xs: [CGFloat], ys: [CGFloat], color: NSColor, width: CGFloat, dash: [CGFloat]? = nil) {
        guard xs.count == ys.count, !xs.isEmpty else { return }
        ctx.saveGState()
        ctx.setStrokeColor(color.cgColor)
        ctx.setLineWidth(width)
        ctx.setLineJoin(.round)
        ctx.setLineCap(.round)
        if let dash { ctx.setLineDash(phase: 0, lengths: dash) }
        var penDown = false
        for i in 0..<xs.count {
            let y = ys[i]
            guard y.isFinite else {
                penDown = false
                continue
            }
            let p = CGPoint(x: xs[i], y: y)
            if penDown { ctx.addLine(to: p) } else { ctx.move(to: p) }
            penDown = true
        }
        ctx.strokePath()
        ctx.restoreGState()
    }

    /// Renders the view at its current size into a PNG, using the light print theme so
    /// the figure drops straight into a lab report.
    func pngData(scale: CGFloat = 2, light: Bool = true) -> Data? {
        exporting = light
        prepareForExport()
        defer {
            exporting = false
            prepareForExport()
            needsDisplay = true
        }
        let size = bounds.size
        guard size.width > 1, size.height > 1,
              let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(size.width * scale), pixelsHigh: Int(size.height * scale),
                                         bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                                         colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0) else { return nil }
        rep.size = size
        cacheDisplay(in: bounds, to: rep)
        return rep.representation(using: .png, properties: [:])
    }

    /// Hook for subclasses to rebuild caches before an off-screen render.
    func prepareForExport() {}
}

/// 1, 2 or 5 × 10ⁿ step close to `raw`.
func niceStep(_ raw: Double) -> Double {
    guard raw > 0, raw.isFinite else { return 1 }
    let p = pow(10, floor(log10(raw)))
    let m = raw / p
    if m < 1.5 { return p }
    if m < 3.5 { return 2 * p }
    if m < 7.5 { return 5 * p }
    return 10 * p
}
