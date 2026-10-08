import AudioToolbox
import AVFoundation
import CoreAudio
import Foundation

/// Records the default input device with a bare AUHAL unit (input only) and writes mono
/// samples into a ring. Lower latency and fewer device restrictions than AVAudioEngine's
/// input node, which wants input and output on the same device.
final class MicCapture {
    let ring = SampleRing(capacity: 1 << 17)
    private(set) var sampleRate: Double = 0
    private var unit: AudioUnit?
    private var bufferList: UnsafeMutableAudioBufferListPointer?
    private var channels = 1
    private let mono: UnsafeMutablePointer<Float>
    private let filtered: UnsafeMutablePointer<Float>
    private var antiAlias: FilterKernel?
    private static let maxFrames = 4096

    init() {
        mono = .allocate(capacity: Self.maxFrames)
        filtered = .allocate(capacity: Self.maxFrames)
    }

    deinit {
        stop()
        mono.deallocate()
        filtered.deallocate()
    }

    enum CaptureError: LocalizedError {
        case noDevice, failed(String, OSStatus)
        var errorDescription: String? {
            switch self {
            case .noDevice: "No microphone was found."
            case .failed(let what, let status): "Couldn't start the microphone (\(what), error \(status))."
            }
        }
    }

    /// Starts recording. `targetRate` is the lab's processing rate; when it's lower than the
    /// microphone's rate an anti-aliasing filter runs before the samples are handed over.
    func start(targetRate: Double) throws {
        stop()
        var desc = AudioComponentDescription(
            componentType: kAudioUnitType_Output, componentSubType: kAudioUnitSubType_HALOutput,
            componentManufacturer: kAudioUnitManufacturer_Apple, componentFlags: 0, componentFlagsMask: 0)
        guard let comp = AudioComponentFindNext(nil, &desc) else { throw CaptureError.noDevice }
        var au: AudioUnit?
        try check(AudioComponentInstanceNew(comp, &au), "open")
        guard let au else { throw CaptureError.noDevice }
        unit = au

        var one: UInt32 = 1, zero: UInt32 = 0
        try check(AudioUnitSetProperty(au, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &one, 4), "enable input")
        try check(AudioUnitSetProperty(au, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &zero, 4), "disable output")

        var device = AudioDeviceID(0)
        var size = UInt32(MemoryLayout<AudioDeviceID>.size)
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDefaultInputDevice, mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain)
        try check(AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &address, 0, nil, &size, &device), "find device")
        guard device != 0 else { throw CaptureError.noDevice }
        try check(AudioUnitSetProperty(au, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &device, size), "select device")

        var hw = AudioStreamBasicDescription()
        size = UInt32(MemoryLayout<AudioStreamBasicDescription>.size)
        try check(AudioUnitGetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 1, &hw, &size), "read format")
        guard hw.mSampleRate > 0, hw.mChannelsPerFrame > 0 else { throw CaptureError.noDevice }
        sampleRate = hw.mSampleRate
        channels = Int(hw.mChannelsPerFrame)

        var client = AudioStreamBasicDescription(
            mSampleRate: hw.mSampleRate, mFormatID: kAudioFormatLinearPCM,
            mFormatFlags: kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved,
            mBytesPerPacket: 4, mFramesPerPacket: 1, mBytesPerFrame: 4, mChannelsPerFrame: hw.mChannelsPerFrame,
            mBitsPerChannel: 32, mReserved: 0)
        try check(AudioUnitSetProperty(au, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &client, size), "set format")

        var maxFrames = UInt32(Self.maxFrames)
        AudioUnitSetProperty(au, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxFrames, 4)

        let list = AudioBufferList.allocate(maximumBuffers: channels)
        for c in 0..<channels {
            list[c] = AudioBuffer(mNumberChannels: 1, mDataByteSize: UInt32(Self.maxFrames * 4),
                                  mData: UnsafeMutableRawPointer(UnsafeMutablePointer<Float>.allocate(capacity: Self.maxFrames)))
        }
        bufferList = list

        if targetRate < sampleRate * 0.98 {
            // Elliptic low-pass just below the new Nyquist frequency.
            var spec = DesignSpec()
            spec.method = .iir
            spec.family = .elliptic
            spec.order = 8
            spec.f1 = targetRate * 0.45
            spec.rippleDB = 0.1
            spec.stopDB = 90
            antiAlias = FilterKernel(buildFilter(spec, fs: sampleRate))
        } else {
            antiAlias = nil
        }

        var callback = AURenderCallbackStruct(
            inputProc: { refCon, flags, timeStamp, _, frames, _ in
                let capture = Unmanaged<MicCapture>.fromOpaque(refCon).takeUnretainedValue()
                return capture.didReceive(flags, timeStamp, frames)
            },
            inputProcRefCon: Unmanaged.passUnretained(self).toOpaque())
        try check(AudioUnitSetProperty(au, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 0, &callback,
                                       UInt32(MemoryLayout<AURenderCallbackStruct>.size)), "set callback")
        try check(AudioUnitInitialize(au), "initialise")
        try check(AudioOutputUnitStart(au), "start")
    }

    func stop() {
        if let unit {
            AudioOutputUnitStop(unit)
            AudioUnitUninitialize(unit)
            AudioComponentInstanceDispose(unit)
        }
        unit = nil
        if let list = bufferList {
            for b in list { b.mData?.deallocate() }
            free(list.unsafeMutablePointer)
        }
        bufferList = nil
    }

    private func check(_ status: OSStatus, _ what: String) throws {
        if status != noErr { throw CaptureError.failed(what, status) }
    }

    private func didReceive(_ flags: UnsafeMutablePointer<AudioUnitRenderActionFlags>,
                            _ timeStamp: UnsafePointer<AudioTimeStamp>, _ frames: UInt32) -> OSStatus {
        guard let unit, let list = bufferList else { return noErr }
        let n = min(Int(frames), Self.maxFrames)
        for c in 0..<channels { list[c].mDataByteSize = UInt32(n * 4) }
        let status = AudioUnitRender(unit, flags, timeStamp, 1, UInt32(n), list.unsafeMutablePointer)
        guard status == noErr else { return status }
        let scale = 1 / Float(channels)
        for i in 0..<n { mono[i] = 0 }
        for c in 0..<channels {
            guard let data = list[c].mData?.assumingMemoryBound(to: Float.self) else { continue }
            for i in 0..<n { mono[i] += data[i] * scale }
        }
        var offset = 0
        while offset < n {
            let m = min(n - offset, FilterKernel.maxBlock)
            if let antiAlias {
                antiAlias.process(mono + offset, filtered, m)
                ring.write(filtered, count: m)
            } else {
                ring.write(mono + offset, count: m)
            }
            offset += m
        }
        return noErr
    }
}

/// Reads the microphone ring on the output thread at the lab's sample rate. A 4-point
/// Hermite interpolator converts the rate, and the read speed is nudged so the buffer
/// stays about 40 ms full even though mic and speakers run on separate clocks.
final class MicReader {
    let ring: SampleRing
    private let nominalRatio: Double
    private var target: Double
    private var position = -1.0
    private var adjust = 1.0

    init(ring: SampleRing, micRate: Double, outputRate: Double) {
        self.ring = ring
        nominalRatio = micRate / outputRate
        target = micRate * 0.04 + 1024
    }

    func read(into dst: UnsafeMutablePointer<Float>, _ n: Int, gain: Float) {
        let written = Double(ring.totalWritten)
        // Keep at least two output blocks' worth of mic samples in reserve.
        target = max(target, Double(n) * nominalRatio * 2 + 256)
        if position < 0 || written - position > Double(ring.capacity) / 2 {
            position = written - target
        }
        let needed = Double(n) * nominalRatio * adjust + 4
        if written - position < needed {
            // Underrun (mic just started or stalled): wait for data to build up.
            dst.update(repeating: 0, count: n)
            position = max(0, written - target)
            return
        }
        let error = (written - position - target) / target
        adjust += (1 + max(-0.003, min(0.003, error * 0.01)) - adjust) * 0.05
        let step = nominalRatio * adjust
        for i in 0..<n {
            let base = Int(position)
            let t = Float(position - Double(base))
            let y0 = ring.sample(base - 1), y1 = ring.sample(base), y2 = ring.sample(base + 1), y3 = ring.sample(base + 2)
            let c1 = 0.5 * (y2 - y0)
            let c2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3
            let c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2)
            dst[i] = gain * (((c3 * t + c2) * t + c1) * t + y1)
            position += step
        }
    }
}
