import AppKit
import AVFoundation

// Copyable code for coursework (MATLAB, SciPy, C), figure export and filtered-audio export.

enum CodeLanguage: String, CaseIterable {
    case matlab, python, c

    var title: String {
        switch self {
        case .matlab: "MATLAB"
        case .python: "Python (SciPy)"
        case .c: "C"
        }
    }
}

func ordinal(_ n: Int) -> String {
    let suffix: String
    switch (n % 100, n % 10) {
    case (11...13, _): suffix = "th"
    case (_, 1): suffix = "st"
    case (_, 2): suffix = "nd"
    case (_, 3): suffix = "rd"
    default: suffix = "th"
    }
    return "\(n)\(suffix)"
}

/// "4th-order Butterworth low-pass" etc.
func filterTitle(_ spec: DesignSpec) -> String {
    switch spec.method {
    case .iir:
        return "\(ordinal(spec.order))-order \(spec.family.title) \(spec.band.title.lowercased())"
    case .fir:
        return "\(spec.taps)-tap FIR \(spec.band.title.lowercased()) (\(spec.window.title) window)"
    case .poleZero:
        return spec.presetName ?? "Hand-placed poles and zeros"
    }
}

func filterSubtitle(_ spec: DesignSpec, fs: Double) -> String {
    let rate = "fs = " + formatHz(fs)
    switch spec.method {
    case .iir:
        let edges = spec.band.isBand ? "\(formatHz(spec.f1)) – \(formatHz(spec.f2))" : formatHz(spec.f1)
        var s = "\(spec.band.isBand ? "Band edges" : "Cutoff") \(edges) (\(spec.family.cutoffMeaning))"
        if spec.family.usesPassbandRipple { s += String(format: " · ripple %.2f dB", spec.rippleDB) }
        if spec.family.usesStopbandAttenuation { s += String(format: " · stopband %.0f dB", spec.stopDB) }
        return s + " · " + rate
    case .fir:
        let edges = spec.band.isBand ? "\(formatHz(spec.f1)) – \(formatHz(spec.f2))" : formatHz(spec.f1)
        return "\(spec.band.isBand ? "Band edges" : "Cutoff") \(edges) · " + rate
    case .poleZero:
        return "Gain scaled so the peak is 0 dB · " + rate
    }
}

private func num(_ v: Double) -> String {
    if v == 0 { return "0" }
    return String(format: "%.17g", v)
}

private func complexList(_ zs: [Complex], python: Bool) -> String {
    let j = python ? "j" : "i"
    return zs.map { z in
        if z.isReal(tolerance: 1e-12) { return num(z.re) }
        return "\(num(z.re))\(z.im < 0 ? "-" : "+")\(num(abs(z.im)))\(j)"
    }.joined(separator: ", ")
}

func exportCode(_ lang: CodeLanguage, spec rawSpec: DesignSpec, filter: DigitalFilter) -> String {
    let fs = filter.sampleRate
    let spec = rawSpec.clamped(fs: fs)
    let header = "Filter Lab: \(filterTitle(spec)). \(filterSubtitle(spec, fs: fs).replacingOccurrences(of: "\u{2212}", with: "-"))"
    let fsText = fs == fs.rounded() ? String(format: "%.0f", fs) : num(fs)
    switch lang {
    case .matlab: return matlabCode(spec, filter, header, fsText)
    case .python: return pythonCode(spec, filter, header, fsText)
    case .c: return cCode(filter, header)
    }
}

private func sosRows(_ filter: DigitalFilter) -> [[Double]] {
    guard case .iir(let sos) = filter.structure else { return [] }
    return sos.map { [$0.b0, $0.b1, $0.b2, 1, $0.a1, $0.a2] }
}

private func matlabCode(_ spec: DesignSpec, _ filter: DigitalFilter, _ header: String, _ fs: String) -> String {
    var out = "% \(header)\nfs = \(fs);\n"
    let wn = spec.band.isBand ? "[\(num(spec.f1)) \(num(spec.f2))]/(fs/2)" : "\(num(spec.f1))/(fs/2)"
    let type: String
    switch spec.band {
    case .lowpass: type = "'low'"
    case .highpass: type = "'high'"
    case .bandpass: type = "'bandpass'"
    case .bandstop: type = "'stop'"
    }
    switch spec.method {
    case .iir:
        let call: String?
        switch spec.family {
        case .butterworth: call = "butter(\(spec.order), \(wn), \(type))"
        case .chebyshev1: call = "cheby1(\(spec.order), \(num(spec.rippleDB)), \(wn), \(type))"
        case .chebyshev2: call = "cheby2(\(spec.order), \(num(spec.stopDB)), \(wn), \(type))"
        case .elliptic: call = "ellip(\(spec.order), \(num(spec.rippleDB)), \(num(spec.stopDB)), \(wn), \(type))"
        case .bessel: call = nil
        }
        if let call {
            out += "[z, p, k] = \(call);\nsos = zp2sos(z, p, k);   % rows: b0 b1 b2 a0 a1 a2\n\n"
            out += "% The same filter as designed by Filter Lab (zp2sos may order the sections differently):\n"
        } else {
            out += "% MATLAB's besself() is analog-only, so here are Filter Lab's digital coefficients\n"
            out += "% (Bessel prototype normalised like SciPy's norm='phase', then bilinear transform):\n"
        }
        out += "sos_lab = [\n" + sosRows(filter).map { "    " + $0.map(num).joined(separator: " ") + ";" }.joined(separator: "\n") + "\n];\n"
        if call == nil { out += "sos = sos_lab;\n" }
        out += "\nfreqz(sos, 4096, fs);   % plot the response\n% y = sosfilt(sos, x);  % filter a signal x\n"
    case .fir:
        guard case .fir(let h) = filter.structure else { return out }
        let window: String
        switch spec.window {
        case .rectangular: window = "rectwin(\(h.count))"
        case .hann: window = "hann(\(h.count))"
        case .hamming: window = "hamming(\(h.count))"
        case .blackman: window = "blackman(\(h.count))"
        case .kaiser: window = "kaiser(\(h.count), \(num(spec.kaiserBeta)))"
        }
        out += "b = fir1(\(h.count - 1), \(wn), \(type), \(window));   % \(h.count) taps\n\n"
        out += "% Filter Lab's taps (identical to fir1 above):\nb_lab = [" + h.map(num).joined(separator: " ") + "];\n"
        out += "\nfreqz(b, 1, 4096, fs);\n% y = filter(b, 1, x);\n"
    case .poleZero:
        out += "z = [\(complexList(filter.zpk.zeros, python: false))].';\n"
        out += "p = [\(complexList(filter.zpk.poles, python: false))].';\n"
        out += "k = \(num(filter.zpk.gain));\n"
        out += "sos = zp2sos(z, p, k);\n\nzplane(z, p);\nfigure; freqz(sos, 4096, fs);\n% y = sosfilt(sos, x);\n"
    }
    return out
}

private func pythonCode(_ spec: DesignSpec, _ filter: DigitalFilter, _ header: String, _ fs: String) -> String {
    var out = "# \(header)\nimport numpy as np\nfrom scipy import signal\n\nfs = \(fs)\n"
    let wn = spec.band.isBand ? "[\(num(spec.f1)), \(num(spec.f2))]" : num(spec.f1)
    let btype = "'\(spec.band.rawValue)'"
    switch spec.method {
    case .iir:
        let call: String
        switch spec.family {
        case .butterworth: call = "signal.butter(\(spec.order), \(wn), btype=\(btype), fs=fs, output='sos')"
        case .chebyshev1: call = "signal.cheby1(\(spec.order), \(num(spec.rippleDB)), \(wn), btype=\(btype), fs=fs, output='sos')"
        case .chebyshev2: call = "signal.cheby2(\(spec.order), \(num(spec.stopDB)), \(wn), btype=\(btype), fs=fs, output='sos')"
        case .elliptic: call = "signal.ellip(\(spec.order), \(num(spec.rippleDB)), \(num(spec.stopDB)), \(wn), btype=\(btype), fs=fs, output='sos')"
        case .bessel: call = "signal.bessel(\(spec.order), \(wn), btype=\(btype), norm='phase', fs=fs, output='sos')"
        }
        out += "sos = \(call)\n\n"
        out += "# The same filter as designed by Filter Lab (sections may be ordered differently):\n"
        out += "sos_lab = np.array([\n" + sosRows(filter).map { "    [" + $0.map(num).joined(separator: ", ") + "]," }.joined(separator: "\n") + "\n])\n"
        out += "\nw, h = signal.sosfreqz(sos, worN=4096, fs=fs)\n# y = signal.sosfilt(sos, x)\n"
    case .fir:
        guard case .fir(let h) = filter.structure else { return out }
        let window: String
        switch spec.window {
        case .rectangular: window = "'boxcar'"
        case .hann: window = "'hann'"
        case .hamming: window = "'hamming'"
        case .blackman: window = "'blackman'"
        case .kaiser: window = "('kaiser', \(num(spec.kaiserBeta)))"
        }
        out += "h = signal.firwin(\(h.count), \(wn), window=\(window), pass_zero=\(btype), fs=fs)\n\n"
        out += "# Filter Lab's taps (identical to firwin above):\nh_lab = np.array([" + h.map(num).joined(separator: ", ") + "])\n"
        out += "\nw, H = signal.freqz(h, worN=4096, fs=fs)\n# y = signal.lfilter(h, 1, x)\n"
    case .poleZero:
        out += "z = np.array([\(complexList(filter.zpk.zeros, python: true))])\n"
        out += "p = np.array([\(complexList(filter.zpk.poles, python: true))])\n"
        out += "k = \(num(filter.zpk.gain))\n"
        out += "sos = signal.zpk2sos(z, p, k)\n\nw, h = signal.sosfreqz(sos, worN=4096, fs=fs)\n# y = signal.sosfilt(sos, x)\n"
    }
    return out
}

private func cCode(_ filter: DigitalFilter, _ header: String) -> String {
    switch filter.structure {
    case .iir(let sos):
        var out = "/* \(header)\n   Cascade of biquads, transposed direct form II. Call filter_sample() once per sample. */\n\n"
        out += "#define NUM_SECTIONS \(sos.count)\n\n"
        out += "/* b0, b1, b2, a1, a2 for each section (a0 = 1) */\nstatic const double sos[NUM_SECTIONS][5] = {\n"
        out += sos.map { "    { \(num($0.b0)), \(num($0.b1)), \(num($0.b2)), \(num($0.a1)), \(num($0.a2)) }," }.joined(separator: "\n")
        out += "\n};\n\nstatic double state[NUM_SECTIONS][2];\n\n"
        out += """
        double filter_sample(double x)
        {
            for (int s = 0; s < NUM_SECTIONS; s++) {
                const double *c = sos[s];
                double y = c[0] * x + state[s][0];
                state[s][0] = c[1] * x - c[3] * y + state[s][1];
                state[s][1] = c[2] * x - c[4] * y;
                x = y;
            }
            return x;
        }

        """
        return out
    case .fir(let h):
        var out = "/* \(header)\n   Direct-form FIR. Call filter_sample() once per sample. */\n\n"
        out += "#define NUM_TAPS \(h.count)\n\nstatic const double h[NUM_TAPS] = {\n"
        var lines: [String] = []
        var i = 0
        while i < h.count {
            lines.append("    " + h[i..<min(h.count, i + 4)].map(num).joined(separator: ", ") + ",")
            i += 4
        }
        out += lines.joined(separator: "\n") + "\n};\n\nstatic double delay[NUM_TAPS];\nstatic int pos = 0;\n\n"
        out += """
        double filter_sample(double x)
        {
            delay[pos] = x;
            double y = 0.0;
            int i = pos;
            for (int k = 0; k < NUM_TAPS; k++) {
                y += h[k] * delay[i];          /* h[k] * x[n-k] */
                i = (i == 0) ? NUM_TAPS - 1 : i - 1;
            }
            pos = (pos + 1) % NUM_TAPS;
            return y;
        }

        """
        return out
    }
}

// MARK: - Saving files

func savePNG(_ data: Data?, suggestedName: String, window: NSWindow?) {
    guard let data else { return }
    let panel = NSSavePanel()
    panel.allowedContentTypes = [.png]
    panel.nameFieldStringValue = suggestedName
    panel.message = "Figures are saved with a white background, ready for a lab report."
    let save = { (response: NSApplication.ModalResponse) in
        guard response == .OK, let url = panel.url else { return }
        try? data.write(to: url)
    }
    if let window { panel.beginSheetModal(for: window, completionHandler: save) } else { save(panel.runModal()) }
}

/// Runs the current source through the current filter offline and writes a WAV file.
func exportFilteredAudio(model: LabModel, window: NSWindow?) {
    let fs = model.fs
    var input: [Float]
    var name: String
    if let loop = model.audio.currentLoopSamples {
        input = loop.samples
        name = loop.name
    } else if model.source == .microphone {
        NSSound.beep()
        return
    } else {
        // Render 8 seconds of the test signal.
        let gen = SourceGenerator(fs: fs)
        let n = Int(fs * 8)
        input = [Float](repeating: 0, count: n)
        input.withUnsafeMutableBufferPointer { buf in
            var offset = 0
            while offset < n {
                let m = min(1024, n - offset)
                _ = gen.render(model.source, frequency: model.frequency, loop: nil, into: buf.baseAddress! + offset, m)
                offset += m
            }
        }
        name = model.source.title
    }
    if model.source == .musicHum {
        var phase = 0.0
        let amps = [0.10, 0.05, 0.07, 0.03, 0.05]
        for i in 0..<input.count {
            phase += 50 / fs
            if phase >= 1 { phase -= 1 }
            var h = 0.0
            for (k, a) in amps.enumerated() { h += a * sin(2 * .pi * phase * Double(k + 1)) }
            input[i] += Float(h)
        }
    }
    let kernel = FilterKernel(model.filter)
    var output = [Float](repeating: 0, count: input.count)
    input.withUnsafeBufferPointer { src in
        output.withUnsafeMutableBufferPointer { dst in
            var offset = 0
            while offset < input.count {
                let m = min(FilterKernel.maxBlock, input.count - offset)
                kernel.process(src.baseAddress! + offset, dst.baseAddress! + offset, m)
                offset += m
            }
        }
    }
    let panel = NSSavePanel()
    panel.allowedContentTypes = [.wav]
    panel.nameFieldStringValue = "\(name) – filtered.wav"
    let save = { (response: NSApplication.ModalResponse) in
        guard response == .OK, let url = panel.url,
              let format = AVAudioFormat(standardFormatWithSampleRate: fs, channels: 1),
              let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: AVAudioFrameCount(output.count)) else { return }
        buffer.frameLength = AVAudioFrameCount(output.count)
        let peak = output.map(abs).max() ?? 1
        let g: Float = peak > 0.99 ? 0.99 / peak : 1
        for i in 0..<output.count { buffer.floatChannelData![0][i] = output[i] * g }
        do {
            let settings: [String: Any] = [AVFormatIDKey: kAudioFormatLinearPCM, AVSampleRateKey: fs, AVNumberOfChannelsKey: 1,
                                           AVLinearPCMBitDepthKey: 16, AVLinearPCMIsFloatKey: false, AVLinearPCMIsBigEndianKey: false]
            let file = try AVAudioFile(forWriting: url, settings: settings, commonFormat: .pcmFormatFloat32, interleaved: false)
            try file.write(from: buffer)
        } catch {
            let alert = NSAlert(error: error)
            alert.runModal()
        }
    }
    if let window { panel.beginSheetModal(for: window, completionHandler: save) } else { save(panel.runModal()) }
}
