import Foundation
import CoreVideo
import OpenGLES
import AVFoundation

// 定义支持的视频滤镜类型枚举
public enum VideoFilterType: Int {
    case brightness = 0
    case gaussianBlur = 1
    case lookup = 2
    case bilateral = 3
    case cinematicLookup = 4 // 电影级 3D LUT
    case computeBlur = 5 // Not supported on iOS, mapped for cross-platform API parity
    case nightVision = 6
}

// 引擎运行状态
public enum FilterEngineState {
    case stopped, initializing, running, degraded
    case error(Error)
}

/**
 * 跨平台实时视频滤镜的 iOS 端门面类 (Facade)。
 *
 * Threading Model:
 * 1. Swift `actor`: The use of `actor` ensures that all calls to this class (and thus to the
 *    underlying C++ engine) are serialized. This prevents race conditions on the non-thread-safe
 *    OpenGL/EAGLContext.
 * 2. Serial Access: Methods like `addFilter`, `processFrame`, and `release` are guaranteed to
 *    execute one at a time, fulfilling the Core Engine's single-thread constraint.
 * 3. AsyncStream: Provides a non-blocking way for UI layers to consume processed frames.
 */

public struct PerformanceMetrics {
    public let averageFrameTimeMs: Float
    public let p50FrameTimeMs: Float
    public let p90FrameTimeMs: Float
    public let p99FrameTimeMs: Float
    public let droppedFrames: Int
}
public actor VideoFilterManager {

    // 底层 C++ / Objective-C++ 引擎包装器
    private let engine: FilterEngine
    private var context: EAGLContext?

    // 录制器
    private var videoEncoder: VideoEncoder?

    public private(set) var state: FilterEngineState = .stopped

    // AsyncStream 的发送端
    private var streamContinuation: AsyncStream<Result<CVPixelBuffer, Error>>.Continuation?

    /**
     * 核心输出流：接收者可以通过 `for await result in processedFrames` 不断获取处理后的帧。
     */
    public let processedFrames: AsyncStream<Result<CVPixelBuffer, Error>>

    public init() {
        self.engine = FilterEngine()
        var continuation: AsyncStream<Result<CVPixelBuffer, Error>>.Continuation!
        self.processedFrames = AsyncStream { cont in
            continuation = cont
        }
        self.streamContinuation = continuation
    }

    /// 初始化底层滤镜引擎，传入已设置好 OpenGL ES 3.0 的 EAGLContext
    public func initialize(context: EAGLContext) {
        self.state = .initializing
        self.context = context

        let result = engine.initialize(context: context)
        if result == 0 {
            self.state = .running
        } else {
            let error = NSError(domain: "VideoFilterManager", code: Int(result), userInfo: [NSLocalizedDescriptionKey: "Failed to initialize FilterEngine. Error code: \(result)"])
            self.state = .error(error)
        }
    }

    /**
     * Primary entry point for frame processing.
     * @note Serialized by the Actor. This method triggers the underlying C++ processFrame.
     *
     * - Parameter pixelBuffer: Raw frame from camera (e.g. AVCaptureVideoDataOutput).
     */
    public func processFrame(_ pixelBuffer: CVPixelBuffer, timestamp: CMTime? = nil) {
        let finalTimestamp = timestamp ?? CMClockGetTime(CMClockGetHostTimeClock())

        guard case .running = state else {
            // 降级策略：如果引擎未初始化或者崩溃，直接使用 yield 原样返回输入的 pixelBuffer，
            // 这确保了哪怕特效引擎坏了，用户的相机画面也不会黑屏 (Bypass 原图)。
            streamContinuation?.yield(.success(pixelBuffer))

            if let encoder = videoEncoder, encoder.isRecording {
                encoder.appendVideoPixelBuffer(pixelBuffer, timestamp: finalTimestamp)
            }
            return
        }

        if let processedBuffer = engine.processFrame(pixelBuffer) {
            // 处理成功，将带特效的 Buffer 发送进 AsyncStream 队列
            streamContinuation?.yield(.success(processedBuffer))

            // 如果正在录制，将处理后的帧写入编码器
            if let encoder = videoEncoder, encoder.isRecording {
                encoder.appendVideoPixelBuffer(processedBuffer, timestamp: finalTimestamp)
            }
        } else {
            // 处理失败 (例如内部 simulateCrash 被触发，或者 GPU 显存耗尽)。
            let errorCode = engine.lastErrorCode
            handleError(Int(errorCode))

            // Bypass 原图以防黑屏
            streamContinuation?.yield(.success(pixelBuffer))

            if let encoder = videoEncoder, encoder.isRecording {
                encoder.appendVideoPixelBuffer(pixelBuffer, timestamp: finalTimestamp)
            }
        }
    }

    private func handleError(_ errorCode: Int) {
        // ERROR: fatal (-1000 to -1999)
        // DEGRADED: recoverable (-2000 to -4999)
        if errorCode <= -1000 && errorCode >= -1999 {
            let error = NSError(domain: "VideoFilterManager", code: errorCode, userInfo: [NSLocalizedDescriptionKey: "Fatal engine error: \(errorCode)"])
            self.state = .error(error)
        } else if errorCode <= -2000 && errorCode >= -4999 {
            self.state = .degraded
        }
    }

    /// 动态添加滤镜到处理管线
    public func addFilter(_ type: VideoFilterType) throws {
        guard case .running = state else { return }
        guard let swiftType = SwiftFilterType(rawValue: type.rawValue) else { return }
        let res = engine.addFilter(swiftType)
        if res != 0 {
            throw NSError(domain: "VideoFilterManager", code: Int(res), userInfo: [NSLocalizedDescriptionKey: "Add filter failed"])
        }
    }

    /// 移除所有滤镜
    public func removeAllFilters() throws {
        guard case .running = state else { return }
        let res = engine.removeAllFilters()
        if res != 0 {
            throw NSError(domain: "VideoFilterManager", code: Int(res), userInfo: [NSLocalizedDescriptionKey: "Remove all filters failed"])
        }
    }

    /// 实时更新滤镜的浮点参数（例如调节 intensity）
    public func updateParameter(key: String, value: Float) {
        guard case .running = state else { return }
        engine.updateParameterFloat(key: key, value: value)
    }

    /// 实时更新滤镜的整型参数
    public func updateParameter(key: String, value: Int) {
        guard case .running = state else { return }
        engine.updateParameterInt(key: key, value: value)
    }


    /// 开始录制
    public func startVideoRecording(config: VideoExportConfig) throws {
        let encoder = VideoEncoder(config: config)
        try encoder.startRecording()
        self.videoEncoder = encoder
    }

    /// 停止录制
    public func stopVideoRecording(completion: ((URL?) -> Void)? = nil) {
        if let encoder = videoEncoder {
            encoder.stopRecording(isFallback: false, completion: completion)
            self.videoEncoder = nil
        } else {
            completion?(nil)
        }
    }

    /// 供外部 (如 AVCaptureAudioDataOutput) 传入麦克风 PCM
    public func appendAudioSampleBuffer(_ sampleBuffer: CMSampleBuffer) {
        if let encoder = videoEncoder, encoder.isRecording {
            encoder.appendAudioSampleBuffer(sampleBuffer)
        }
    }

    /// 释放引擎及 AsyncStream

    public func getPerformanceMetrics() -> PerformanceMetrics? {
        guard let arr = engine.getPerformanceMetrics() else { return nil }
        if arr.count == 5 {
            return PerformanceMetrics(
                averageFrameTimeMs: arr[0].floatValue,
                p50FrameTimeMs: arr[1].floatValue,
                p90FrameTimeMs: arr[2].floatValue,
                p99FrameTimeMs: arr[3].floatValue,
                droppedFrames: arr[4].intValue
            )
        }
        return nil
    }

    public func recordDroppedFrame() {
        engine.recordDroppedFrame()
    }

    public func release() {
        if let encoder = videoEncoder {
            encoder.stopRecording(isFallback: false)
            self.videoEncoder = nil
        }
        engine.releaseEngine()
        streamContinuation?.finish()
        self.state = .stopped
    }
}
