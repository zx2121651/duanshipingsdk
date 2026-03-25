import SwiftUI
import MetalKit
import CoreImage

/// A SwiftUI view that renders `CVPixelBuffer`s processed by `VideoFilterManager`.
public struct FilterCameraView: UIViewRepresentable {
    let filterManager: VideoFilterManager

    public init(filterManager: VideoFilterManager) {
        self.filterManager = filterManager
    }

    public func makeUIView(context: Context) -> MTKView {
        let mtkView = MTKView()
        mtkView.device = MTLCreateSystemDefaultDevice()
        mtkView.framebufferOnly = false
        mtkView.delegate = context.coordinator
        mtkView.clearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: 1)

        // Start consuming the frames stream
        let task = Task {
            for await result in filterManager.processedFrames {
                switch result {
                case .success(let pixelBuffer):
                    context.coordinator.currentPixelBuffer = pixelBuffer
                    // Manually trigger a display cycle to render the new buffer
                    mtkView.draw()
                case .failure(let error):
                    // In a production app, handle degradation (e.g. logging)
                    print("Video render degradation triggered: \(error)")
                }
            }
        }

        context.coordinator.task = task

        return mtkView
    }

    public func updateUIView(_ uiView: MTKView, context: Context) {
        // Handle changes in view state if necessary
    }

    public func makeCoordinator() -> Coordinator {
        Coordinator()
    }

    /// Handle cleanup when the view disappears
    public static func dismantleUIView(_ uiView: MTKView, coordinator: Coordinator) {
        coordinator.task?.cancel()
    }

    public class Coordinator: NSObject, MTKViewDelegate {
        var currentPixelBuffer: CVPixelBuffer?
        var task: Task<Void, Never>?

        // Use CIContext to render CoreVideo buffers to the MTKView drawable
        lazy var ciContext = CIContext(mtlDevice: MTLCreateSystemDefaultDevice()!)
        let colorSpace = CGColorSpaceCreateDeviceRGB()

        public func draw(in view: MTKView) {
            guard let pixelBuffer = currentPixelBuffer,
                  let drawable = view.currentDrawable,
                  let commandBuffer = ciContext.commandQueue?.makeCommandBuffer() else {
                return
            }

            let image = CIImage(cvPixelBuffer: pixelBuffer)

            // Assuming we fit the camera feed into the screen; adjust aspect ratio as needed
            let viewSize = view.drawableSize
            let destinationBounds = CGRect(x: 0, y: 0, width: viewSize.width, height: viewSize.height)

            // Efficiently draw the CVPixelBuffer onto the Metal texture
            ciContext.render(
                image,
                to: drawable.texture,
                commandBuffer: commandBuffer,
                bounds: destinationBounds,
                colorSpace: colorSpace
            )

            commandBuffer.present(drawable)
            commandBuffer.commit()
        }

        public func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
            // Layout changes or rotations can be handled here
        }
    }
}
