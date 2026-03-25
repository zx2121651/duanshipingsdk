import Foundation
import CoreVideo
import OpenGLES

public enum VideoFilterType: Int {
    case brightness = 0
    case gaussianBlur = 1
    case lookup = 2
    case bilateral = 3
}

/// Facade class to handle the video filter pipeline using Swift Concurrency.
/// An `actor` provides thread-safe access to the underlying `FilterEngine` and EAGLContext.
public actor VideoFilterManager {

    private let engine: FilterEngine
    private var isInitialized = false

    public init() {
        self.engine = FilterEngine()
    }

    /// Initialize the underlying filter engine on the actor's execution context.
    /// - Parameter context: An EAGLContext configured for OpenGL ES 3.0.
    public func initialize(context: EAGLContext) async throws {
        guard !isInitialized else { return }
        engine.initialize(context: context)
        isInitialized = true
    }

    /// Process a video frame through the filter pipeline.
    /// This method is async and can be awaited by AVCaptureVideoDataOutput's delegate queue.
    /// - Parameter pixelBuffer: The input camera frame.
    /// - Returns: The processed pixel buffer.
    public func processFrame(_ pixelBuffer: CVPixelBuffer) async throws -> CVPixelBuffer {
        guard isInitialized else {
            return pixelBuffer // Return original if not initialized
        }

        // Ensure processFrame is thread-safe within the actor context
        if let processedBuffer = engine.processFrame(pixelBuffer) {
            return processedBuffer
        }

        return pixelBuffer // Fallback to original
    }

    /// Add a filter to the current pipeline.
    public func addFilter(_ type: VideoFilterType) async {
        guard isInitialized else { return }
        // Ensure swift enums match internal ones
        guard let swiftType = SwiftFilterType(rawValue: type.rawValue) else { return }
        engine.addFilter(swiftType)
    }

    /// Clear all filters from the current pipeline.
    public func removeAllFilters() async {
        guard isInitialized else { return }
        engine.removeAllFilters()
    }

    /// Update a float parameter for the active filter.
    public func updateParameter(key: String, value: Float) async {
        guard isInitialized else { return }
        engine.updateParameterFloat(key: key, value: value)
    }

    /// Update an integer parameter for the active filter.
    public func updateParameter(key: String, value: Int) async {
        guard isInitialized else { return }
        engine.updateParameterInt(key: key, value: value)
    }

    /// Clean up the underlying resources and GL states.
    public func release() async {
        guard isInitialized else { return }
        engine.releaseEngine()
        isInitialized = false
    }
}
