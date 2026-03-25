import Foundation
import CoreVideo
import OpenGLES

public enum VideoFilterType: Int {
    case brightness = 0
    case gaussianBlur = 1
    case lookup = 2
    case bilateral = 3
}

public enum FilterEngineState {
    case stopped, initializing, running, degraded
    case error(Error)
}

/// Facade class to handle the video filter pipeline using Swift Concurrency.
/// An `actor` provides thread-safe access to the underlying `FilterEngine` and EAGLContext.
public actor VideoFilterManager {

    private let engine: FilterEngine
    private var context: EAGLContext?

    public private(set) var state: FilterEngineState = .stopped

    private var streamContinuation: AsyncStream<Result<CVPixelBuffer, Error>>.Continuation?

    /// Stream of processed frames, async consumed by UI
    public nonisolated let processedFrames: AsyncStream<Result<CVPixelBuffer, Error>>

    public init() {
        self.engine = FilterEngine()
        var continuation: AsyncStream<Result<CVPixelBuffer, Error>>.Continuation!
        self.processedFrames = AsyncStream { cont in
            continuation = cont
        }
        self.streamContinuation = continuation
    }

    /// Initialize the underlying filter engine on the actor's execution context.
    /// - Parameter context: An EAGLContext configured for OpenGL ES 3.0.
    public func initialize(context: EAGLContext) {
        do {
            self.state = .initializing
            self.context = context
            engine.initialize(context: context)
            self.state = .running
        } catch {
            self.state = .error(error)
        }
    }

    /// Process a video frame through the filter pipeline.
    /// This method can be called from AVCaptureVideoDataOutput's delegate queue.
    /// - Parameter pixelBuffer: The input camera frame.
    public func processFrame(_ pixelBuffer: CVPixelBuffer) {
        guard case .running = state else {
            // Degrade: if not running/errored, yield original frame
            streamContinuation?.yield(.success(pixelBuffer))
            return
        }

        if let processedBuffer = engine.processFrame(pixelBuffer) {
            streamContinuation?.yield(.success(processedBuffer))
        } else {
            // Degrade: failed processing, yield original frame
            self.state = .degraded
            streamContinuation?.yield(.success(pixelBuffer))
        }
    }

    /// Add a filter to the current pipeline.
    public func addFilter(_ type: VideoFilterType) {
        guard case .running = state else { return }
        // Ensure swift enums match internal ones
        guard let swiftType = SwiftFilterType(rawValue: type.rawValue) else { return }
        engine.addFilter(swiftType)
    }

    /// Clear all filters from the current pipeline.
    public func removeAllFilters() {
        guard case .running = state else { return }
        engine.removeAllFilters()
    }

    /// Update a float parameter for the active filter.
    public func updateParameter(key: String, value: Float) {
        guard case .running = state else { return }
        engine.updateParameterFloat(key: key, value: value)
    }

    /// Update an integer parameter for the active filter.
    public func updateParameter(key: String, value: Int) {
        guard case .running = state else { return }
        engine.updateParameterInt(key: key, value: value)
    }

    /// Clean up the underlying resources and GL states.
    public func release() {
        engine.releaseEngine()
        streamContinuation?.finish()
        self.state = .stopped
    }
}
