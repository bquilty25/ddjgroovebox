// SPDX-License-Identifier: GPL-3.0-only
// The instrument's audio to the Mac's output: an AVAudioSourceNode pulling the instrument's render function at its
// own sample rate (AVAudioEngine converts to the device's). The instruments run their sequencers (and SLOOP its whole
// UI) inside that render, so the render must never stop for long:
//   - the engine is restarted when the output device changes (a device switched, the DDJ-400's audio unplugged);
//   - while no audio callback comes (no device, a failed start), a fallback thread renders into a discard buffer at
//     real-time pace, so the instrument keeps running, silently.
import AVFoundation
import CoreAudio
import Foundation

/// Renders interleaved stereo float frames; host_time is the mach time the first frame will be heard at.
typealias RenderFunction = (UnsafeMutablePointer<Float>, UInt32, UInt64) -> Void

final class AudioOutput: @unchecked Sendable {
    private let engine = AVAudioEngine()
    private var source: AVAudioSourceNode?
    private let maxFrames = 8192
    private let scratch: UnsafeMutablePointer<Float>
    private let lock = NSLock()
    private var render: RenderFunction?
    private(set) var sampleRate: Double = 48000
    private var lastCallback: UInt64 = 0                 // mach time of the last audio callback (atomic enough: 64-bit)
    private var fallback: Thread?
    private var fallbackRunning = false
    private var configObserver: NSObjectProtocol?
    /// a short message when audio is not running (nil when fine), for the window
    private(set) var problem: String?
    var onProblemChange: ((String?) -> Void)?

    static let ticksPerSecond: Double = {
        var tb = mach_timebase_info_data_t()
        mach_timebase_info(&tb)
        return 1e9 * Double(tb.denom) / Double(tb.numer)
    }()

    init() {
        scratch = .allocate(capacity: maxFrames * 2)
        scratch.initialize(repeating: 0, count: maxFrames * 2)
        configObserver = NotificationCenter.default.addObserver(forName: .AVAudioEngineConfigurationChange, object: engine,
                                                                queue: .main) { [weak self] _ in self?.restart() }
    }

    deinit {
        if let configObserver { NotificationCenter.default.removeObserver(configObserver) }
        stopFallback()
        engine.stop()
        scratch.deallocate()
    }

    /// Play `render` at `rate` (replaces what played before).
    func start(rate: Double, render: @escaping RenderFunction) {
        lock.lock(); self.render = render; lock.unlock()
        sampleRate = rate
        rebuild()
        startFallback()
    }

    func stop() {
        stopFallback()
        engine.stop()
    }

    /// seconds of output latency (device + buffer): the time between a render and its sound. Measured when audio
    /// (re)starts: asking for it costs a round trip to the audio server (it was a sixth of the UI's time at 60 a second)
    private(set) var outputLatency: Double = 0.01

    private func rebuild() {
        engine.stop()
        if let source { engine.detach(source) }
        let format = AVAudioFormat(standardFormatWithSampleRate: sampleRate, channels: 2)!
        let scratch = self.scratch, maxFrames = self.maxFrames
        let node = AVAudioSourceNode(format: format) { [unowned self] _, timestamp, frameCount, bufferList -> OSStatus in
            let abl = UnsafeMutableAudioBufferListPointer(bufferList)
            let host = timestamp.pointee.mHostTime
            self.lastCallback = mach_absolute_time()
            self.lock.lock()
            let render = self.render
            self.lock.unlock()
            var done = 0
            let total = Int(frameCount)
            let tps = Self.ticksPerSecond / self.sampleRate
            while done < total {
                let n = min(total - done, maxFrames)
                render?(scratch, UInt32(n), host &+ UInt64(Double(done) * tps))
                if abl.count >= 2, let l = abl[0].mData?.assumingMemoryBound(to: Float.self),
                   let r = abl[1].mData?.assumingMemoryBound(to: Float.self) {
                    for i in 0..<n {
                        l[done + i] = scratch[2 * i]
                        r[done + i] = scratch[2 * i + 1]
                    }
                } else if abl.count == 1, let d = abl[0].mData?.assumingMemoryBound(to: Float.self) {
                    for i in 0..<n { d[done + i] = 0.5 * (scratch[2 * i] + scratch[2 * i + 1]) }
                }
                done += n
            }
            return noErr
        }
        source = node
        engine.attach(node)
        engine.connect(node, to: engine.mainMixerNode, format: format)
        engine.mainMixerNode.outputVolume = 1
        setLowLatency(frames: 256)
        engine.prepare()
        do {
            try engine.start()
            outputLatency = engine.outputNode.presentationLatency
            setProblem(nil)
        } catch {
            setProblem("NO AUDIO OUTPUT: \(error.localizedDescription.uppercased())")
            DispatchQueue.main.asyncAfter(deadline: .now() + 2) { [weak self] in self?.restartIfStopped() }
        }
    }

    /// the output changed (device switched or gone): connect again, keep the instrument running
    private func restart() {
        rebuild()
    }

    private func restartIfStopped() {
        if !engine.isRunning { rebuild() }
    }

    private func setProblem(_ p: String?) {
        guard p != problem else { return }
        problem = p
        onProblemChange?(p)
    }

    // MARK: the fallback clock

    private func startFallback() {
        guard fallback == nil else { return }
        fallbackRunning = true
        let t = Thread { [weak self] in self?.fallbackLoop() }
        t.name = "audio fallback clock"
        t.qualityOfService = .userInitiated
        fallback = t
        t.start()
    }

    private func stopFallback() {
        fallbackRunning = false
        fallback = nil
    }

    /// renders in real time into a discard buffer while the audio callback is silent for more than 100 ms
    private func fallbackLoop() {
        let block = 256
        let discard = UnsafeMutablePointer<Float>.allocate(capacity: block * 2)
        defer { discard.deallocate() }
        while fallbackRunning {
            let silentFor = Double(mach_absolute_time() &- lastCallback) / Self.ticksPerSecond
            if silentFor > 0.1 {
                lock.lock(); let render = self.render; lock.unlock()
                render?(discard, UInt32(block), mach_absolute_time())
                Thread.sleep(forTimeInterval: Double(block) / sampleRate)
                if silentFor > 1, engine.isRunning == false { DispatchQueue.main.async { [weak self] in self?.flagStopped() } }
            } else {
                Thread.sleep(forTimeInterval: 0.05)
            }
        }
    }

    private func flagStopped() {
        if problem == nil { setProblem("AUDIO STOPPED: RECONNECTING") }
        restartIfStopped()
    }

    /// Ask the output device for small I/O buffers: pads play without a noticeable delay.
    private func setLowLatency(frames: UInt32) {
        guard let unit = engine.outputNode.audioUnit else { return }
        var device = AudioDeviceID(0)
        var size = UInt32(MemoryLayout<AudioDeviceID>.size)
        guard AudioUnitGetProperty(unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &device, &size) == noErr
        else { return }
        var addr = AudioObjectPropertyAddress(mSelector: kAudioDevicePropertyBufferFrameSize,
                                              mScope: kAudioObjectPropertyScopeGlobal,
                                              mElement: kAudioObjectPropertyElementMain)
        var value = frames
        AudioObjectSetPropertyData(device, &addr, 0, nil, UInt32(MemoryLayout<UInt32>.size), &value)
    }
}
