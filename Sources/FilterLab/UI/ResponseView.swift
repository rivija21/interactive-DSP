import AppKit

/// Frequency response (magnitude / phase / group delay) and time response (impulse / step)
/// of the designed filter. Cutoff markers can be dragged to redesign the filter.
final class ResponseView: PlotView {
    private var dirty = true
    private var xs: [CGFloat] = []
    private var freqs: [Double] = []
    private var values: [Double] = []
    private var range = (lo: -100.0, hi: 5.0, step: 20.0)
    private var magnitudeFloor = -100.0
    private var hover: CGPoint?
    private var dragMarker: Int?
    private var trackingArea: NSTrackingArea?

    override init(model: LabModel) {
        super.init(model: model)
        insets = NSEdgeInsets(top: 22, left: 48, bottom: 26, right: 44)
    }

    func invalidate() {
        dirty = true
        needsDisplay = true
    }

    override func setFrameSize(_ newSize: NSSize) {
        super.setFrameSize(newSize)
        dirty = true
    }

    override func prepareForExport() {
        dirty = true
    }

    private var mode: ResponseMode { model.responseMode }
    private var isTimeMode: Bool { mode == .impulse || mode == .step }

    // MARK: Data

    private func recompute() {
        dirty = false
        let r = plotRect
        let filter = model.filter
        switch mode {
        case .magnitude, .phase, .groupDelay:
            let n = max(200, Int(r.width * 1.5))
            freqs = frequencyGrid(count: n)
            if mode == .magnitude {
                // Narrow notches fall between grid points, so add each unit-circle zero's
                // exact frequency (and its shoulders) to draw the full depth.
                var extra: [Double] = []
                for z in filter.zpk.zeros where abs(z.magnitude - 1) < 1e-3 && z.im >= 0 {
                    let f = abs(z.phase) / (2 * .pi) * model.fs
                    for d in [-0.02, -0.005, 0, 0.005, 0.02] { extra.append(f * (1 + d)) }
                }
                if !extra.isEmpty {
                    freqs = (freqs + extra.filter { $0 > fMin && $0 < fMax }).sorted()
                }
            }
            xs = freqs.map { x(forFrequency: $0, in: r) }
            switch mode {
            case .magnitude:
                values = freqs.map { max(-300, filter.magnitudeDB(at: $0)) }
                updateMagnitudeRange()
            case .phase:
                var last = 0.0, offset = 0.0
                values = freqs.enumerated().map { i, f in
                    var ph = filter.response(at: f).phase
                    if i > 0 {
                        let d = ph + offset - last
                        if d > .pi { offset -= 2 * .pi } else if d < -.pi { offset += 2 * .pi }
                    }
                    ph += offset
                    last = ph
                    return ph * 180 / .pi
                }
                let lo = values.min() ?? -180, hi = values.max() ?? 180
                let span = max(180, hi - lo)
                let steps: [Double] = [15, 30, 45, 90, 180, 360, 720, 1440, 2880, 5760, 11520, 23040, 46080]
                let step = steps.first { $0 >= span / 7 } ?? 92160
                range = ((lo / step).rounded(.down) * step, max((hi / step).rounded(.up) * step, (lo / step).rounded(.down) * step + step), step)
            default:
                values = freqs.map { filter.groupDelay(at: $0) }
                let sorted = values.filter(\.isFinite).sorted()
                let p = { (q: Double) in sorted.isEmpty ? 0 : sorted[min(sorted.count - 1, Int(q * Double(sorted.count - 1)))] }
                var lo = min(0, p(0.01)), hi = max(p(0.985) * 1.2, lo + 2)
                let step = niceStep((hi - lo) / 5)
                lo = (lo / step).rounded(.down) * step
                hi = (hi / step).rounded(.up) * step
                range = (lo, hi, step)
            }
        case .impulse, .step:
            let length = min(filter.suggestedResponseLength(), 1500)
            values = mode == .impulse ? filter.impulseResponse(length: length) : filter.stepResponse(length: length)
            let finite = values.map { $0.isFinite ? $0 : 0 }
            var lo = min(0, finite.min() ?? 0), hi = max(0, finite.max() ?? 1)
            if hi - lo < 1e-9 { hi = lo + 1 }
            let pad = (hi - lo) * 0.08
            let step = niceStep((hi - lo + 2 * pad) / 5)
            lo = ((lo - pad) / step).rounded(.down) * step
            hi = ((hi + pad) / step).rounded(.up) * step
            range = (lo, hi, step)
            let n = values.count
            xs = (0..<n).map { i in r.minX + CGFloat(Double(i) + 0.5) / CGFloat(n) * r.width }
        }
    }

    private func updateMagnitudeRange() {
        let sorted = values.sorted()
        let p10 = sorted.isEmpty ? -60 : sorted[sorted.count / 10]
        let peak = sorted.last ?? 0
        let top = max(5, ((peak + 1) / 5).rounded(.up) * 5)
        var desired = max(-160, min(-40, ((p10 - 10) / 20).rounded(.down) * 20))
        if model.spec.method == .iir && model.spec.family.usesStopbandAttenuation {
            desired = min(desired, -((model.spec.stopDB + 20) / 20).rounded(.up) * 20)
        }
        // Only move the floor for big changes, so the axis doesn't jump while dragging.
        if abs(desired - magnitudeFloor) > 20 || magnitudeFloor > p10 { magnitudeFloor = desired }
        let span = top - magnitudeFloor
        range = (magnitudeFloor, top, span <= 60 ? 10 : 20)
    }

    // MARK: Drawing

    override func draw(_ dirtyRect: NSRect) {
        guard let ctx = NSGraphicsContext.current?.cgContext else { return }
        if dirty { recompute() }
        ctx.setFillColor(theme.panel.cgColor)
        ctx.fill(bounds)
        let r = plotRect
        ctx.setFillColor(theme.plotBackground.cgColor)
        ctx.fill(r)

        if isTimeMode {
            drawTimeResponse(ctx, r)
        } else {
            drawFrequencyResponse(ctx, r)
        }
        drawFrame(ctx, r)
    }

    private func valueLabel(_ v: Double) -> String {
        switch mode {
        case .magnitude: return v == 0 ? "0" : formatSigned(v, "%.0f")
        case .phase: return formatSigned(v, "%.0f") + "°"
        case .groupDelay: return v == v.rounded() ? formatSigned(v, "%.0f") : formatSigned(v, "%.1f")
        default:
            let decimals = max(0, Int(ceil(-log10(abs(range.step)) - 1e-9)))
            let s = "%.\(decimals)f"
            return abs(v) < abs(range.step) * 1e-6 ? "0" : formatSigned(v, s)
        }
    }

    private func drawFrequencyResponse(_ ctx: CGContext, _ r: CGRect) {
        drawFrequencyGrid(ctx, in: r)
        let rightLabels: ((Double) -> String)? = mode == .groupDelay
            ? { v in formatMs(v / self.model.fs * 1000) } : nil
        drawValueGrid(ctx, in: r, min: range.lo, max: range.hi, step: range.step, format: valueLabel, rightLabels: rightLabels)
        let unit: String
        switch mode {
        case .magnitude: unit = "dB"
        case .phase: unit = "deg"
        default: unit = "samples"
        }
        drawText(unit, at: CGPoint(x: r.minX - 5, y: r.minY - 13), h: .right, v: .middle)
        if mode == .groupDelay {
            drawText("time", at: CGPoint(x: r.maxX + 5, y: r.minY - 13), h: .left)
        }

        ctx.saveGState()
        ctx.clip(to: r)
        let color = mode == .phase ? theme.phase : theme.response
        let ys = values.map { v -> CGFloat in
            guard v.isFinite else { return .nan }
            let c = max(range.lo - (range.hi - range.lo), min(range.hi + (range.hi - range.lo), v))
            return y(forValue: c, min: range.lo, max: range.hi, in: r)
        }

        if mode == .magnitude {
            drawMagnitudeGuides(ctx, r)
            // Soft fill under the curve.
            let path = CGMutablePath()
            path.move(to: CGPoint(x: xs.first ?? r.minX, y: r.maxY))
            for (x, y) in zip(xs, ys) where y.isFinite { path.addLine(to: CGPoint(x: x, y: min(y, r.maxY))) }
            path.addLine(to: CGPoint(x: xs.last ?? r.maxX, y: r.maxY))
            path.closeSubpath()
            ctx.addPath(path)
            ctx.setFillColor(color.withAlphaComponent(exporting ? 0.10 : 0.13).cgColor)
            ctx.fillPath()
        }
        strokeCurve(ctx, xs: xs, ys: ys, color: color, width: 2)

        if model.filter.isStable == false && mode == .magnitude {
            drawText("unstable: |H| on the circle no longer describes the output",
                     at: CGPoint(x: r.midX, y: r.minY + 8), h: .center, v: .top, font: Fonts.small, color: theme.danger)
        }
        if model.zerosPending && model.spec.method == .fir && mode == .magnitude {
            // Nothing extra: the curve comes straight from the taps.
        }
        drawSourceMarker(ctx, r)
        ctx.restoreGState()
        drawCutoffMarkers(ctx, r)
        drawHover(ctx, r)
    }

    private func drawMagnitudeGuides(_ ctx: CGContext, _ r: CGRect) {
        let spec = model.spec
        var guides: [(Double, String)] = [(-3.0103, "−3 dB")]
        if spec.method == .iir {
            if spec.family.usesPassbandRipple { guides.append((-spec.rippleDB, "ripple " + formatDB(-spec.rippleDB))) }
            if spec.family.usesStopbandAttenuation { guides.append((-spec.stopDB, "stopband " + formatDB(-spec.stopDB, decimals: 0))) }
        }
        ctx.saveGState()
        ctx.setLineDash(phase: 0, lengths: [4, 4])
        ctx.setLineWidth(1)
        var lastY = -CGFloat.infinity, lastRight = r.minX
        for (db, label) in guides.sorted(by: { $0.0 > $1.0 }) where db > range.lo && db < range.hi {
            let y = y(forValue: db, min: range.lo, max: range.hi, in: r)
            ctx.setStrokeColor(theme.secondaryText.withAlphaComponent(0.45).cgColor)
            ctx.move(to: CGPoint(x: r.minX, y: y))
            ctx.addLine(to: CGPoint(x: r.maxX, y: y))
            ctx.strokePath()
            // Labels sit under their line at the left; close lines share a row.
            let x = abs(y - lastY) < 12 ? lastRight + 10 : r.minX + 6
            let rect = drawText(label, at: CGPoint(x: x, y: y + 2), h: .left, v: .top, color: theme.secondaryText)
            lastY = y
            lastRight = rect.maxX
        }
        ctx.restoreGState()
    }

    /// The frequency currently playing (sine, square or sweep), so you can see where on
    /// the curve the sound you hear sits.
    private func drawSourceMarker(_ ctx: CGContext, _ r: CGRect) {
        guard !exporting, [.sine, .square, .sweep].contains(model.source) else { return }
        let f = model.audio.params.currentFrequency.value
        guard f > fMin, f < fMax else { return }
        let x = x(forFrequency: f, in: r)
        ctx.setStrokeColor(theme.output.withAlphaComponent(0.8).cgColor)
        ctx.setLineWidth(1.5)
        ctx.move(to: CGPoint(x: x, y: r.minY))
        ctx.addLine(to: CGPoint(x: x, y: r.maxY))
        ctx.strokePath()
        if mode == .magnitude {
            let db = model.filter.magnitudeDB(at: f)
            let y = y(forValue: max(range.lo, min(range.hi, db)), min: range.lo, max: range.hi, in: r)
            ctx.setFillColor(theme.output.cgColor)
            ctx.fillEllipse(in: CGRect(x: x - 4, y: y - 4, width: 8, height: 8))
            drawText("♪ " + formatHz(f) + "  " + formatDB(db), at: CGPoint(x: x + 6, y: r.maxY - 6), h: .left, v: .bottom,
                     font: Fonts.smallBold, color: theme.output)
        }
    }

    private var markerFrequencies: [Double] {
        let spec = model.spec
        guard spec.method != .poleZero else { return [] }
        return spec.band.isBand ? [spec.f1, spec.f2] : [spec.f1]
    }

    private func drawCutoffMarkers(_ ctx: CGContext, _ r: CGRect) {
        let markers = markerFrequencies
        for (i, f) in markers.enumerated() {
            let x = x(forFrequency: f, in: r)
            guard x >= r.minX - 1, x <= r.maxX + 1 else { continue }
            let active = dragMarker == i
            ctx.saveGState()
            ctx.setStrokeColor(theme.marker.withAlphaComponent(active ? 0.95 : 0.6).cgColor)
            ctx.setLineWidth(active ? 1.5 : 1)
            ctx.setLineDash(phase: 0, lengths: [2, 3])
            ctx.move(to: CGPoint(x: x, y: r.minY))
            ctx.addLine(to: CGPoint(x: x, y: r.maxY))
            ctx.strokePath()
            ctx.restoreGState()
            if !exporting {
                // Grab handle.
                let handle = CGRect(x: x - 5, y: r.minY - 1, width: 10, height: 12)
                let path = NSBezierPath(roundedRect: handle, xRadius: 3, yRadius: 3)
                theme.marker.withAlphaComponent(active ? 1 : 0.85).setFill()
                path.fill()
            }
            let name = markers.count == 2 ? (i == 0 ? "f₁" : "f₂") : "fc"
            let label = "\(name) \(formatHz(f))"
            let leftSide = x > r.maxX - 90
            drawText(label, at: CGPoint(x: leftSide ? x - 8 : x + 8, y: r.minY + 2), h: leftSide ? .right : .left, v: .top,
                     font: Fonts.smallBold, color: theme.marker)
        }
    }

    private func drawHover(_ ctx: CGContext, _ r: CGRect) {
        guard let hover, r.contains(hover), !exporting, dragMarker == nil else { return }
        let f = frequency(forX: hover.x, in: r)
        ctx.setStrokeColor(theme.cursor.withAlphaComponent(0.35).cgColor)
        ctx.setLineWidth(1)
        ctx.move(to: CGPoint(x: hover.x, y: r.minY))
        ctx.addLine(to: CGPoint(x: hover.x, y: r.maxY))
        ctx.strokePath()
        let filter = model.filter
        var lines = [formatHz(f)]
        var value: Double
        switch mode {
        case .magnitude:
            value = filter.magnitudeDB(at: f)
            lines.append(formatDB(value, decimals: 2))
            let h = filter.response(at: f).magnitude
            lines.append(String(format: "|H| = %.4g", h))
        case .phase:
            let i = nearestIndex(f)
            value = values[i]
            lines.append(formatSigned(value, "%.1f") + "°")
        default:
            value = filter.groupDelay(at: f)
            lines.append(String(format: "%.2f samples", value))
            lines.append(formatMs(value / model.fs * 1000))
        }
        if value.isFinite {
            let y = y(forValue: max(range.lo, min(range.hi, value)), min: range.lo, max: range.hi, in: r)
            ctx.setFillColor((mode == .phase ? theme.phase : theme.response).cgColor)
            ctx.fillEllipse(in: CGRect(x: hover.x - 3.5, y: y - 3.5, width: 7, height: 7))
            drawReadout(lines, near: CGPoint(x: hover.x, y: y), in: r)
        }
    }

    private func nearestIndex(_ f: Double) -> Int {
        guard !freqs.isEmpty else { return 0 }
        var lo = 0, hi = freqs.count - 1
        while hi - lo > 1 {
            let mid = (lo + hi) / 2
            if freqs[mid] < f { lo = mid } else { hi = mid }
        }
        return abs(freqs[lo] - f) < abs(freqs[hi] - f) ? lo : hi
    }

    private func drawTimeResponse(_ ctx: CGContext, _ r: CGRect) {
        let n = values.count
        guard n > 0 else { return }
        // x grid in samples.
        let step = niceStep(Double(n) / 8)
        ctx.setLineWidth(1)
        var k = 0.0
        while k <= Double(n) {
            let x = floor(r.minX + CGFloat(k / Double(n)) * r.width) + 0.5
            ctx.setStrokeColor(theme.gridMajor.cgColor)
            ctx.move(to: CGPoint(x: x, y: r.minY))
            ctx.addLine(to: CGPoint(x: x, y: r.maxY))
            ctx.strokePath()
            drawText(String(format: "%.0f", k), at: CGPoint(x: x, y: r.maxY + 4), h: .center, v: .top)
            k += step
        }
        drawText("n", at: CGPoint(x: r.minX - 6, y: r.maxY + 4), h: .right, v: .top)
        drawText(String(format: "%d samples = %@", n, formatMs(Double(n) / model.fs * 1000)), at: CGPoint(x: r.maxX - 6, y: r.minY + 6), h: .right, v: .top,
                 font: Fonts.small, color: theme.secondaryText)
        drawValueGrid(ctx, in: r, min: range.lo, max: range.hi, step: range.step, format: valueLabel)

        ctx.saveGState()
        ctx.clip(to: r)
        let zeroY = y(forValue: 0, min: range.lo, max: range.hi, in: r)
        let color = model.filter.isStable ? theme.response : theme.danger
        let ys = values.map { y(forValue: $0.isFinite ? $0 : 0, min: range.lo, max: range.hi, in: r) }
        if n <= 160 {
            // Stem plot, as in the textbooks.
            ctx.setStrokeColor(color.withAlphaComponent(0.7).cgColor)
            ctx.setLineWidth(1.2)
            for i in 0..<n {
                ctx.move(to: CGPoint(x: xs[i], y: zeroY))
                ctx.addLine(to: CGPoint(x: xs[i], y: ys[i]))
            }
            ctx.strokePath()
            ctx.setFillColor(color.cgColor)
            let rad: CGFloat = n > 80 ? 2 : 3
            for i in 0..<n {
                ctx.fillEllipse(in: CGRect(x: xs[i] - rad, y: ys[i] - rad, width: rad * 2, height: rad * 2))
            }
        } else {
            strokeCurve(ctx, xs: xs, ys: ys, color: color, width: 1.6)
        }
        ctx.restoreGState()

        if let hover, r.contains(hover), !exporting {
            let i = max(0, min(n - 1, Int((hover.x - r.minX) / r.width * CGFloat(n))))
            ctx.setStrokeColor(theme.cursor.withAlphaComponent(0.35).cgColor)
            ctx.move(to: CGPoint(x: xs[i], y: r.minY))
            ctx.addLine(to: CGPoint(x: xs[i], y: r.maxY))
            ctx.strokePath()
            let name = mode == .impulse ? "h" : "s"
            drawReadout(["\(name)[\(i)] = " + formatSigned(values[i], "%.5f"), formatMs(Double(i) / model.fs * 1000)],
                        near: CGPoint(x: xs[i], y: ys[i]), in: r)
        }
    }

    // MARK: Mouse

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let trackingArea { removeTrackingArea(trackingArea) }
        let area = NSTrackingArea(rect: bounds, options: [.mouseMoved, .mouseEnteredAndExited, .activeInKeyWindow, .cursorUpdate],
                                  owner: self, userInfo: nil)
        addTrackingArea(area)
        trackingArea = area
    }

    private func markerHit(_ p: CGPoint) -> Int? {
        guard !isTimeMode else { return nil }
        let r = plotRect
        guard p.y >= r.minY - 4, p.y <= r.maxY else { return nil }
        for (i, f) in markerFrequencies.enumerated() where abs(x(forFrequency: f, in: r) - p.x) < 6 {
            return i
        }
        return nil
    }

    override func mouseMoved(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        hover = p
        let r = plotRect
        if !isTimeMode && r.contains(p) {
            model.cursorFrequency = frequency(forX: p.x, in: r)
        } else {
            model.cursorFrequency = nil
        }
        if markerHit(p) != nil { NSCursor.resizeLeftRight.set() } else { NSCursor.arrow.set() }
        needsDisplay = true
    }

    override func mouseExited(with event: NSEvent) {
        hover = nil
        model.cursorFrequency = nil
        NSCursor.arrow.set()
        needsDisplay = true
    }

    override func mouseDown(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        dragMarker = markerHit(p)
        if dragMarker == nil, !isTimeMode, plotRect.contains(p), model.spec.method != .poleZero, !model.spec.band.isBand {
            // Click anywhere to move a single cutoff there.
            dragMarker = 0
            mouseDragged(with: event)
        }
        needsDisplay = true
    }

    override func mouseDragged(with event: NSEvent) {
        guard let marker = dragMarker else { return }
        let p = convert(event.locationInWindow, from: nil)
        let r = plotRect
        let f = frequency(forX: min(max(p.x, r.minX), r.maxX), in: r)
        model.updateSpec { s in
            if marker == 0 {
                s.f1 = s.band.isBand ? min(f, s.f2 / 1.02) : f
            } else {
                s.f2 = max(f, s.f1 * 1.02)
            }
        }
        hover = p
        model.cursorFrequency = f
    }

    override func mouseUp(with event: NSEvent) {
        dragMarker = nil
        needsDisplay = true
    }

    override func cursorUpdate(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        if markerHit(p) != nil { NSCursor.resizeLeftRight.set() } else { super.cursorUpdate(with: event) }
    }
}
