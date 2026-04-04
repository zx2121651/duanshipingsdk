import Foundation
import CoreVideo
import OpenGLES

// 定义支持的视频滤镜类型枚举
public enum VideoFilterType: Int {
    case brightness = 0
    case gaussianBlur = 1
    case lookup = 2
    case bilateral = 3
    case cinematicLookup = 4 // 电影级 3D LUT
    case computeBlur = 5 // Not supported on iOS, mapped for cross-platform API parity
}

// 引擎运行状态
public enum FilterEngineState {
    case stopped, initializing, running, degraded
    case error(Error)
}

/**
 * 跨平台实时视频滤镜的 iOS 端门面类 (Facade)。
 * 核心设计：
 * 1. 使用 Swift `actor`。在 iOS 的 OpenGL/EAGLContext 编程中，跨线程访问 Context 是极其危险的。
 *    Actor 可以确保内部所有的滤镜添加、销毁和状态修改都在一个串行的执行上下文中，完美解决了线程安全问题。
 * 2. 引入 `AsyncStream` 供上层消费者（SwiftUI 视图）非阻塞地获取最新帧。
 */
public actor VideoFilterManager {

    // 底层 C++ / Objective-C++ 引擎包装器
    private let engine: FilterEngine
    private var context: EAGLContext?

    public private(set) var state: FilterEngineState = .stopped

    // AsyncStream 的发送端
    private var streamContinuation: AsyncStream<Result<CVPixelBuffer, Error>>.Continuation?

    /**
     * 核心输出流：接收者可以通过 `for await result in processedFrames` 不断获取处理后的帧。
     * 使用 nonisolated 修饰，这样外部订阅这个流时不需要等待 Actor 的锁。
     */
    public nonisolated let processedFrames: AsyncStream<Result<CVPixelBuffer, Error>>

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
     * 视频帧处理的核心入口。通常由 AVCaptureVideoDataOutput 的 delegate 回调。
     * - Parameter pixelBuffer: 相机硬件直出的原始视频帧
     */
    public func processFrame(_ pixelBuffer: CVPixelBuffer) {
        guard case .running = state else {
            // 降级策略：如果引擎未初始化或者崩溃，直接使用 yield 原样返回输入的 pixelBuffer，
            // 这确保了哪怕特效引擎坏了，用户的相机画面也不会黑屏 (Bypass 原图)。
            streamContinuation?.yield(.success(pixelBuffer))
            return
        }

        if let processedBuffer = engine.processFrame(pixelBuffer) {
            // 处理成功，将带特效的 Buffer 发送进 AsyncStream 队列
            streamContinuation?.yield(.success(processedBuffer))
        } else {
            // 处理失败 (例如内部 simulateCrash 被触发，或者 GPU 显存耗尽)。
            // 将状态标为 degraded，并 Bypass 原图。
            self.state = .degraded
            streamContinuation?.yield(.success(pixelBuffer))
        }
    }

    /// 动态添加滤镜到处理管线
    public func addFilter(_ type: VideoFilterType) {
        guard case .running = state else { return }
        guard let swiftType = SwiftFilterType(rawValue: type.rawValue) else { return }
        engine.addFilter(swiftType)
    }

    /// 移除所有滤镜
    public func removeAllFilters() {
        guard case .running = state else { return }
        engine.removeAllFilters()
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

    /// 释放引擎及 AsyncStream
    public func release() {
        engine.releaseEngine()
        streamContinuation?.finish()
        self.state = .stopped
    }
}
