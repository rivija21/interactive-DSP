import Accelerate
import AppKit

/// Windowed real FFT giving magnitude in dBFS (a full-scale sine reads 0 dB).
final class SpectrumAnalyzer {
    let size: Int
    private let log2n: vDSP_Length
    private let setup: FFTSetup
    private var window: [Float]
    private var windowSum: Float
    private var buffer: [Float]
    private var real: [Float]
    private var imag: [Float]

    init(size: Int) {
        self.size = size
        log2n = vDSP_Length(log2(Double(size)).rounded())
        setup = vDSP_create_fftsetup(log2n, FFTRadix(kFFTRadix2))!
        window = [Float](repeating: 0, count: size)
        vDSP_hann_window(&window, vDSP_Length(size), Int32(vDSP_HANN_DENORM))
        windowSum = window.reduce(0, +)
        buffer = [Float](repeating: 0, count: size)
        real = [Float](repeating: 0, count: size / 2)
        imag = [Float](repeating: 0, count: size / 2)
    }

    deinit {
        vDSP_destroy_fftsetup(setup)
    }

    /// Power spectrum (linear, |A|²) of `samples` into out[0...size/2].
    func power(_ samples: UnsafePointer<Float>, into out: inout [Float]) {
        if out.count != size / 2 + 1 { out = [Float](repeating: 0, count: size / 2 + 1) }
        vDSP_vmul(samples, 1, window, 1, &buffer, 1, vDSP_Length(size))
        real.withUnsafeMutableBufferPointer { re in
            imag.withUnsafeMutableBufferPointer { im in
                var split = DSPSplitComplex(realp: re.baseAddress!, imagp: im.baseAddress!)
                buffer.withUnsafeBufferPointer { b in
                    b.baseAddress!.withMemoryRebound(to: DSPComplex.self, capacity: size / 2) {
                        vDSP_ctoz($0, 2, &split, 1, vDSP_Length(size / 2))
                    }
                }
                vDSP_fft_zrip(setup, &split, 1, log2n, FFTDirection(FFT_FORWARD))
            }
        }
        let norm = 1 / (windowSum * windowSum)
        out[0] = real[0] * real[0] * norm / 4
        out[size / 2] = imag[0] * imag[0] * norm / 4
        for k in 1..<(size / 2) {
            out[k] = (real[k] * real[k] + imag[k] * imag[k]) * norm
        }
    }
}

func fftSize(for fs: Double, divisor: Double) -> Int {
    var n = 256
    while Double(n) < fs / divisor { n *= 2 }
    return n
}

/// Maps plot columns to FFT bins (max over the bins a column covers, or interpolation
/// where bins are wider than a column).
struct BinMap {
    var lo: [Int] = []
    var hi: [Int] = []
    var frac: [Float] = []

    init() {}

    init(frequencies: [(Double, Double)], binHz: Double, bins: Int) {
        for (f0, f1) in frequencies {
            let b0 = f0 / binHz, b1 = f1 / binHz
            let i0 = max(0, min(bins - 1, Int(b0.rounded(.up))))
            let i1 = max(0, min(bins - 1, Int(b1.rounded(.down))))
            if i1 >= i0 {
                lo.append(i0)
                hi.append(i1)
                frac.append(-1)
            } else {
                let c = max(0, min(Double(bins - 1) - 1e-6, (b0 + b1) / 2))
                lo.append(Int(c))
                hi.append(Int(c) + 1)
                frac.append(Float(c - Double(Int(c))))
            }
        }
    }

    func value(_ column: Int, _ p: UnsafePointer<Float>) -> Float {
        let f = frac[column]
        if f < 0 {
            var m: Float = 0
            for i in lo[column]...hi[column] { m = max(m, p[i]) }
            return m
        }
        return p[lo[column]] * (1 - f) + p[hi[column]] * f
    }
}

/// Live spectrum of the input (grey) and output (teal).
final class SpectrumView: PlotView {
    private var analyzer: SpectrumAnalyzer?
    private var samples: [Float] = []
    private var inPower: [Float] = []
    private var outPower: [Float] = []
    private var inSmooth: [Float] = []
    private var outSmooth: [Float] = []
    private var columns: [(Double, Double)] = []
    private var map = BinMap()
    private var mapKey = ""
    private var hover: CGPoint?
    private var trackingArea: NSTrackingArea?
    private let dbFloor = -120.0

    override init(model: LabModel) {
        super.init(model: model)
        insets = NSEdgeInsets(top: 20, left: 46, bottom: 24, right: 14)
    }

    /// Forget the averaged spectrum (after the source or filter changes), so the old
    /// sound doesn't linger as a fading ghost.
    func resetSmoothing() {
        inSmooth = []
        outSmooth = []
    }

    func tick() {
        guard !model.hold else { return }
        let n = fftSize(for: model.fs, divisor: 12)
        if analyzer?.size != n {
            analyzer = SpectrumAnalyzer(size: n)
            samples = [Float](repeating: 0, count: n)
            inSmooth = []
            outSmooth = []
        }
        guard let analyzer else { return }
        samples.withUnsafeMutableBufferPointer { model.audio.inputRing.copyLatest(n, into: $0.baseAddress!) }
        samples.withUnsafeBufferPointer { analyzer.power($0.baseAddress!, into: &inPower) }
        samples.withUnsafeMutableBufferPointer { model.audio.outputRing.copyLatest(n, into: $0.baseAddress!) }
        samples.withUnsafeBufferPointer { analyzer.power($0.baseAddress!, into: &outPower) }
        smooth(&inSmooth, inPower)
        smooth(&outSmooth, outPower)
        needsDisplay = true
    }

    private func smooth(_ acc: inout [Float], _ p: [Float]) {
        if acc.count != p.count {
            acc = p
            return
        }
        for i in 0..<p.count {
            // Fast attack and gentle release, like a hardware analyser. When a signal
            // vanishes (more than 20 dB down) fall at 3 dB per frame so no ghost lingers.
            if p[i] > acc[i] {
                acc[i] += (p[i] - acc[i]) * 0.6
            } else if p[i] < acc[i] * 0.01 {
                acc[i] = max(p[i], acc[i] * 0.5)
            } else {
                acc[i] += (p[i] - acc[i]) * 0.25
            }
        }
    }

    private func ensureMap(_ r: CGRect) {
        guard let analyzer else { return }
        let key = "\(Int(r.width))-\(model.logAxis)-\(model.fs)-\(analyzer.size)"
        guard key != mapKey else { return }
        mapKey = key
        let count = max(10, Int(r.width))
        columns = (0..<count).map { i in
            (frequency(forX: r.minX + CGFloat(i), in: r), frequency(forX: r.minX + CGFloat(i + 1), in: r))
        }
        map = BinMap(frequencies: columns, binHz: model.fs / Double(analyzer.size), bins: analyzer.size / 2 + 1)
    }

    private func db(_ p: Float) -> Double {
        max(dbFloor - 10, 10 * log10(Double(max(p, 1e-20))))
    }

    override func draw(_ dirtyRect: NSRect) {
        guard let ctx = NSGraphicsContext.current?.cgContext else { return }
        ctx.setFillColor(theme.panel.cgColor)
        ctx.fill(bounds)
        let r = plotRect
        ctx.setFillColor(theme.plotBackground.cgColor)
        ctx.fill(r)
        drawFrequencyGrid(ctx, in: r)
        drawValueGrid(ctx, in: r, min: dbFloor, max: 0, step: 20, format: { $0 == 0 ? "0" : formatSigned($0, "%.0f") })
        drawText("dBFS", at: CGPoint(x: r.minX - 5, y: r.minY - 12), h: .right)
        ensureMap(r)
        guard !inSmooth.isEmpty, map.lo.count == columns.count else {
            drawFrame(ctx, r)
            return
        }
        let xs = (0..<columns.count).map { r.minX + CGFloat($0) + 0.5 }
        var inDB = [Double](repeating: 0, count: columns.count)
        var outDB = inDB
        inSmooth.withUnsafeBufferPointer { p in for c in 0..<columns.count { inDB[c] = db(map.value(c, p.baseAddress!)) } }
        outSmooth.withUnsafeBufferPointer { p in for c in 0..<columns.count { outDB[c] = db(map.value(c, p.baseAddress!)) } }
        let yIn = inDB.map { y(forValue: $0, min: dbFloor, max: 0, in: r) }
        let yOut = outDB.map { y(forValue: $0, min: dbFloor, max: 0, in: r) }

        ctx.saveGState()
        ctx.clip(to: r)
        // Input: filled grey area.
        let area = CGMutablePath()
        area.move(to: CGPoint(x: xs[0], y: r.maxY))
        for (x, y) in zip(xs, yIn) { area.addLine(to: CGPoint(x: x, y: y)) }
        area.addLine(to: CGPoint(x: xs.last!, y: r.maxY))
        area.closeSubpath()
        ctx.addPath(area)
        ctx.setFillColor(theme.input.withAlphaComponent(0.22).cgColor)
        ctx.fillPath()
        strokeCurve(ctx, xs: xs, ys: yIn, color: theme.input.withAlphaComponent(0.85), width: 1)

        if model.showPrediction {
            let filter = model.filter
            let predicted = zip(columns, inDB).map { col, d -> CGFloat in
                let f = (col.0 + col.1) / 2
                return y(forValue: max(dbFloor - 10, d + filter.magnitudeDB(at: f)), min: dbFloor, max: 0, in: r)
            }
            strokeCurve(ctx, xs: xs, ys: predicted, color: theme.response.withAlphaComponent(0.9), width: 1.3, dash: [5, 4])
        }
        // Output: bright teal with a faint fill.
        let outArea = CGMutablePath()
        outArea.move(to: CGPoint(x: xs[0], y: r.maxY))
        for (x, y) in zip(xs, yOut) { outArea.addLine(to: CGPoint(x: x, y: y)) }
        outArea.addLine(to: CGPoint(x: xs.last!, y: r.maxY))
        outArea.closeSubpath()
        ctx.addPath(outArea)
        ctx.setFillColor(theme.output.withAlphaComponent(0.10).cgColor)
        ctx.fillPath()
        strokeCurve(ctx, xs: xs, ys: yOut, color: theme.output, width: 1.6)
        ctx.restoreGState()
        drawFrame(ctx, r)

        // Legend.
        var lx = r.minX + 10
        for (name, color) in [("input", theme.input), ("output", theme.output)] + (model.showPrediction ? [("predicted = input + |H|", theme.response)] : []) {
            ctx.setFillColor(color.cgColor)
            ctx.fill(CGRect(x: lx, y: r.minY + 9, width: 10, height: 3))
            let rect = drawText(name, at: CGPoint(x: lx + 14, y: r.minY + 10), h: .left, v: .middle, font: Fonts.small, color: theme.secondaryText)
            lx = rect.maxX + 14
        }

        if let hover, r.contains(hover), !exporting {
            let c = max(0, min(columns.count - 1, Int(hover.x - r.minX)))
            let f = (columns[c].0 + columns[c].1) / 2
            ctx.setStrokeColor(theme.cursor.withAlphaComponent(0.35).cgColor)
            ctx.setLineWidth(1)
            ctx.move(to: CGPoint(x: hover.x, y: r.minY))
            ctx.addLine(to: CGPoint(x: hover.x, y: r.maxY))
            ctx.strokePath()
            drawReadout([formatHz(f), "in  " + formatDB(inDB[c]), "out " + formatDB(outDB[c]),
                         "gain " + formatDB(outDB[c] - inDB[c])], near: CGPoint(x: hover.x, y: yOut[c]), in: r)
        }
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let trackingArea { removeTrackingArea(trackingArea) }
        let area = NSTrackingArea(rect: bounds, options: [.mouseMoved, .mouseEnteredAndExited, .activeInKeyWindow], owner: self, userInfo: nil)
        addTrackingArea(area)
        trackingArea = area
    }

    override func mouseMoved(with event: NSEvent) {
        hover = convert(event.locationInWindow, from: nil)
        let r = plotRect
        model.cursorFrequency = r.contains(hover!) ? frequency(forX: hover!.x, in: r) : nil
        needsDisplay = true
    }

    override func mouseExited(with event: NSEvent) {
        hover = nil
        model.cursorFrequency = nil
        needsDisplay = true
    }
}

// MARK: - Spectrogram

/// Scrolling time–frequency picture (short-time Fourier transform), newest on the right.
final class SpectrogramView: PlotView {
    private var analyzer: SpectrumAnalyzer?
    private var fftFrame: [Float] = []
    private var power: [Float] = []
    private var context: CGContext?
    private var imageWidth = 0
    private var imageHeight = 0
    private var writeColumn = 0
    private var cursor = -1
    private var rowMap = BinMap()
    private var key = ""
    private var hop = 480
    private var hover: CGPoint?
    private var trackingArea: NSTrackingArea?
    private static let lut: [UInt32] = makeColormap()

    override init(model: LabModel) {
        super.init(model: model)
        insets = NSEdgeInsets(top: 20, left: 46, bottom: 24, right: 14)
    }

    func reset() {
        key = ""
        needsDisplay = true
    }

    private var ring: SampleRing { model.spectrogramShowsInput ? model.audio.inputRing : model.audio.outputRing }

    private func setup(_ r: CGRect) {
        let n = fftSize(for: model.fs, divisor: 24)
        let k = "\(Int(r.width))x\(Int(r.height))-\(model.logAxis)-\(model.fs)-\(model.spectrogramShowsInput)"
        guard k != key || analyzer?.size != n else { return }
        key = k
        analyzer = SpectrumAnalyzer(size: n)
        fftFrame = [Float](repeating: 0, count: n)
        hop = max(32, Int(model.fs / 100))
        imageWidth = max(10, Int(r.width))
        imageHeight = max(10, Int(r.height))
        context = CGContext(data: nil, width: imageWidth, height: imageHeight, bitsPerComponent: 8, bytesPerRow: imageWidth * 4,
                            space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue)
        if let data = context?.data {
            let px = data.bindMemory(to: UInt32.self, capacity: imageWidth * imageHeight)
            px.update(repeating: Self.lut[0], count: imageWidth * imageHeight)
        }
        writeColumn = 0
        cursor = -1
        // Row 0 is the top of the image = highest frequency.
        let rows = (0..<imageHeight).map { row -> (Double, Double) in
            let yTop = r.minY + CGFloat(row), yBottom = r.minY + CGFloat(row + 1)
            return (frequencyAtY(yBottom, r), frequencyAtY(yTop, r))
        }
        rowMap = BinMap(frequencies: rows, binHz: model.fs / Double(n), bins: n / 2 + 1)
    }

    /// Vertical frequency axis (low at the bottom).
    private func frequencyAtY(_ y: CGFloat, _ r: CGRect) -> Double {
        let t = Double((r.maxY - y) / r.height)
        return model.logAxis ? fMin * pow(fMax / fMin, t) : fMin + t * (fMax - fMin)
    }

    private func yForFrequency(_ f: Double, _ r: CGRect) -> CGFloat {
        let t = model.logAxis ? log(max(f, fMin) / fMin) / log(fMax / fMin) : (f - fMin) / (fMax - fMin)
        return r.maxY - CGFloat(t) * r.height
    }

    func tick() {
        guard !model.hold else { return }
        let r = plotRect
        setup(r)
        guard let analyzer, let context, let data = context.data else { return }
        let n = analyzer.size
        let total = ring.totalWritten
        if cursor < 0 || total - cursor > ring.capacity / 2 || cursor > total {
            cursor = total
        }
        let px = data.bindMemory(to: UInt32.self, capacity: imageWidth * imageHeight)
        var columns = 0
        while cursor + hop <= total && columns < 40 {
            cursor += hop
            fftFrame.withUnsafeMutableBufferPointer { ring.copy(from: cursor - n, count: n, into: $0.baseAddress!) }
            fftFrame.withUnsafeBufferPointer { analyzer.power($0.baseAddress!, into: &power) }
            power.withUnsafeBufferPointer { p in
                for row in 0..<imageHeight {
                    let v = rowMap.value(row, p.baseAddress!)
                    let db = 10 * log10(Double(max(v, 1e-20)))
                    let t = max(0, min(1, (db + 110) / 100))
                    px[row * imageWidth + writeColumn] = Self.lut[Int(t * 255)]
                }
            }
            writeColumn = (writeColumn + 1) % imageWidth
            columns += 1
        }
        if columns > 0 { needsDisplay = true }
    }

    override func draw(_ dirtyRect: NSRect) {
        guard let ctx = NSGraphicsContext.current?.cgContext else { return }
        ctx.setFillColor(theme.panel.cgColor)
        ctx.fill(bounds)
        let r = plotRect
        setup(r)
        if let image = context?.makeImage() {
            ctx.saveGState()
            ctx.clip(to: r)
            ctx.interpolationQuality = .none
            // Oldest columns start at writeColumn; draw them first so "now" is at the right edge.
            let w = CGFloat(imageWidth)
            let split = CGFloat(writeColumn)
            ctx.translateBy(x: r.minX, y: r.maxY)
            ctx.scaleBy(x: r.width / w, y: -r.height / CGFloat(imageHeight))
            if let older = image.cropping(to: CGRect(x: split, y: 0, width: w - split, height: CGFloat(imageHeight))) {
                ctx.draw(older, in: CGRect(x: 0, y: 0, width: w - split, height: CGFloat(imageHeight)))
            }
            if split > 0, let newer = image.cropping(to: CGRect(x: 0, y: 0, width: split, height: CGFloat(imageHeight))) {
                ctx.draw(newer, in: CGRect(x: w - split, y: 0, width: split, height: CGFloat(imageHeight)))
            }
            ctx.restoreGState()
        }
        // Frequency labels on the left.
        let ticks: [Double] = model.logAxis
            ? [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]
            : stride(from: 0.0, through: fMax, by: niceStep(fMax / 6)).map { $0 }
        for f in ticks where f >= fMin && f <= fMax {
            let y = yForFrequency(f, r)
            ctx.setStrokeColor(theme.gridMajor.withAlphaComponent(0.55).cgColor)
            ctx.setLineWidth(1)
            ctx.move(to: CGPoint(x: r.minX, y: y))
            ctx.addLine(to: CGPoint(x: r.maxX, y: y))
            ctx.strokePath()
            drawText(formatHzTick(f), at: CGPoint(x: r.minX - 5, y: y), h: .right)
        }
        drawText("Hz", at: CGPoint(x: r.minX - 5, y: r.minY - 12), h: .right)
        // Time labels underneath.
        let seconds = Double(imageWidth) * Double(hop) / model.fs
        let step = niceStep(seconds / 6)
        var t = 0.0
        while t <= seconds + 1e-9 {
            let x = r.maxX - CGFloat(t / seconds) * r.width
            drawText(t == 0 ? "now" : String(format: "−%gs", t), at: CGPoint(x: x, y: r.maxY + 4), h: t == 0 ? .right : .center, v: .top)
            t += step
        }
        drawText(model.spectrogramShowsInput ? "input" : "output (what you hear)", at: CGPoint(x: r.minX + 8, y: r.minY + 6),
                 h: .left, v: .top, font: Fonts.smallBold, color: theme.secondaryText)
        drawFrame(ctx, r)
        if let hover, r.contains(hover), !exporting {
            let f = frequencyAtY(hover.y, r)
            let ago = Double((r.maxX - hover.x) / r.width) * seconds
            drawReadout([formatHz(f), String(format: "%.2f s ago", ago)], near: hover, in: r)
        }
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let trackingArea { removeTrackingArea(trackingArea) }
        let area = NSTrackingArea(rect: bounds, options: [.mouseMoved, .mouseEnteredAndExited, .activeInKeyWindow], owner: self, userInfo: nil)
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

    private static func makeColormap() -> [UInt32] {
        // Paper to ink: quiet levels fade into the page, loud ones darken through
        // amber and crimson to deep indigo.
        let stops: [(Double, UInt32)] = [
            (0, 0xFFFDF8), (0.22, 0xF4E6BE), (0.42, 0xEBB66F), (0.6, 0xD7743F),
            (0.76, 0xAE3D3B), (0.89, 0x632452), (1, 0x231A3B),
        ]
        return (0..<256).map { i in
            let t = Double(i) / 255
            var k = 0
            while k < stops.count - 2 && t > stops[k + 1].0 { k += 1 }
            let (t0, c0) = stops[k], (t1, c1) = stops[k + 1]
            let u = (t - t0) / (t1 - t0)
            func ch(_ c: UInt32, _ s: UInt32) -> Double { Double((c >> s) & 0xFF) }
            let rr = UInt32(ch(c0, 16) + (ch(c1, 16) - ch(c0, 16)) * u)
            let gg = UInt32(ch(c0, 8) + (ch(c1, 8) - ch(c0, 8)) * u)
            let bb = UInt32(ch(c0, 0) + (ch(c1, 0) - ch(c0, 0)) * u)
            // Memory order R, G, B, X (little-endian UInt32).
            return rr | (gg << 8) | (bb << 16) | (0xFF << 24)
        }
    }
}

// MARK: - Oscilloscope

/// Input and output waveforms, triggered on the input's rising zero crossing.
final class ScopeView: PlotView {
    private var input: [Float] = []
    private var output: [Float] = []
    private var shown = (start: 0, count: 0)
    private var scale: Double = 0.5
    private var outScale: Double = 0.5
    private var lastTrigger: (input: [Float], output: [Float], time: Date)?
    private var hover: CGPoint?
    private var trackingArea: NSTrackingArea?
    private static let levels = [0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0]

    override init(model: LabModel) {
        super.init(model: model)
        insets = NSEdgeInsets(top: 20, left: 46, bottom: 24, right: 14)
    }

    /// Like a scope in "normal" trigger mode: show the most recent triggered sweep, and
    /// keep showing it for a while if no new trigger arrives (so sparse clicks stay visible).
    func tick() {
        guard !model.hold else { return }
        let window = max(16, Int(model.scopeWindowMs / 1000 * model.fs))
        let history = window + Int(0.6 * model.fs)
        if input.count != history {
            input = [Float](repeating: 0, count: history)
            output = [Float](repeating: 0, count: history)
            lastTrigger = nil
        }
        // Both rings are written in the same audio block, so the same indices line up.
        let end = model.audio.inputRing.totalWritten
        input.withUnsafeMutableBufferPointer { model.audio.inputRing.copy(from: end - history, count: history, into: $0.baseAddress!) }
        output.withUnsafeMutableBufferPointer { model.audio.outputRing.copy(from: end - history, count: history, into: $0.baseAddress!) }
        let pre = window / 10
        var peak: Float = 0
        for v in input { peak = max(peak, abs(v)) }
        let hysteresis = 0.02 * peak
        var trigger = -1
        var i = history - window + pre
        while i > pre + 1 && peak > 1e-4 {
            if input[i - 1] <= 0 && input[i] > hysteresis * 0.2 {
                var dipped = false
                for j in max(0, i - window / 4)..<i where input[j] < -hysteresis {
                    dipped = true
                    break
                }
                if dipped || input[i] > 0.3 * peak {
                    trigger = i
                    break
                }
            }
            i -= 1
        }
        if trigger >= 0 {
            let start = trigger - pre
            lastTrigger = (Array(input[start..<start + window]), Array(output[start..<start + window]), Date())
        } else if let last = lastTrigger, Date().timeIntervalSince(last.time) > 1.5 || last.input.count != window {
            lastTrigger = nil
        }
        if let last = lastTrigger {
            input.replaceSubrange((history - window)..<history, with: last.input)
            output.replaceSubrange((history - window)..<history, with: last.output)
        }
        shown = (history - window, window)

        var inPeak: Float = 0, outPeak: Float = 0
        for k in shown.start..<(shown.start + window) {
            inPeak = max(inPeak, abs(input[k]))
            outPeak = max(outPeak, abs(output[k]))
        }
        // One shared scale so shapes compare fairly, unless the output is so much quieter
        // than the input (a narrow resonator fed clicks) that it needs its own gain.
        scale = pick(Double(max(inPeak, outPeak)), current: scale)
        outScale = outPeak < inPeak / 4 ? pick(Double(outPeak), current: outScale) : scale
        needsDisplay = true
    }

    private func pick(_ p: Double, current: Double) -> Double {
        let want = Self.levels.first { $0 >= p * 1.1 } ?? 1
        return want > current || want <= current / 2 ? want : current
    }

    /// How much the output trace is magnified relative to the input's scale.
    private var outputGain: Double { outScale < scale ? scale / outScale : 1 }

    override func draw(_ dirtyRect: NSRect) {
        guard let ctx = NSGraphicsContext.current?.cgContext else { return }
        ctx.setFillColor(theme.panel.cgColor)
        ctx.fill(bounds)
        let r = plotRect
        ctx.setFillColor(theme.plotBackground.cgColor)
        ctx.fill(r)
        // 10 horizontal divisions like a real scope.
        let ms = Double(shown.count) / model.fs * 1000
        ctx.setLineWidth(1)
        for k in 0...10 {
            let x = floor(r.minX + CGFloat(k) / 10 * r.width) + 0.5
            ctx.setStrokeColor((k == 0 || k == 10 ? theme.gridMajor : theme.gridMinor.blended(withFraction: 0.4, of: theme.gridMajor)!).cgColor)
            ctx.move(to: CGPoint(x: x, y: r.minY))
            ctx.addLine(to: CGPoint(x: x, y: r.maxY))
            ctx.strokePath()
            if k % 2 == 0 {
                let t = ms * Double(k) / 10
                drawText(t == 0 ? "0" : (ms >= 50 ? String(format: "%.0f", t) : String(format: "%.1f", t)), at: CGPoint(x: x, y: r.maxY + 4), h: .center, v: .top)
            }
        }
        drawText("ms", at: CGPoint(x: r.minX - 6, y: r.maxY + 4), h: .right, v: .top)
        drawValueGrid(ctx, in: r, min: -scale, max: scale, step: scale / 2, format: { formatSigned($0, self.scale < 0.04 ? "%.4f" : (self.scale < 0.2 ? "%.3f" : "%.2f")) })

        if shown.count > 1, input.count >= shown.start + shown.count, shown.start >= 0 {
            let n = shown.count
            let xs = (0..<n).map { r.minX + CGFloat($0) / CGFloat(n - 1) * r.width }
            let yIn = (0..<n).map { y(forValue: Double(input[shown.start + $0]), min: -scale, max: scale, in: r) }
            let g = outputGain
            let yOut = (0..<n).map { y(forValue: Double(output[shown.start + $0]) * g, min: -scale, max: scale, in: r) }
            ctx.saveGState()
            ctx.clip(to: r)
            strokeCurve(ctx, xs: xs, ys: yIn, color: theme.input, width: 1.3)
            strokeCurve(ctx, xs: xs, ys: yOut, color: theme.output, width: 2)
            ctx.restoreGState()
        }
        var lx = r.minX + 10
        let outName = outputGain > 1 ? String(format: "output (magnified ×%g)", outputGain) : "output"
        for (name, color) in [("input", theme.input), (outName, theme.output)] {
            ctx.setFillColor(color.cgColor)
            ctx.fill(CGRect(x: lx, y: r.minY + 9, width: 10, height: 3))
            let rect = drawText(name, at: CGPoint(x: lx + 14, y: r.minY + 10), h: .left, v: .middle, font: Fonts.small, color: theme.secondaryText)
            lx = rect.maxX + 14
        }
        drawFrame(ctx, r)
        if let hover, r.contains(hover), !exporting, shown.count > 1 {
            let i = max(0, min(shown.count - 1, Int((hover.x - r.minX) / r.width * CGFloat(shown.count - 1))))
            let t = Double(i) / model.fs * 1000
            if shown.start + i < input.count {
                drawReadout([String(format: "t = %.2f ms", t), "in  " + formatSigned(Double(input[shown.start + i])),
                             "out " + formatSigned(Double(output[shown.start + i]))], near: hover, in: r)
            }
        }
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let trackingArea { removeTrackingArea(trackingArea) }
        let area = NSTrackingArea(rect: bounds, options: [.mouseMoved, .mouseEnteredAndExited, .activeInKeyWindow], owner: self, userInfo: nil)
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
}
