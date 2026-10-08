import Foundation

/// Short lessons shown in the Theory panel. Paragraph markup: lines starting with "• " are
/// bullets, "= " lines are equations, "Try: " lines are hints.
struct Lesson {
    let id: String
    let title: String
    let summary: String
    let body: [String]
    let experiments: [String]
}

enum Lessons {
    static let all: [Lesson] = [
        Lesson(
            id: "zplane", title: "The z-plane in one minute",
            summary: "Where frequencies live, and why poles and zeros shape the response.",
            body: [
                "A digital filter's transfer function can be factored into its zeros zᵢ and poles pᵢ:",
                "= H(z) = k · (z − z₁)(z − z₂)… / ((z − p₁)(z − p₂)…)",
                "Real signals at frequency f become points on the unit circle, z = e^{jω} with ω = 2πf / fs. 0 Hz sits at z = 1. The Nyquist frequency fs/2 sits at z = −1, and the frequencies in between go anticlockwise along the top half.",
                "On the circle, every factor (z − zᵢ) is just a vector from the zero to the point. So:",
                "= |H(e^{jω})| = k × (product of distances to the zeros) / (product of distances to the poles)",
                "• A zero close to the circle pulls the response down near its angle. Exactly on the circle it makes a perfect notch.",
                "• A pole close to the circle pushes the response up near its angle, giving a resonance.",
                "• Poles and zeros at the origin are the same distance from every point on the circle, so they only add delay.",
                "Try: hover over the Response plot. A dot moves round the circle, and lines join it to every pole and zero, so you can see this product of distances working live.",
            ],
            experiments: ["resonator", "notch"]),
        Lesson(
            id: "stability", title: "Stability: inside the circle",
            summary: "Why every pole must stay inside |z| = 1.",
            body: [
                "Each pole p adds a term like pⁿ to the impulse response h[n]. What happens next depends only on |p|:",
                "• |p| < 1: the term decays. The filter is stable.",
                "• |p| = 1: it rings forever (a marginally stable oscillator).",
                "• |p| > 1: it grows without limit. The filter is unstable.",
                "So a causal LTI filter is BIBO-stable exactly when all its poles are strictly inside the unit circle.",
                "The distance to the circle sets how long the filter rings. A pole at radius r decays like rⁿ, so its time constant is about 1/(1 − r) samples. At r = 0.99 that's about 100 samples (2 ms at 48 kHz).",
                "FIR filters have all their poles at the origin, so they are always stable.",
                "Try: in Pole–Zero mode, drag a pole outside the circle. The impulse response (Response → Impulse) grows exponentially, and Filter Lab mutes the audio to protect your ears.",
            ],
            experiments: ["resonator"]),
        Lesson(
            id: "families", title: "Butterworth, Chebyshev, Elliptic, Bessel",
            summary: "Four classic answers to the same trade-off.",
            body: [
                "All the IIR families here start from an analog low-pass prototype. They make different trade-offs between a flat passband, a steep transition and a well-behaved phase.",
                "• Butterworth: maximally flat passband, no ripple anywhere. |H(jΩ)|² = 1 / (1 + (Ω/Ωc)^{2N}). It is −3 dB at the cutoff, and the roll-off approaches −20N dB per decade. Its analog poles sit evenly on a circle.",
                "• Chebyshev I: allows ripple of Rp dB in the passband. In return it is much steeper than Butterworth at the same order. Its poles sit on an ellipse.",
                "• Chebyshev II (inverse Chebyshev): flat passband, with ripple in the stopband that stays below −Rs dB. It has finite zeros, which land on the unit circle and make notches in the stopband.",
                "• Elliptic (Cauer): ripple in both bands. It gives the narrowest transition possible for a given order, at the cost of the most phase distortion.",
                "• Bessel: makes the group delay as flat as possible, so waveforms keep their shape. The roll-off is the gentlest of the five.",
                "Rule of thumb: when the shape of a pulse matters, use Bessel or a linear-phase FIR. When you need a sharp cut, use elliptic. When you want no ripple, use Butterworth.",
            ],
            experiments: ["families", "order"]),
        Lesson(
            id: "bilinear", title: "From analog to digital: the bilinear transform",
            summary: "How MATLAB's butter() and SciPy's signal.butter() really work.",
            body: [
                "Classic IIR design reuses decades of analog filter theory in three steps:",
                "• 1. Take the analog low-pass prototype H(s) with a cutoff of 1 rad/s (Butterworth, Chebyshev, …).",
                "• 2. Transform it into the band you want: s → s/Ωc for a low-pass, s → Ωc/s for a high-pass, and s → (s² + Ω₀²)/(B·s) for a band-pass.",
                "• 3. Map it to digital with the bilinear transform:",
                "= s = 2·fs · (z − 1) / (z + 1)",
                "This maps the whole jΩ axis onto the unit circle exactly once. That means no aliasing, and a stable analog filter (left half-plane) always gives a stable digital one (inside the circle).",
                "The price is frequency warping: analog Ω and digital ω are related by",
                "= Ω = 2·fs · tan(ω/2)",
                "So the design pre-warps the cutoff to Ωc = 2·fs·tan(π·fc/fs). Then the digital cutoff lands exactly at fc.",
                "Infinite analog frequency (s = ∞) maps to z = −1. That is why a Butterworth low-pass has all N of its zeros stacked at z = −1. Look at the z-plane: a single ○ marked with N.",
                "Filter Lab follows these exact steps, and its results match SciPy to about 12 significant digits.",
            ],
            experiments: ["order"]),
        Lesson(
            id: "fir", title: "FIR filters and the window method",
            summary: "Truncated sinc, Gibbs ripples and window trade-offs.",
            body: [
                "An FIR (finite impulse response) filter is a weighted moving sum with no feedback:",
                "= y[n] = Σₖ h[k] · x[n − k],   k = 0 … N−1",
                "The ideal low-pass has an infinitely long impulse response, h[n] = 2fc/fs · sinc(2fc·n/fs). The window method keeps N samples of it, centred, and multiplies them by a smooth window w[n].",
                "• Rectangular (plain truncation): the sharpest transition, but Gibbs ripples leave only about 21 dB of stopband attenuation.",
                "• Hann ≈ 44 dB, Hamming ≈ 53 dB, Blackman ≈ 74 dB: each gets smoother, with more attenuation and a wider transition.",
                "• Kaiser: one parameter β slides between these. A larger β gives more attenuation and a wider transition.",
                "The transition width shrinks roughly like 1/N. Doubling the taps halves it, but costs twice the computation and twice the delay.",
                "Because the taps are symmetric (h[k] = h[N−1−k]), the phase is exactly linear. Every frequency is delayed by the same (N−1)/2 samples.",
                "The zeros of a linear-phase FIR come in groups: z, 1/z and their conjugates. Watch them line up on the unit circle across the stopband.",
            ],
            experiments: ["windows", "linear-phase"]),
        Lesson(
            id: "phase", title: "Phase, group delay and waveform shape",
            summary: "Why a filter can change a square wave's shape without changing its spectrum.",
            body: [
                "Group delay is how long each frequency is held up by the filter:",
                "= τ(ω) = −dφ(ω)/dω   (samples)",
                "If τ is the same at every frequency (linear phase), the output is just a delayed copy of the input's surviving components, and shapes are preserved. Symmetric FIR filters do this exactly.",
                "IIR filters have non-linear phase. Their group delay peaks near the cutoff, where the poles are closest to the circle. The sharper the filter (high-order elliptic), the bigger the peak.",
                "That's why a square wave through an elliptic low-pass rings lopsidedly, while through a linear-phase FIR the ringing is symmetric before and after each edge.",
                "Try: Response → Group delay, then compare Butterworth, Elliptic and Bessel at the same order. Then look at a square wave on the Scope.",
            ],
            experiments: ["linear-phase"]),
        Lesson(
            id: "displays", title: "Reading the live displays",
            summary: "Spectrum, spectrogram and scope, and what they're measuring.",
            body: [
                "• Spectrum: an FFT of the latest ~85 ms (4096 points at 48 kHz) with a Hann window. 0 dBFS is a full-scale sine. Grey is the input, teal is the output (what you hear). The vertical gap between them is the filter's gain at each frequency.",
                "• With white noise in, the output spectrum takes the shape of |H(f)|. This is how filters are measured on the bench. Turn on “Predicted” to overlay input + |H| in dB and see theory match measurement.",
                "• Spectrogram: many short FFTs side by side. Time runs to the right, frequency goes up, and colour shows level. The 8-second sweep draws a rising line, and a low-pass makes it fade above fc.",
                "• Scope: the time-domain input and output, triggered on the input's rising edge. Look for the delay, the phase shift and the ringing.",
                "• The time–frequency trade-off: a longer FFT gives finer frequency resolution (fs/N) but blurrier timing. That's the uncertainty principle of the STFT.",
            ],
            experiments: ["sweep", "order"]),
        Lesson(
            id: "sampling", title: "Sampling rate and Nyquist",
            summary: "fs sets the playground: everything lives between 0 and fs/2.",
            body: [
                "A signal sampled at fs can only represent frequencies up to the Nyquist frequency fs/2. Anything higher folds back down (aliasing). That's why microphones are low-pass filtered before sampling. Filter Lab does the same when you pick a lower fs for the mic.",
                "Digital frequency is relative: ω = 2πf/fs. The same pole–zero picture means different frequencies in Hz at different sample rates. Try switching fs in Pole–Zero mode: the poles stay put, and the frequencies in Hz scale.",
                "Telephone speech uses fs = 8 kHz, so the bandwidth is under 4 kHz. CD audio uses 44.1 kHz, and studios use 48 kHz.",
                "Try: set fs = 8 kHz with the music loop. Even with the filter off, the hats and sparkle are gone.",
            ],
            experiments: ["fs"]),
    ]

    static func lesson(_ id: String) -> Lesson? { all.first { $0.id == id } }
}

/// Guided experiments: each sets up the lab in one click.
struct Experiment {
    let id: String
    let title: String
    let description: String
}

enum Experiments {
    static let all: [Experiment] = [
        Experiment(id: "lowpass-music", title: "Hear a low-pass filter",
                   description: "Music through a 4th-order Butterworth low-pass at 800 Hz. Drag the cutoff in the Response plot and press Space to compare with the original."),
        Experiment(id: "hum", title: "Remove 50 Hz mains hum",
                   description: "Music with a hum from badly earthed equipment, cleaned by five narrow notches at 50, 100, 150, 200 and 250 Hz. Toggle Space: the buzz goes and the music stays. Scroll to zoom into z = 1."),
        Experiment(id: "order", title: "Order vs steepness",
                   description: "White noise through a Butterworth low-pass at 2 kHz. Raise the Order and watch the slope steepen by 20 dB per decade per order."),
        Experiment(id: "families", title: "Compare filter families",
                   description: "4th order, 1 kHz, white noise. Switch the Family and compare ripple, steepness and group delay."),
        Experiment(id: "windows", title: "Windows and Gibbs ripples",
                   description: "A 63-tap FIR with a rectangular window: about 21 dB of stopband. Switch to Hamming and then Blackman, and watch the sidelobes drop as the transition widens."),
        Experiment(id: "linear-phase", title: "Linear vs non-linear phase",
                   description: "A 200 Hz square wave through a 101-tap FIR low-pass on the Scope. The ringing is symmetric. Then switch the Method to IIR (elliptic): the ringing becomes lopsided."),
        Experiment(id: "resonator", title: "Make it ring",
                   description: "Clicks through a resonator: a pole pair at r = 0.995 and 1 kHz. Each click makes a quiet ‘ping’, so turn the volume up. Drag the pole towards the circle for a longer ring, and past it to go unstable."),
        Experiment(id: "notch", title: "A notch from one pair of zeros",
                   description: "A sine at 1 kHz with zeros on the unit circle at 1 kHz. Move the frequency slider and hear the tone vanish as the dot reaches the zero."),
        Experiment(id: "telephone", title: "Telephone voice",
                   description: "Your microphone through a 300–3400 Hz band-pass, the classic telephone channel. Wear headphones, then turn on Listen."),
        Experiment(id: "comb", title: "Comb filter",
                   description: "Sixteen poles evenly spaced round the circle give the metallic, robotic tone of y[n] = x[n] + 0.8·y[n−16]."),
        Experiment(id: "sweep", title: "See the response with a sweep",
                   description: "An 8-second sweep through a Chebyshev I low-pass on the spectrogram. The line fades above 2 kHz, and the passband ripple shows as a wobble."),
        Experiment(id: "fs", title: "Lower the sampling rate",
                   description: "Switch to fs = 8 kHz, as in telephone systems. Everything above 4 kHz is gone, even with the filter bypassed."),
    ]

    static func experiment(_ id: String) -> Experiment? { all.first { $0.id == id } }
}

extension LabModel {
    func runExperiment(_ id: String) {
        var s = DesignSpec()
        var source: SourceKind = .music
        var live: LiveView = .spectrum
        var response: ResponseMode = .magnitude
        var wantListen = true
        switch id {
        case "lowpass-music":
            s.method = .iir; s.family = .butterworth; s.order = 4; s.f1 = 800
        case "hum":
            s.method = .poleZero; s.items = PoleZeroPreset.humRemover.items(fs: fs); s.presetName = PoleZeroPreset.humRemover.title
            source = .musicHum
        case "order":
            s.method = .iir; s.family = .butterworth; s.order = 2; s.f1 = 2000
            source = .whiteNoise; wantListen = false
        case "families":
            s.method = .iir; s.family = .elliptic; s.order = 4; s.f1 = 1000; s.rippleDB = 1; s.stopDB = 60
            source = .whiteNoise; wantListen = false
        case "windows":
            s.method = .fir; s.taps = 63; s.window = .rectangular; s.f1 = 2000
            source = .whiteNoise; wantListen = false
        case "linear-phase":
            s.method = .fir; s.taps = 101; s.window = .hamming; s.f1 = 2000
            s.family = .elliptic; s.order = 6; s.rippleDB = 1; s.stopDB = 60
            source = .square; frequency = 200; live = .scope; scopeWindowMs = 20
        case "resonator":
            s.method = .poleZero; s.items = PoleZeroPreset.resonator.items(fs: fs); s.presetName = PoleZeroPreset.resonator.title
            source = .clicks; live = .scope; scopeWindowMs = 20; response = .impulse
        case "notch":
            s.method = .poleZero; s.items = PoleZeroPreset.notch.items(fs: fs); s.presetName = PoleZeroPreset.notch.title
            source = .sine; frequency = 700
        case "telephone":
            s.method = .iir; s.family = .butterworth; s.band = .bandpass; s.order = 4; s.f1 = 300; s.f2 = 3400
            source = .microphone; wantListen = false
        case "comb":
            s.method = .poleZero; s.items = PoleZeroPreset.comb.items(fs: fs); s.presetName = PoleZeroPreset.comb.title
        case "sweep":
            s.method = .iir; s.family = .chebyshev1; s.order = 6; s.f1 = 2000; s.rippleDB = 1
            source = .sweep; live = .spectrogram
        case "fs":
            setSampleRate(8000)
            s = spec
            s.method = .iir
            filterOn = false
        default:
            return
        }
        setSpec(s)
        selectedItem = nil
        setSource(source)
        liveView = live
        responseMode = response
        if id != "fs" { filterOn = true }
        if wantListen && source != .microphone { listen = true }
        if id == "fs" { listen = true }
        activeExperiment = id
    }
}
