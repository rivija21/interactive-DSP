import Foundation
import Synchronization
import os

// Plumbing for passing data to and from the real-time audio threads without blocking them.

/// A Double that can be read and written from any thread.
final class SharedDouble: @unchecked Sendable {
    private let bits: Atomic<UInt64>

    init(_ value: Double) {
        bits = Atomic(value.bitPattern)
    }

    var value: Double {
        get { Double(bitPattern: bits.load(ordering: .relaxed)) }
        set { bits.store(newValue.bitPattern, ordering: .relaxed) }
    }
}

final class SharedInt: @unchecked Sendable {
    private let storage: Atomic<Int>

    init(_ value: Int) {
        storage = Atomic(value)
    }

    var value: Int {
        get { storage.load(ordering: .relaxed) }
        set { storage.store(newValue, ordering: .relaxed) }
    }
}

/// A ring of Float samples with one writer. Readers either consume in order (the mic
/// path) or just look at the most recent samples (the analysis displays).
final class SampleRing: @unchecked Sendable {
    let capacity: Int
    private let mask: Int
    private let storage: UnsafeMutablePointer<Float>
    private let written = Atomic<Int>(0)

    init(capacity: Int) {
        precondition(capacity > 0 && capacity & (capacity - 1) == 0, "capacity must be a power of two")
        self.capacity = capacity
        mask = capacity - 1
        storage = .allocate(capacity: capacity)
        storage.initialize(repeating: 0, count: capacity)
    }

    deinit {
        storage.deallocate()
    }

    /// Total samples ever written. Sample i (absolute) lives at storage[i & mask].
    var totalWritten: Int { written.load(ordering: .acquiring) }

    func write(_ src: UnsafePointer<Float>, count n: Int) {
        let w = written.load(ordering: .relaxed)
        var i = 0
        while i < n {
            let idx = (w + i) & mask
            let chunk = min(n - i, capacity - idx)
            (storage + idx).update(from: src + i, count: chunk)
            i += chunk
        }
        written.store(w + n, ordering: .releasing)
    }

    @inline(__always) func sample(_ absoluteIndex: Int) -> Float {
        storage[absoluteIndex & mask]
    }

    /// Copies samples [start, start + count) (absolute indices) into dst.
    func copy(from start: Int, count: Int, into dst: UnsafeMutablePointer<Float>) {
        var i = 0
        while i < count {
            let idx = (start + i) & mask
            let chunk = min(count - i, capacity - idx)
            dst.advanced(by: i).update(from: storage + idx, count: chunk)
            i += chunk
        }
    }

    /// Copies the most recent `count` samples; returns the absolute index just past the last one.
    @discardableResult
    func copyLatest(_ count: Int, into dst: UnsafeMutablePointer<Float>) -> Int {
        let end = totalWritten
        copy(from: end - count, count: count, into: dst)
        return end
    }
}

/// Hands objects (filter kernels, audio loops) from the main thread to an audio thread.
/// The audio thread never blocks and never frees memory: the object it replaces is parked
/// in `retired` and released later on the main thread.
final class Exchange<T: AnyObject>: @unchecked Sendable {
    private let lock: UnsafeMutablePointer<os_unfair_lock>
    private var pending: T?
    private var retired: T?

    init() {
        lock = .allocate(capacity: 1)
        lock.initialize(to: os_unfair_lock())
    }

    deinit {
        lock.deallocate()
    }

    /// Main thread: queue `object` to replace whatever the audio thread is using.
    func publish(_ object: T) {
        var garbage: (T?, T?)
        os_unfair_lock_lock(lock)
        garbage = (pending, retired)
        pending = object
        retired = nil
        os_unfair_lock_unlock(lock)
        _ = garbage // released here, outside the lock
    }

    /// Main thread: release whatever the audio thread has finished with.
    func drain() {
        var garbage: T?
        os_unfair_lock_lock(lock)
        garbage = retired
        retired = nil
        os_unfair_lock_unlock(lock)
        _ = garbage
    }

    /// Audio thread: swap in the pending object if there is one. Never blocks.
    @inline(__always)
    func take(into current: inout T?) -> Bool {
        guard os_unfair_lock_trylock(lock) else { return false }
        defer { os_unfair_lock_unlock(lock) }
        guard pending != nil, retired == nil else { return false }
        retired = current
        current = pending
        pending = nil
        return true
    }
}

/// A block of mono audio that loops (the synthesized music or a loaded file).
final class LoopBuffer {
    let samples: UnsafeMutablePointer<Float>
    let count: Int
    let name: String

    init(_ data: [Float], name: String) {
        let n = max(1, data.count)
        let memory = UnsafeMutablePointer<Float>.allocate(capacity: n)
        memory.initialize(repeating: 0, count: n)
        data.withUnsafeBufferPointer { src in
            if let base = src.baseAddress { memory.update(from: base, count: data.count) }
        }
        count = n
        samples = memory
        self.name = name
    }

    deinit {
        samples.deallocate()
    }
}
