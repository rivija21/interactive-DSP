import AppKit

/// The z-plane: unit circle, poles (×) and zeros (○), an optional |H(z)| heat map, and the
/// geometric link between a frequency and its point e^{jω} on the circle.
final class ZPlaneView: PlotView {
    private var zoom: Double = 1
    private var center = Complex.zero
    private var autoRange = 1.3
    private var heat: CGImage?
    private var heatDirty = true
    private var heatSize: CGFloat = 0
    private var heatGeneration = 0
    private var hover: CGPoint?
    private var trackingArea: NSTrackingArea?

    private enum DragTarget {
        case item(UUID, conjugate: Bool)
        case root(Complex, PZItem.Kind)
        case pan(start: CGPoint, center: Complex)
    }
    private var drag: DragTarget?
    private var dragMoved = false
    private var mouseDownPoint = CGPoint.zero

    override init(model: LabModel) {
        super.init(model: model)
        insets = NSEdgeInsets(top: 8, left: 8, bottom: 8, right: 8)
    }

    func invalidate() {
        updateAutoRange()
        heatDirty = true
        needsDisplay = true
    }

    func resetView() {
        zoom = 1
        center = .zero
        heatDirty = true
        needsDisplay = true
    }

    // MARK: Geometry

    private var range: Double { autoRange / zoom }

    /// Square drawing area.
    private var square: CGRect {
        let r = plotRect
        let side = min(r.width, r.height)
        return CGRect(x: r.midX - side / 2, y: r.midY - side / 2, width: side, height: side)
    }

    private func point(_ z: Complex) -> CGPoint {
        let s = square
        let scale = s.width / CGFloat(2 * range)
        return CGPoint(x: s.midX + CGFloat(z.re - center.re) * scale, y: s.midY - CGFloat(z.im - center.im) * scale)
    }

    private func complex(_ p: CGPoint) -> Complex {
        let s = square
        let scale = Double(s.width) / (2 * range)
        return Complex(Double(p.x - s.midX) / scale + center.re, -Double(p.y - s.midY) / scale + center.im)
    }

    private var pixelsPerUnit: CGFloat { square.width / CGFloat(2 * range) }

    private func updateAutoRange() {
        let roots = model.filter.zpk.zeros + model.filter.zpk.poles
        let far = roots.map(\.magnitude).filter { $0.isFinite }.max() ?? 0
        autoRange = min(4, max(1.3, far * 1.12))
    }

    // MARK: Roots to draw

    private struct Mark {
        var z: Complex
        var kind: PZItem.Kind
        var count: Int
        var itemID: UUID?
        var conjugate: Bool
    }

    private func marks() -> [Mark] {
        var result: [Mark] = []
        if model.spec.method == .poleZero {
            for item in model.spec.items {
                result.append(Mark(z: item.position, kind: item.kind, count: 1, itemID: item.id, conjugate: false))
                if item.paired {
                    result.append(Mark(z: item.position.conj, kind: item.kind, count: 1, itemID: item.id, conjugate: true))
                }
            }
            // Origin roots added to keep the filter causal.
            let originZeros = model.filter.zpk.zeros.filter { $0.magnitude < 1e-12 }.count
                - model.spec.items.filter { $0.kind == .zero && $0.position.magnitude < 1e-12 }.reduce(0) { $0 + ($1.paired ? 2 : 1) }
            let originPoles = model.filter.zpk.poles.filter { $0.magnitude < 1e-12 }.count
                - model.spec.items.filter { $0.kind == .pole && $0.position.magnitude < 1e-12 }.reduce(0) { $0 + ($1.paired ? 2 : 1) }
            if originZeros > 0 { result.append(Mark(z: .zero, kind: .zero, count: originZeros, itemID: nil, conjugate: false)) }
            if originPoles > 0 { result.append(Mark(z: .zero, kind: .pole, count: originPoles, itemID: nil, conjugate: false)) }
            return result
        }
        // Designed filters: group coincident roots and show their multiplicity.
        for (kind, roots) in [(PZItem.Kind.zero, model.filter.zpk.zeros), (.pole, model.filter.zpk.poles)] {
            var groups: [(Complex, Int)] = []
            for r in roots {
                if let i = groups.firstIndex(where: { ($0.0 - r).magnitude < 1e-6 * max(1, r.magnitude) }) {
                    groups[i].1 += 1
                } else {
                    groups.append((r, 1))
                }
            }
            for (z, n) in groups { result.append(Mark(z: z, kind: kind, count: n, itemID: nil, conjugate: false)) }
        }
        return result
    }

    // MARK: Drawing

    override func draw(_ dirtyRect: NSRect) {
        guard let ctx = NSGraphicsContext.current?.cgContext else { return }
        ctx.setFillColor(theme.panel.cgColor)
        ctx.fill(bounds)
        let s = square
        ctx.saveGState()
        ctx.addPath(CGPath(roundedRect: s, cornerWidth: 6, cornerHeight: 6, transform: nil))
        ctx.clip()
        ctx.setFillColor(theme.plotBackground.cgColor)
        ctx.fill(s)

        if model.showHMap && heatMapAvailable {
            requestHeatMap()
            if let heat {
                ctx.saveGState()
                ctx.interpolationQuality = .medium
                ctx.translateBy(x: s.minX, y: s.maxY)
                ctx.scaleBy(x: 1, y: -1)
                ctx.draw(heat, in: CGRect(x: 0, y: 0, width: s.width, height: s.height))
                ctx.restoreGState()
            }
        }
        drawGrid(ctx, s)
        drawCursorLink(ctx)
        drawSourceDot(ctx)
        let all = marks()
        for m in all { drawMark(ctx, m) }
        ctx.restoreGState()

        ctx.setStrokeColor(theme.gridMajor.cgColor)
        ctx.setLineWidth(1)
        ctx.addPath(CGPath(roundedRect: s.insetBy(dx: 0.5, dy: 0.5), cornerWidth: 6, cornerHeight: 6, transform: nil))
        ctx.strokePath()

        if model.showHMap && !heatMapAvailable && !exporting {
            drawText("|H| map: not shown for FIR", at: CGPoint(x: s.minX + 8, y: s.maxY - 6), h: .left, v: .bottom,
                     font: Fonts.small, color: theme.secondaryText)
        }
        drawHoverInfo(ctx, all)
        if zoom != 1 || center != .zero, !exporting {
            drawText(String(format: "zoom ×%.1f", zoom), at: CGPoint(x: s.minX + 8, y: s.maxY - 6), h: .left, v: .bottom,
                     color: theme.secondaryText)
        }
    }

    private func drawGrid(_ ctx: CGContext, _ s: CGRect) {
        let o = point(.zero)
        let ppu = pixelsPerUnit
        // Axes.
        ctx.setStrokeColor(theme.gridMajor.cgColor)
        ctx.setLineWidth(1)
        ctx.move(to: CGPoint(x: s.minX, y: o.y))
        ctx.addLine(to: CGPoint(x: s.maxX, y: o.y))
        ctx.move(to: CGPoint(x: o.x, y: s.minY))
        ctx.addLine(to: CGPoint(x: o.x, y: s.maxY))
        ctx.strokePath()
        // Faint rings and spokes.
        ctx.setStrokeColor(theme.gridMinor.cgColor)
        for r in [0.25, 0.5, 0.75] {
            ctx.addEllipse(in: CGRect(x: o.x - CGFloat(r) * ppu, y: o.y - CGFloat(r) * ppu, width: CGFloat(2 * r) * ppu, height: CGFloat(2 * r) * ppu))
        }
        ctx.strokePath()
        for k in 1..<8 where k != 4 {
            let a = Double(k) * .pi / 8
            ctx.move(to: o)
            ctx.addLine(to: point(Complex(polar: 1, a)))
            ctx.move(to: o)
            ctx.addLine(to: point(Complex(polar: 1, -a)))
        }
        ctx.strokePath()
        // Unit circle.
        ctx.setStrokeColor(theme.unitCircle.withAlphaComponent(0.9).cgColor)
        ctx.setLineWidth(1.6)
        ctx.addEllipse(in: CGRect(x: o.x - ppu, y: o.y - ppu, width: 2 * ppu, height: 2 * ppu))
        ctx.strokePath()

        // Frequency labels around the upper half of the circle. 0 Hz and fs/2 sit just
        // inside the circle under the real axis so they never fall off the plot.
        let fs = model.fs
        for k in 0...4 {
            let a = Double(k) * .pi / 4
            let f = fs * Double(k) / 8
            let p = point(Complex(polar: 1, a))
            ctx.setStrokeColor(theme.unitCircle.withAlphaComponent(0.7).cgColor)
            ctx.setLineWidth(1)
            ctx.move(to: p)
            ctx.addLine(to: point(Complex(polar: 1 + 5 / Double(ppu), a)))
            ctx.strokePath()
            switch k {
            case 0:
                drawText("0 Hz", at: CGPoint(x: p.x + 6, y: p.y + 5), h: .left, v: .top, color: theme.secondaryText)
            case 4:
                drawText("fs/2 = " + formatHz(f), at: CGPoint(x: p.x + 6, y: p.y + 6), h: .left, v: .top, color: theme.secondaryText)
            default:
                let q = point(Complex(polar: 1 + 20 / Double(ppu), a))
                drawText(String(format: "%g kHz", (f / 100).rounded() / 10), at: CGPoint(x: q.x + (k == 2 ? 26 : 0), y: q.y),
                         h: .center, v: .middle, color: theme.secondaryText)
            }
        }
        drawText("Re", at: CGPoint(x: s.maxX - 4, y: o.y - 3), h: .right, v: .bottom)
        drawText("Im", at: CGPoint(x: o.x - 4, y: s.minY + 3), h: .right, v: .top)
    }

    private func drawMark(_ ctx: CGContext, _ m: Mark) {
        let p = point(m.z)
        guard p.x.isFinite, p.y.isFinite else { return }
        let selected = m.itemID != nil && m.itemID == model.selectedItem
        let outside = m.kind == .pole && m.z.magnitude >= 1 - 1e-9
        let color = m.kind == .pole ? (outside ? theme.danger : theme.pole) : theme.zero
        let size: CGFloat = 5.5
        if selected {
            ctx.setFillColor(color.withAlphaComponent(0.22).cgColor)
            ctx.fillEllipse(in: CGRect(x: p.x - 12, y: p.y - 12, width: 24, height: 24))
        }
        ctx.setLineWidth(2.2)
        ctx.setStrokeColor(color.cgColor)
        if m.kind == .zero {
            ctx.addEllipse(in: CGRect(x: p.x - size, y: p.y - size, width: size * 2, height: size * 2))
            ctx.strokePath()
        } else {
            ctx.setLineCap(.round)
            ctx.move(to: CGPoint(x: p.x - size, y: p.y - size))
            ctx.addLine(to: CGPoint(x: p.x + size, y: p.y + size))
            ctx.move(to: CGPoint(x: p.x - size, y: p.y + size))
            ctx.addLine(to: CGPoint(x: p.x + size, y: p.y - size))
            ctx.strokePath()
        }
        if m.count > 1 {
            drawText("\(m.count)", at: CGPoint(x: p.x + 7, y: p.y - 7), h: .left, v: .bottom, font: Fonts.smallBold, color: color)
        }
    }

    /// Lines from every pole and zero to the point e^{jω} for the hovered frequency:
    /// |H| is (product of zero distances) / (product of pole distances).
    private func drawCursorLink(_ ctx: CGContext) {
        guard !exporting, let f = model.cursorFrequency else { return }
        let w = 2 * .pi * f / model.fs
        let target = Complex(polar: 1, w)
        let tp = point(target)
        let roots = (model.filter.zpk.zeros.map { ($0, PZItem.Kind.zero) } + model.filter.zpk.poles.map { ($0, PZItem.Kind.pole) })
            .filter { $0.0.magnitude > 1e-12 }
        if roots.count <= 40 {
            ctx.setLineWidth(1)
            for (z, kind) in roots {
                let color = kind == .zero ? theme.zero : theme.pole
                ctx.setStrokeColor(color.withAlphaComponent(0.45).cgColor)
                ctx.move(to: point(z))
                ctx.addLine(to: tp)
                ctx.strokePath()
            }
        }
        ctx.setFillColor(theme.cursor.cgColor)
        ctx.fillEllipse(in: CGRect(x: tp.x - 4.5, y: tp.y - 4.5, width: 9, height: 9))
        let label = String(format: "e^jω, ω = %.3f rad  (%@)", w, formatHz(f))
        let below = tp.y < square.midY
        drawText(label, at: CGPoint(x: tp.x, y: tp.y + (below ? -10 : 10)), h: tp.x > square.midX ? .right : .left,
                 v: below ? .bottom : .top, font: Fonts.small, color: theme.text)
    }

    private func drawSourceDot(_ ctx: CGContext) {
        guard !exporting, [.sine, .square, .sweep].contains(model.source) else { return }
        let f = model.audio.params.currentFrequency.value
        guard f > 0 else { return }
        let p = point(Complex(polar: 1, 2 * .pi * f / model.fs))
        ctx.setFillColor(theme.output.cgColor)
        ctx.fillEllipse(in: CGRect(x: p.x - 5, y: p.y - 5, width: 10, height: 10))
        ctx.setStrokeColor(theme.output.withAlphaComponent(0.35).cgColor)
        ctx.setLineWidth(5)
        ctx.addEllipse(in: CGRect(x: p.x - 8, y: p.y - 8, width: 16, height: 16))
        ctx.strokePath()
    }

    private func drawHoverInfo(_ ctx: CGContext, _ all: [Mark]) {
        guard let hover, square.contains(hover), !exporting, drag == nil || dragMoved else { return }
        if let m = hitMark(hover, all) ?? (dragMoved ? draggedMark(all) : nil) {
            let r = m.z.magnitude, a = abs(m.z.phase)
            let f = a / (2 * .pi) * model.fs
            var lines = [m.kind == .pole ? "pole" : "zero"]
            if m.count > 1 { lines[0] += " ×\(m.count)" }
            if m.z.isReal(tolerance: 1e-9) {
                lines.append("z = " + formatSigned(m.z.re))
            } else {
                lines.append(String(format: "r = %.4f, θ = ±%.2f°", r, a * 180 / .pi))
                lines.append("f = " + formatHz(f))
            }
            if m.kind == .pole && r >= 1 { lines.append("outside |z| = 1: unstable") }
            drawReadout(lines, near: point(m.z), in: square)
        } else {
            let z = complex(hover)
            let h = model.filter.zpk.response(at: z).magnitude
            let db = 20 * log10(max(h, 1e-30))
            let lines = [String(format: "z = %.3f %@ %.3fj", z.re, z.im < 0 ? "−" : "+", abs(z.im)),
                         "|z| = " + String(format: "%.3f", z.magnitude),
                         "|H(z)| = " + formatDB(db)]
            drawReadout(lines, near: hover, in: square)
        }
    }

    private func draggedMark(_ all: [Mark]) -> Mark? {
        if case .item(let id, let conj) = drag {
            return all.first { $0.itemID == id && $0.conjugate == conj }
        }
        return nil
    }

    private func hitMark(_ p: CGPoint, _ all: [Mark]) -> Mark? {
        var best: Mark?
        var bestD: CGFloat = 10
        for m in all {
            let q = point(m.z)
            let d = hypot(q.x - p.x, q.y - p.y)
            if d < bestD {
                bestD = d
                best = m
            }
        }
        return best
    }

    // MARK: |H(z)| heat map

    /// A long FIR filter is a polynomial of degree N−1: its |H(z)| swings by 20(N−1)·log₁₀|z| dB
    /// across the plane, which would drown the picture, so the map is only drawn for IIR
    /// and hand-made filters.
    private var heatMapAvailable: Bool {
        model.spec.method != .fir && model.filter.zpk.zeros.count + model.filter.zpk.poles.count <= 80
    }

    private func requestHeatMap() {
        let s = square
        if s.width != heatSize { heatDirty = true }
        guard heatDirty else { return }
        if model.zerosPending {
            heat = nil
            return
        }
        heatDirty = false
        heatSize = s.width
        heatGeneration += 1
        let generation = heatGeneration
        // Roots at the origin only add delay (a factor of z⁻ⁿ), but n of them would raise a
        // huge mountain in the middle, so the map leaves them out.
        var zpk = model.filter.zpk
        zpk.zeros = zpk.zeros.filter { $0.magnitude > 1e-12 }
        zpk.poles = zpk.poles.filter { $0.magnitude > 1e-12 }
        let range = self.range, center = self.center
        let light = exporting
        let zeroColor = theme.zero, poleColor = theme.pole, bg = theme.plotBackground
        let compute = { () -> CGImage? in
            makeHeatMap(zpk: zpk, n: 150, range: range, center: center, zeroColor: zeroColor, poleColor: poleColor,
                        background: bg, light: light)
        }
        if exporting {
            heat = compute()
            return
        }
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            let image = compute()
            DispatchQueue.main.async {
                guard let self, self.heatGeneration == generation else { return }
                self.heat = image
                self.needsDisplay = true
            }
        }
    }

    override func prepareForExport() {
        heatDirty = true
    }

    // MARK: Mouse

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let trackingArea { removeTrackingArea(trackingArea) }
        let area = NSTrackingArea(rect: bounds, options: [.mouseMoved, .mouseEnteredAndExited, .activeInKeyWindow],
                                  owner: self, userInfo: nil)
        addTrackingArea(area)
        trackingArea = area
    }

    override func mouseMoved(with event: NSEvent) {
        hover = convert(event.locationInWindow, from: nil)
        needsDisplay = true
    }

    override func mouseExited(with event: NSEvent) {
        hover = nil
        needsDisplay = true
    }

    override func scrollWheel(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        let delta = event.hasPreciseScrollingDeltas ? event.scrollingDeltaY * 0.01 : event.scrollingDeltaY * 0.1
        zoomBy(exp(Double(delta)), around: p)
    }

    override func magnify(with event: NSEvent) {
        zoomBy(1 + Double(event.magnification), around: convert(event.locationInWindow, from: nil))
    }

    private func zoomBy(_ factor: Double, around p: CGPoint) {
        let before = complex(p)
        zoom = min(400, max(0.6, zoom * factor))
        let after = complex(p)
        center = center + (before - after)
        if zoom <= 1.0001 && abs(zoom - 1) < 0.05 { center = .zero }
        heatDirty = true
        needsDisplay = true
    }

    override func mouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
        let p = convert(event.locationInWindow, from: nil)
        mouseDownPoint = p
        dragMoved = false
        drag = nil
        if event.clickCount == 2, hitMark(p, marks()) == nil {
            resetView()
            return
        }
        switch model.zTool {
        case .addZero, .addPole:
            addItem(at: p, kind: model.zTool == .addZero ? .zero : .pole, snap: !event.modifierFlags.contains(.option))
            return
        case .move:
            break
        }
        if let m = hitMark(p, marks()) {
            if let id = m.itemID {
                model.selectedItem = id
                drag = .item(id, conjugate: m.conjugate)
            } else if m.z.magnitude > 1e-12 {
                drag = .root(m.z, m.kind)
            }
        } else {
            model.selectedItem = nil
            drag = .pan(start: p, center: center)
        }
        needsDisplay = true
    }

    override func mouseDragged(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        hover = p
        if hypot(p.x - mouseDownPoint.x, p.y - mouseDownPoint.y) > 2 { dragMoved = true }
        guard dragMoved, let drag else { return }
        switch drag {
        case .pan(let start, let c):
            let s = Double(pixelsPerUnit)
            center = c - Complex(Double(p.x - start.x) / s, -Double(p.y - start.y) / s)
            heatDirty = true
            needsDisplay = true
        case .root(let z, let kind):
            // Grabbing a designed filter's root turns it into a hand-edited one.
            model.convertToPoleZero()
            let target = z.im < 0 ? z.conj : z
            if let item = model.spec.items.filter({ $0.kind == kind })
                .min(by: { ($0.position - target).magnitude < ($1.position - target).magnitude }) {
                model.selectedItem = item.id
                self.drag = .item(item.id, conjugate: z.im < 0)
                moveItem(item.id, conjugate: z.im < 0, to: p, snap: !event.modifierFlags.contains(.option))
            }
        case .item(let id, let conj):
            moveItem(id, conjugate: conj, to: p, snap: !event.modifierFlags.contains(.option))
        }
    }

    override func mouseUp(with event: NSEvent) {
        drag = nil
        dragMoved = false
        needsDisplay = true
    }

    private func snapped(_ z0: Complex, kind: PZItem.Kind, paired: Bool, snap: Bool) -> Complex {
        var z = z0
        let tol = Double(7 / pixelsPerUnit)
        if !paired {
            z.im = 0
        } else if abs(z.im) < tol {
            z.im = 0
        }
        if snap && kind == .zero && abs(z.magnitude - 1) < tol {
            z = Complex(polar: 1, z.phase)
        }
        if snap && abs(z.magnitude) < tol { z = .zero }
        return z
    }

    private func moveItem(_ id: UUID, conjugate: Bool, to p: CGPoint, snap: Bool) {
        var items = model.spec.items
        guard let i = items.firstIndex(where: { $0.id == id }) else { return }
        var z = complex(p)
        if conjugate { z = z.conj }
        if items[i].paired && z.im < 0 { z = z.conj }
        z = snapped(z, kind: items[i].kind, paired: items[i].paired, snap: snap)
        items[i].position = Complex(z.re, items[i].paired ? abs(z.im) : 0)
        model.setItems(items)
    }

    private func addItem(at p: CGPoint, kind: PZItem.Kind, snap: Bool) {
        if model.spec.method != .poleZero { model.convertToPoleZero() }
        var z = complex(p)
        let tol = Double(8 / pixelsPerUnit)
        let real = abs(z.im) < tol
        z = snapped(z, kind: kind, paired: !real, snap: snap)
        let item = PZItem(kind, real ? Complex(z.re) : Complex(z.re, abs(z.im)), paired: !real)
        model.setItems(model.spec.items + [item])
        model.selectedItem = item.id
    }

    override func menu(for event: NSEvent) -> NSMenu? {
        let p = convert(event.locationInWindow, from: nil)
        guard let m = hitMark(p, marks()) else { return nil }
        if m.itemID == nil {
            guard m.z.magnitude > 1e-12 else { return nil }
            let menu = NSMenu()
            menu.addItem(withTitle: "Edit Poles and Zeros by Hand", action: #selector(convertAction), keyEquivalent: "").target = self
            return menu
        }
        model.selectedItem = m.itemID
        let menu = NSMenu()
        menu.addItem(withTitle: "Delete", action: #selector(deleteAction), keyEquivalent: "").target = self
        if m.kind == .pole && m.z.magnitude >= 1 {
            menu.addItem(withTitle: "Reflect Inside Unit Circle (1/p*)", action: #selector(reflectAction), keyEquivalent: "").target = self
        }
        if m.kind == .zero && abs(m.z.magnitude - 1) > 1e-9 && m.z.magnitude > 0 {
            menu.addItem(withTitle: "Move onto Unit Circle", action: #selector(onCircleAction), keyEquivalent: "").target = self
        }
        return menu
    }

    @objc private func convertAction() { model.convertToPoleZero() }
    @objc private func deleteAction() { model.deleteSelectedItem() }

    @objc private func reflectAction() {
        guard let id = model.selectedItem else { return }
        var items = model.spec.items
        guard let i = items.firstIndex(where: { $0.id == id }) else { return }
        let z = items[i].position
        items[i].position = Complex(polar: 1 / z.magnitude, z.phase)
        model.setItems(items)
    }

    @objc private func onCircleAction() {
        guard let id = model.selectedItem else { return }
        var items = model.spec.items
        guard let i = items.firstIndex(where: { $0.id == id }) else { return }
        let z = items[i].position
        items[i].position = Complex(polar: 1, z.phase)
        if !items[i].paired { items[i].position = Complex(z.re >= 0 ? 1 : -1) }
        model.setItems(items)
    }

    override var acceptsFirstResponder: Bool { true }

    override func keyDown(with event: NSEvent) {
        switch event.keyCode {
        case 51, 117: model.deleteSelectedItem()   // delete, forward delete
        case 53: model.zTool = .move               // escape
        default: super.keyDown(with: event)
        }
    }
}

/// Renders log|H(z)| over the visible square as a soft colour wash: blue valleys at the
/// zeros, warm mountains at the poles.
private func makeHeatMap(zpk: ZPK, n: Int, range: Double, center: Complex, zeroColor: NSColor, poleColor: NSColor,
                         background: NSColor, light: Bool) -> CGImage? {
    guard !(zpk.zeros.isEmpty && zpk.poles.isEmpty) else { return nil }
    var pixels = [UInt8](repeating: 0, count: n * n * 4)
    func rgb(_ c: NSColor) -> (Double, Double, Double) {
        let s = c.usingColorSpace(.sRGB) ?? c
        return (Double(s.redComponent), Double(s.greenComponent), Double(s.blueComponent))
    }
    let zc = rgb(zeroColor), pc = rgb(poleColor), bg = rgb(background)
    let logGain = log10(max(abs(zpk.gain), 1e-300))
    for row in 0..<n {
        for col in 0..<n {
            let z = Complex(center.re + (Double(col) + 0.5) / Double(n) * 2 * range - range,
                            center.im + range - (Double(row) + 0.5) / Double(n) * 2 * range)
            var l = logGain
            for q in zpk.zeros { l += 0.5 * log10(max((z - q).norm2, 1e-300)) }
            for p in zpk.poles { l -= 0.5 * log10(max((z - p).norm2, 1e-300)) }
            // Valleys fade in over 100 dB, peaks over 40 dB, so the zeros don't flood the plane.
            var v = l < 0 ? max(-1, l / 5) : min(1, l / 2)
            if !v.isFinite { v = 0 }
            let target = v < 0 ? zc : pc
            let a = pow(abs(v), 1.4) * (v < 0 ? 0.24 : 0.45)
            let i = (row * n + col) * 4
            pixels[i] = UInt8(max(0, min(255, (bg.0 + (target.0 - bg.0) * a) * 255)))
            pixels[i + 1] = UInt8(max(0, min(255, (bg.1 + (target.1 - bg.1) * a) * 255)))
            pixels[i + 2] = UInt8(max(0, min(255, (bg.2 + (target.2 - bg.2) * a) * 255)))
            pixels[i + 3] = 255
        }
    }
    let space = CGColorSpace(name: CGColorSpace.sRGB)!
    guard let provider = CGDataProvider(data: Data(pixels) as CFData) else { return nil }
    return CGImage(width: n, height: n, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: n * 4, space: space,
                   bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipLast.rawValue), provider: provider,
                   decode: nil, shouldInterpolate: true, intent: .defaultIntent)
}
