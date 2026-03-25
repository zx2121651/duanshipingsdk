import SwiftUI
import MetalKit
import CoreImage

/// A SwiftUI view that renders `CVPixelBuffer`s processed by `VideoFilterManager`.
public struct FilterCameraView: View {
    let filterManager: VideoFilterManager

    @State private var intensity: Float = 1.0

    public init(filterManager: VideoFilterManager) {
        self.filterManager = filterManager
    }

    public var body: some View {
        ZStack(alignment: .bottom) {

            // Video Preview Render View
            CameraPreviewRepresentable(filterManager: filterManager)
                .edgesIgnoringSafeArea(.all)
                .task {
                    // Initialize and add the cinematic filter
                    await filterManager.addFilter(.cinematicLookup)
                }

            // Intensity Controls
            VStack {
                Text("Cinematic Intensity: \(String(format: "%.2f", intensity))")
                    .foregroundColor(.white)
                    .shadow(radius: 2)

                Slider(value: Binding(get: {
                    self.intensity
                }, set: { newValue in
                    self.intensity = newValue
                    Task {
                        await filterManager.updateParameter(key: "intensity", value: newValue)
                    }
                }), in: 0.0...1.0)
                .padding()

                // Debug Degradation Trigger
                Button(action: {
                    Task {
                        await filterManager.updateParameter(key: "simulateCrash", value: 1.0)
                    }
                }) {
                    Text("Simulate Overload")
                        .font(.caption)
                        .foregroundColor(.red)
                }
                .padding(.bottom, 30)
            }
            .padding()
            .background(Color.black.opacity(0.4))
        }
    }
}

/// Helper Representable for MTKView
struct CameraPreviewRepresentable: UIViewRepresentable {
    let filterManager: VideoFilterManager

    func makeUIView(context: Context) -> MTKView {
        let mtkView = MTKView()
        mtkView.device = MTLCreateSystemDefaultDevice()
        mtkView.framebufferOnly = false
        mtkView.delegate = context.coordinator
        mtkView.clearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: 1)

        let task = Task {
            for await result in filterManager.processedFrames {
                switch result {
                case .success(let pixelBuffer):
                    context.coordinator.currentPixelBuffer = pixelBuffer
                    mtkView.draw()
                case .failure(let error):
                    print("Video render degradation triggered: \(error)")
                }
            }
        }

        context.coordinator.task = task
        return mtkView
    }

    func updateUIView(_ uiView: MTKView, context: Context) {}

    func makeCoordinator() -> Coordinator {
        Coordinator()
    }

    static func dismantleUIView(_ uiView: MTKView, coordinator: Coordinator) {
        coordinator.task?.cancel()
    }

    class Coordinator: NSObject, MTKViewDelegate {
        var currentPixelBuffer: CVPixelBuffer?
        var task: Task<Void, Never>?

        lazy var ciContext = CIContext(mtlDevice: MTLCreateSystemDefaultDevice()!)
        let colorSpace = CGColorSpaceCreateDeviceRGB()

        func draw(in view: MTKView) {
            guard let pixelBuffer = currentPixelBuffer,
                  let drawable = view.currentDrawable,
                  let commandBuffer = ciContext.commandQueue?.makeCommandBuffer() else {
                return
            }

            let image = CIImage(cvPixelBuffer: pixelBuffer)
            let viewSize = view.drawableSize
            let destinationBounds = CGRect(x: 0, y: 0, width: viewSize.width, height: viewSize.height)

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

        func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}
    }
}
