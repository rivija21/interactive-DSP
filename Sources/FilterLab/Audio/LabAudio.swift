import AVFoundation
import Foundation

/// Settings the audio thread reads every block.
final class RTParams: @unchecked Sendable {
    let source = SharedInt(SourceKind.music.rawValue)
    let frequency = SharedDouble(440)
    let listen = SharedInt(0)
    let volume = SharedDouble(0.7)
    let filterOn = SharedInt(1)
    let micGain = SharedDouble(4)
    let paused = SharedInt(0)
    // Written by the audio thread.
    let currentFrequency = SharedDouble(0)
    let inputPeak = SharedDouble(0)
    let outputPeak = SharedDouble(0)
    let clipped = SharedInt(0)
    let ranAway = SharedInt(0)
}

/// Everything the output render callback touches. One instance per engine run.
final class RenderState {
    let fs: Double
    let params: RTParams
    let kernels: Exchange<FilterKernel>
    let loops: Exchange<LoopBuffer>
    let micLinks: Exchange<MicLink>
    let inputRing: SampleRing
    let outputRing: SampleRing
    private var kernel: FilterKernel?
    private var loop: LoopBuffer?
    private var micLink: MicLink?
    private let generator: SourceGenerator
    private let x: UnsafeMutablePointer<Float>
    private let y: UnsafeMutablePointer<Float>
    private let o: UnsafeMutablePointer<Float>
    private var wet: Float = 1
    private var gain: Float = 0
    private var lastSource = -1

    init(fs: Double, params: RTParams, kernels: Exchange<FilterKernel>, loops: Exchange<LoopBuffer>,
         micLinks: Exchange<MicLink>, inputRing: SampleRing, outputRing: SampleRing) {
        self.fs = fs
        self.params = params
        self.kernels = kernels
        self.loops = loops
        self.micLinks = micLinks
        self.inputRing = inputRing
        self.outputRing = outputRing
        generator = SourceGenerator(fs: fs)
        let n = FilterKernel.maxBlock
        x = .allocate(capacity: n)
        y = .allocate(capacity: n)
        o = .allocate(capacity: n)
    }

    deinit {
        x.deallocate()
        y.deallocate()
        o.deallocate()
    }

    func render(_ frameCount: Int, _ abl: UnsafeMutablePointer<AudioBufferList>) {
        let buffers = UnsafeMutableAudioBufferListPointer(abl)
        guard let first = buffers.first?.mData?.assumingMemoryBound(to: Float.self) else { return }

        let old = kernel
        if kernels.take(into: &kernel), let kernel, let old {
            kernel.adoptState(from: old)
        }
        _ = loops.take(into: &loop)
        _ = micLinks.take(into: &micLink)

        let source = SourceKind(rawValue: params.source.value) ?? .music
        if source.rawValue != lastSource {
            generator.loopPosition = 0
            lastSource = source.rawValue
        }
        let paused = params.paused.value != 0
        let targetWet: Float = params.filterOn.value != 0 ? 1 : 0
        let targetGain = params.listen.value != 0 ? Float(params.volume.value) : 0
        let rampStep = Float(1 / (0.01 * fs))
        var inPeak: Float = 0, outPeak: Float = 0
        var clipped = false

        var offset = 0
        while offset < frameCount {
            let n = min(frameCount - offset, FilterKernel.maxBlock)
            if paused {
                x.update(repeating: 0, count: n)
            } else if source == .microphone {
                if let reader = micLink?.reader {
                    reader.read(into: x, n, gain: Float(params.micGain.value))
                } else {
                    x.update(repeating: 0, count: n)
                }
            } else {
                let f = generator.render(source, frequency: params.frequency.value, loop: loop, into: x, n)
                params.currentFrequency.value = f
            }
            if let kernel {
                if !kernel.process(x, y, n) && kernel.isStable {
                    params.ranAway.value = 1
                }
            } else {
                y.update(from: x, count: n)
            }
            let out = first + offset
            for i in 0..<n {
                wet += max(-rampStep, min(rampStep, targetWet - wet))
                gain += max(-rampStep, min(rampStep, targetGain - gain))
                let heard = x[i] + (y[i] - x[i]) * wet
                o[i] = heard
                var s = heard * gain
                if s > 1 { s = 1; clipped = true } else if s < -1 { s = -1; clipped = true }
                out[i] = s
                inPeak = max(inPeak, abs(x[i]))
                outPeak = max(outPeak, abs(heard))
            }
            inputRing.write(x, count: n)
            outputRing.write(o, count: n)
            offset += n
        }
        for b in buffers.dropFirst() {
            b.mData?.assumingMemoryBound(to: Float.self).update(from: first, count: frameCount)
        }
        params.inputPeak.value = Double(inPeak)
        params.outputPeak.value = Double(outPeak)
        if clipped { params.clipped.value = 1 }
    }
}

/// Connects the microphone ring to the output thread (or disconnects it when reader is nil).
final class MicLink {
    let reader: MicReader?
    init(_ reader: MicReader?) { self.reader = reader }
}

/// Owns the audio engine, the microphone and the loop buffers.
final class LabAudio {
    let params = RTParams()
    let inputRing = SampleRing(capacity: 1 << 18)
    let outputRing = SampleRing(capacity: 1 << 18)
    private let kernels = Exchange<FilterKernel>()
    private let loops = Exchange<LoopBuffer>()
    private let micLinks = Exchange<MicLink>()
    private var engine: AVAudioEngine?
    private var renderState: RenderState?
    private(set) var fs: Double = 48000
    private var lastFilter: DigitalFilter?
    private var musicCache: [Double: [Float]] = [:]
    private var fileAudio: (samples: [Float], rate: Double, name: String)?
    private var mic: MicCapture?
    private(set) var isRunning = false
    private var configObserver: NSObjectProtocol?
    var onStatus: ((String?) -> Void)?

    var source: SourceKind {
        get { SourceKind(rawValue: params.source.value) ?? .music }
        set {
            params.source.value = newValue.rawValue
            refreshLoop()
        }
    }

    var fileName: String? { fileAudio?.name }
    var hasFile: Bool { fileAudio != nil }

    // MARK: Engine

    func start(fs newFs: Double) {
        stopEngine()
        fs = newFs
        let engine = AVAudioEngine()
        guard let format = AVAudioFormat(standardFormatWithSampleRate: fs, channels: 1) else { return }
        let state = RenderState(fs: fs, params: params, kernels: kernels, loops: loops, micLinks: micLinks,
                                inputRing: inputRing, outputRing: outputRing)
        let node = AVAudioSourceNode(format: format) { _, _, frameCount, abl in
            state.render(Int(frameCount), abl)
            return noErr
        }
        engine.attach(node)
        engine.connect(node, to: engine.mainMixerNode, format: format)
        engine.mainMixerNode.outputVolume = 1
        do {
            engine.prepare()
            try engine.start()
        } catch {
            onStatus?("Audio output couldn't start: \(error.localizedDescription)")
            return
        }
        self.engine = engine
        renderState = state
        isRunning = true
        if let lastFilter { setFilter(lastFilter) }
        refreshLoop()
        if mic != nil { connectMic(restartCapture: true) }
        configObserver = NotificationCenter.default.addObserver(
            forName: .AVAudioEngineConfigurationChange, object: engine, queue: .main
        ) { [weak self] _ in
            // Headphones plugged in or the output device changed: rebuild the graph.
            guard let self else { return }
            self.start(fs: self.fs)
        }
    }

    private func stopEngine() {
        if let configObserver { NotificationCenter.default.removeObserver(configObserver) }
        configObserver = nil
        engine?.stop()
        engine = nil
        renderState = nil
        isRunning = false
        kernels.drain()
        loops.drain()
        micLinks.drain()
    }

    func shutdown() {
        stopMic()
        stopEngine()
    }

    /// Main-thread housekeeping: frees objects the audio thread has finished with.
    func tick() {
        kernels.drain()
        loops.drain()
        micLinks.drain()
    }

    // MARK: Filter

    func setFilter(_ filter: DigitalFilter) {
        lastFilter = filter
        params.ranAway.value = 0
        kernels.publish(FilterKernel(filter))
    }

    // MARK: Loops

    private func refreshLoop() {
        switch source {
        case .music, .musicHum:
            if let cached = musicCache[fs] {
                loops.publish(LoopBuffer(cached, name: "Music"))
            } else {
                let rate = fs
                DispatchQueue.global(qos: .userInitiated).async { [weak self] in
                    let samples = synthesizeMusicLoop(fs: rate)
                    DispatchQueue.main.async {
                        guard let self else { return }
                        self.musicCache[rate] = samples
                        if self.fs == rate && (self.source == .music || self.source == .musicHum) {
                            self.loops.publish(LoopBuffer(samples, name: "Music"))
                        }
                    }
                }
            }
        case .file:
            if let fileAudio {
                loops.publish(LoopBuffer(resample(fileAudio.samples, from: fileAudio.rate, to: fs), name: fileAudio.name))
            }
        default:
            break
        }
    }

    /// Music samples at the current rate (for exporting).
    var currentLoopSamples: (samples: [Float], name: String)? {
        switch source {
        case .music, .musicHum:
            return musicCache[fs].map { ($0, "Music loop") }
        case .file:
            return fileAudio.map { (resample($0.samples, from: $0.rate, to: fs), $0.name) }
        default:
            return nil
        }
    }

    /// Decodes an audio file to mono. Throws if the file can't be read.
    func loadFile(_ url: URL) throws {
        let file = try AVAudioFile(forReading: url)
        let format = file.processingFormat
        let maxFrames = AVAudioFrameCount(min(file.length, Int64(format.sampleRate * 600)))
        guard maxFrames > 0, let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: maxFrames) else {
            throw CocoaError(.fileReadCorruptFile)
        }
        try file.read(into: buffer, frameCount: maxFrames)
        let n = Int(buffer.frameLength)
        let channels = Int(format.channelCount)
        guard let data = buffer.floatChannelData, n > 0 else { throw CocoaError(.fileReadCorruptFile) }
        var mono = [Float](repeating: 0, count: n)
        for c in 0..<channels {
            let ch = data[c]
            for i in 0..<n { mono[i] += ch[i] / Float(channels) }
        }
        // Normalise to a comfortable level.
        let peak = mono.map(abs).max() ?? 1
        if peak > 0 {
            let g = min(0.7 / peak, 4)
            for i in 0..<n { mono[i] *= g }
        }
        fileAudio = (mono, format.sampleRate, url.deletingPathExtension().lastPathComponent)
        source = .file
    }

    // MARK: Microphone

    /// Asks for microphone permission if needed, then starts recording.
    func startMic(completion: @escaping (String?) -> Void) {
        let begin = { [weak self] in
            guard let self else { return }
            do {
                let capture = self.mic ?? MicCapture()
                try capture.start(targetRate: self.fs)
                self.mic = capture
                self.connectMic(restartCapture: false)
                completion(nil)
            } catch {
                completion(error.localizedDescription)
            }
        }
        switch AVCaptureDevice.authorizationStatus(for: .audio) {
        case .authorized:
            begin()
        case .notDetermined:
            AVCaptureDevice.requestAccess(for: .audio) { granted in
                DispatchQueue.main.async {
                    if granted { begin() } else { completion("denied") }
                }
            }
        default:
            completion("denied")
        }
    }

    /// Points the output thread at the mic ring. After a sample-rate or device change the
    /// capture restarts, because its anti-aliasing filter depends on the processing rate.
    private func connectMic(restartCapture: Bool) {
        guard let mic else { return }
        if restartCapture { try? mic.start(targetRate: fs) }
        guard mic.sampleRate > 0 else { return }
        micLinks.publish(MicLink(MicReader(ring: mic.ring, micRate: mic.sampleRate, outputRate: fs)))
    }

    func stopMic() {
        micLinks.publish(MicLink(nil))
        mic?.stop()
        mic = nil
    }

    var micActive: Bool { mic != nil }
}

/// Offline sample-rate conversion with AVAudioConverter.
func resample(_ samples: [Float], from inRate: Double, to outRate: Double) -> [Float] {
    if abs(inRate - outRate) < 0.5 || samples.isEmpty { return samples }
    guard let inFormat = AVAudioFormat(standardFormatWithSampleRate: inRate, channels: 1),
          let outFormat = AVAudioFormat(standardFormatWithSampleRate: outRate, channels: 1),
          let converter = AVAudioConverter(from: inFormat, to: outFormat),
          let input = AVAudioPCMBuffer(pcmFormat: inFormat, frameCapacity: AVAudioFrameCount(samples.count))
    else { return samples }
    converter.sampleRateConverterQuality = AVAudioQuality.max.rawValue
    input.frameLength = AVAudioFrameCount(samples.count)
    samples.withUnsafeBufferPointer { input.floatChannelData![0].update(from: $0.baseAddress!, count: samples.count) }
    let outCount = AVAudioFrameCount(Double(samples.count) * outRate / inRate + 1024)
    guard let output = AVAudioPCMBuffer(pcmFormat: outFormat, frameCapacity: outCount) else { return samples }
    var fed = false
    var error: NSError?
    converter.convert(to: output, error: &error) { _, status in
        if fed {
            status.pointee = .endOfStream
            return nil
        }
        fed = true
        status.pointee = .haveData
        return input
    }
    let n = Int(output.frameLength)
    return Array(UnsafeBufferPointer(start: output.floatChannelData![0], count: n))
}
