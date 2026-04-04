import Foundation
import CoreVideo
import OpenGLES

@objc public enum SwiftFilterType: Int {
    case brightness = 0
    case gaussianBlur = 1
    case lookup = 2
    case bilateral = 3
    case cinematicLookup = 4
    case computeBlur = 5
}

@objc public class FilterEngine: NSObject {

    private let wrapper: FilterEngineWrapper
    private var context: EAGLContext?

    public override init() {
        self.wrapper = FilterEngineWrapper()
        super.init()
    }

    // Call on GL thread
    @objc public func initialize(context: EAGLContext) -> Int32 {
        self.context = context
        return wrapper.initialize(with: context)
    }

    // Call on GL thread to process frames from AVFoundation/Camera
    @objc public func processFrame(_ pixelBuffer: CVPixelBuffer) -> CVPixelBuffer? {
        return wrapper.processFrame(pixelBuffer)
    }

    // Update float parameter on the fly
    @objc public func updateParameterFloat(key: String, value: Float) {
        wrapper.updateParameterFloat(key, value: value)
    }

    // Update integer parameter on the fly (e.g. texture IDs)
    @objc public func updateParameterInt(key: String, value: Int) {
        wrapper.updateParameterInt(key, value: Int32(value))
    }

    // Add filter to the pipeline
    @objc public func addFilter(_ type: SwiftFilterType) {
        // Assume mapping matches FilterType
        wrapper.addFilter(FilterType(rawValue: type.rawValue)!)
    }

    // Clear pipeline
    @objc public func removeAllFilters() {
        wrapper.removeAllFilters()
    }

    // Free resources
    @objc public func releaseEngine() {
        wrapper.releaseResources()
    }

    deinit {
        releaseEngine()
    }
}
