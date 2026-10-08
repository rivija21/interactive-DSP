import AppKit

enum LiveView: Int, CaseIterable {
    case spectrum, spectrogram, scope

    var title: String {
        switch self {
        case .spectrum: "Spectrum"
        case .spectrogram: "Spectrogram"
        case .scope: "Scope"
        }
    }
}

enum ResponseMode: Int, CaseIterable {
    case magnitude, phase, groupDelay, impulse, step

    var title: String {
        switch self {
        case .magnitude: "Magnitude"
        case .phase: "Phase"
        case .groupDelay: "Group delay"
        case .impulse: "Impulse"
        case .step: "Step"
        }
    }
}

enum ZTool: Int {
    case move, addZero, addPole
}

enum TheoryPage: Equatable {
    case filter, lessons, lesson(String), experiments
}

struct ModelChange: OptionSet {
    let rawValue: Int
    static let filter = ModelChange(rawValue: 1 << 0)      // the designed filter changed
    static let zeros = ModelChange(rawValue: 1 << 1)       // FIR zeros finished computing
    static let source = ModelChange(rawValue: 1 << 2)      // input source or its settings
    static let output = ModelChange(rawValue: 1 << 3)      // listen / volume / filter on-off
    static let display = ModelChange(rawValue: 1 << 4)     // axis, tabs, view options
    static let cursor = ModelChange(rawValue: 1 << 5)      // hover frequency
    static let selection = ModelChange(rawValue: 1 << 6)   // selected pole/zero or tool
    static let theory = ModelChange(rawValue: 1 << 7)      // theory page
    static let banner = ModelChange(rawValue: 1 << 8)
    static let sampleRate = ModelChange(rawValue: 1 << 9)
    static let all = ModelChange(rawValue: ~0)
}

struct Banner: Equatable {
    enum Style { case info, warning, danger }
    var text: String
    var style: Style
    var actionTitle: String?
    var id: String
}

/// The whole state of the lab. Views observe it; the audio engine mirrors it.
final class LabModel {
    static let sampleRates: [Double] = [8000, 16000, 22050, 32000, 44100, 48000]

    let audio = LabAudio()
    let undoManager = UndoManager()

    private(set) var spec = DesignSpec()
    private(set) var filter: DigitalFilter
    private(set) var fs: Double = 48000
    private(set) var zerosPending = false
    private var cachedMeasurements: (generation: Int, value: FilterMeasurements)?
    private var designGeneration = 0
    private let rootGeneration = SharedInt(0)
    private let rootQueue = DispatchQueue(label: "filterlab.roots", qos: .userInitiated)

    private(set) var source: SourceKind = .music
    var frequency: Double = 440 { didSet { audio.params.frequency.value = frequency; notify(.source) } }
    var micGain: Double = 4 { didSet { audio.params.micGain.value = micGain; notify(.source) } }
    var listen = false { didSet { audio.params.listen.value = listen ? 1 : 0; notify(.output) } }
    var volume: Double = 0.7 { didSet { audio.params.volume.value = volume; notify(.output) } }
    var filterOn = true { didSet { audio.params.filterOn.value = filterOn ? 1 : 0; notify(.output) } }
    var paused = false { didSet { audio.params.paused.value = paused ? 1 : 0; notify(.output) } }

    var logAxis = true { didSet { notify(.display) } }
    var showHMap = true { didSet { notify(.display) } }
    var showPrediction = false { didSet { notify(.display) } }
    var liveView: LiveView = .spectrum { didSet { notify(.display) } }
    var responseMode: ResponseMode = .magnitude { didSet { notify(.display) } }
    var spectrogramShowsInput = false { didSet { notify(.display) } }
    var scopeWindowMs: Double = 10 { didSet { notify(.display) } }
    var hold = false { didSet { notify(.display) } }
    var cursorFrequency: Double? { didSet { if cursorFrequency != oldValue { notify(.cursor) } } }
    var selectedItem: UUID? { didSet { if selectedItem != oldValue { notify(.selection) } } }
    var zTool: ZTool = .move { didSet { notify(.selection) } }
    var theoryPage: TheoryPage = .filter { didSet { notify(.theory) } }
    var activeExperiment: String? { didSet { notify(.theory) } }

    private(set) var banner: Banner?
    private var dismissedBanners: Set<String> = []
    private var micProblem: String?

    private var observers: [UUID: (ModelChange) -> Void] = [:]
    private var lastUndoRegistration = 0.0
    private var saveScheduled = false

    init() {
        filter = DigitalFilter.passthrough(fs: 48000)
        restore()
        audio.onStatus = { [weak self] message in
            if let message { self?.showBanner(Banner(text: message, style: .warning, id: "audio")) }
        }
        filter = buildFilter(spec, fs: fs)
    }

    func start() {
        audio.params.frequency.value = frequency
        audio.params.micGain.value = micGain
        audio.params.volume.value = volume
        audio.params.listen.value = listen ? 1 : 0
        audio.params.filterOn.value = filterOn ? 1 : 0
        audio.source = source
        audio.start(fs: fs)
        redesign()
        if !UserDefaults.standard.bool(forKey: "seenListenTip") {
            showBanner(Banner(text: "Click 🔈 Listen (top right) to hear the filter. Press Space to switch between the original and filtered sound.",
                              style: .info, actionTitle: "Got it", id: "listenTip"))
        }
    }

    // MARK: Observation

    @discardableResult
    func observe(_ handler: @escaping (ModelChange) -> Void) -> UUID {
        let id = UUID()
        observers[id] = handler
        return id
    }

    func notify(_ change: ModelChange) {
        for handler in observers.values { handler(change) }
        if !change.isDisjoint(with: [.filter, .source, .output, .display, .sampleRate]) { scheduleSave() }
    }

    // MARK: Design

    /// Replaces the design. Rapid changes (a slider drag) collapse into one undo step.
    func setSpec(_ new: DesignSpec, undoable: Bool = true) {
        let clamped = new.clamped(fs: fs)
        guard clamped != spec else { return }
        if undoable {
            let now = CACurrentMediaTime()
            if now - lastUndoRegistration > 0.6 {
                let old = spec
                undoManager.registerUndo(withTarget: self) { $0.restoreSpec(old) }
                undoManager.setActionName("Change Filter")
            }
            lastUndoRegistration = now
        }
        spec = clamped
        redesign()
    }

    func updateSpec(_ change: (inout DesignSpec) -> Void) {
        var s = spec
        change(&s)
        setSpec(s)
    }

    private func restoreSpec(_ s: DesignSpec) {
        let current = spec
        undoManager.registerUndo(withTarget: self) { $0.restoreSpec(current) }
        lastUndoRegistration = 0
        spec = s.clamped(fs: fs)
        redesign()
    }

    private func redesign() {
        designGeneration += 1
        var f = buildFilter(spec, fs: fs)
        zerosPending = false
        if case .fir(let h) = f.structure {
            zerosPending = true
            f.zpk.poles = []
            rootGeneration.value = designGeneration
            let generation = designGeneration
            let token = rootGeneration
            rootQueue.async { [weak self] in
                // Skip stale requests: only the newest design is worth solving.
                guard token.value == generation else { return }
                let zeros = firZeros(h)
                DispatchQueue.main.async {
                    guard let self, self.designGeneration == generation else { return }
                    self.filter.zpk.zeros = zeros
                    self.filter.zpk.poles = Array(repeating: .zero, count: zeros.count)
                    self.zerosPending = false
                    self.notify(.zeros)
                }
            }
        }
        filter = f
        audio.setFilter(f)
        updateStabilityBanner()
        notify(.filter)
    }

    var measurements: FilterMeasurements {
        if let c = cachedMeasurements, c.generation == designGeneration { return c.value }
        let m = measure(filter, band: spec.method == .poleZero ? nil : spec.band)
        cachedMeasurements = (designGeneration, m)
        return m
    }

    /// The design's poles/zeros become hand-editable items (switching to Pole–Zero mode).
    func convertToPoleZero() {
        guard spec.method != .poleZero else { return }
        var s = spec
        s.items = editableItems(from: filter.zpk)
        s.method = .poleZero
        s.presetName = "Edited \(spec.method == .fir ? "FIR" : spec.family.title) design"
        setSpec(s)
    }

    func applyPreset(_ preset: PoleZeroPreset) {
        var s = spec
        s.method = .poleZero
        s.items = preset.items(fs: fs)
        s.presetName = preset.title
        setSpec(s)
        selectedItem = nil
    }

    func setItems(_ items: [PZItem], undoable: Bool = true) {
        var s = spec
        s.items = items
        s.method = .poleZero
        setSpec(s, undoable: undoable)
    }

    func deleteSelectedItem() {
        guard spec.method == .poleZero, let id = selectedItem else { return }
        setItems(spec.items.filter { $0.id != id })
        selectedItem = nil
    }

    // MARK: Sample rate

    func setSampleRate(_ newFs: Double) {
        guard newFs != fs else { return }
        fs = newFs
        spec = spec.clamped(fs: fs)
        audio.start(fs: fs)
        if frequency > fs * 0.45 { frequency = fs * 0.2 }
        redesign()
        notify(.sampleRate)
    }

    // MARK: Source

    func setSource(_ newSource: SourceKind, window: NSWindow? = nil) {
        if newSource == .file && !audio.hasFile {
            openAudioFile(window: window)
            return
        }
        let previous = source
        if newSource == .microphone {
            if previous != .microphone { listen = false }
            source = .microphone
            audio.source = .microphone
            notify(.source)
            audio.startMic { [weak self] error in
                guard let self else { return }
                if let error {
                    self.micProblem = error == "denied"
                        ? "Filter Lab isn't allowed to use the microphone. Turn it on in System Settings → Privacy & Security → Microphone."
                        : error
                } else {
                    self.micProblem = nil
                    if !self.dismissedBanners.contains("micTip") {
                        self.showBanner(Banner(text: "Microphone on. Use headphones before turning on Listen, or the speakers will feed back into the mic.",
                                               style: .info, actionTitle: "OK", id: "micTip"))
                    }
                }
                self.updateMicBanner()
            }
        } else {
            if previous == .microphone { audio.stopMic() }
            micProblem = nil
            updateMicBanner()
            source = newSource
            audio.source = newSource
            notify(.source)
        }
    }

    func openAudioFile(window: NSWindow?) {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.audio]
        panel.message = "Choose a song or recording to run through your filter."
        let handler: (NSApplication.ModalResponse) -> Void = { [weak self] response in
            guard let self, response == .OK, let url = panel.url else {
                self?.notify(.source)
                return
            }
            self.loadFile(url)
        }
        if let window { panel.beginSheetModal(for: window, completionHandler: handler) } else { handler(panel.runModal()) }
    }

    func loadFile(_ url: URL) {
        do {
            if source == .microphone { audio.stopMic() }
            try audio.loadFile(url)
            source = .file
            notify(.source)
        } catch {
            showBanner(Banner(text: "Couldn't open “\(url.lastPathComponent)”: \(error.localizedDescription)", style: .warning,
                              actionTitle: "OK", id: "file"))
            notify(.source)
        }
    }

    // MARK: Banners

    func showBanner(_ b: Banner) {
        banner = b
        notify(.banner)
    }

    func dismissBanner() {
        guard let b = banner else { return }
        dismissedBanners.insert(b.id)
        if b.id == "listenTip" { UserDefaults.standard.set(true, forKey: "seenListenTip") }
        if b.id == "micDenied" { micProblem = nil }
        banner = nil
        updateStabilityBanner()
        notify(.banner)
    }

    func performBannerAction() {
        guard let b = banner else { return }
        if b.id == "micDenied", let url = URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone") {
            NSWorkspace.shared.open(url)
            return
        }
        dismissBanner()
    }

    private func updateStabilityBanner() {
        if !filter.isStable {
            let r = filter.zpk.maxPoleRadius
            banner = Banner(text: String(format: "Unstable: a pole is at |z| = %.3f, outside the unit circle. The impulse response grows forever, so the audio is muted.", r),
                            style: .danger, id: "unstable")
            notify(.banner)
        } else if banner?.id == "unstable" {
            banner = nil
            notify(.banner)
        }
    }

    private func updateMicBanner() {
        if let micProblem {
            banner = Banner(text: micProblem, style: .warning, actionTitle: "Open Settings", id: "micDenied")
        } else if banner?.id == "micDenied" {
            banner = nil
        }
        notify(.banner)
    }

    // MARK: Persistence

    private func scheduleSave() {
        guard !saveScheduled else { return }
        saveScheduled = true
        DispatchQueue.main.asyncAfter(deadline: .now() + 1) { [weak self] in
            self?.saveScheduled = false
            self?.save()
        }
    }

    func save() {
        let d = UserDefaults.standard
        if let data = try? JSONEncoder().encode(spec) { d.set(data, forKey: "spec") }
        d.set(fs, forKey: "fs")
        d.set(source == .microphone || source == .file ? SourceKind.music.rawValue : source.rawValue, forKey: "source")
        d.set(frequency, forKey: "frequency")
        d.set(volume, forKey: "volume")
        d.set(micGain, forKey: "micGain")
        d.set(logAxis, forKey: "logAxis")
        d.set(showHMap, forKey: "showHMap")
        d.set(liveView.rawValue, forKey: "liveView")
        d.set(responseMode.rawValue, forKey: "responseMode")
        d.set(scopeWindowMs, forKey: "scopeWindowMs")
    }

    private func restore() {
        let d = UserDefaults.standard
        if let data = d.data(forKey: "spec"), let s = try? JSONDecoder().decode(DesignSpec.self, from: data) { spec = s }
        let savedFs = d.double(forKey: "fs")
        if Self.sampleRates.contains(savedFs) { fs = savedFs }
        if d.object(forKey: "source") != nil, let s = SourceKind(rawValue: d.integer(forKey: "source")),
           s != .microphone && s != .file {
            source = s
        }
        if d.object(forKey: "frequency") != nil { frequency = min(max(d.double(forKey: "frequency"), 20), fs * 0.45) }
        if d.object(forKey: "volume") != nil { volume = d.double(forKey: "volume") }
        if d.object(forKey: "micGain") != nil { micGain = d.double(forKey: "micGain") }
        if d.object(forKey: "logAxis") != nil { logAxis = d.bool(forKey: "logAxis") }
        if d.object(forKey: "showHMap") != nil { showHMap = d.bool(forKey: "showHMap") }
        if let v = LiveView(rawValue: d.integer(forKey: "liveView")) { liveView = v }
        if let v = ResponseMode(rawValue: d.integer(forKey: "responseMode")) { responseMode = v }
        if d.object(forKey: "scopeWindowMs") != nil { scopeWindowMs = d.double(forKey: "scopeWindowMs") }
        spec = spec.clamped(fs: fs)
    }
}
