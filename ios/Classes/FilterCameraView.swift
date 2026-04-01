import SwiftUI
import MetalKit
import CoreImage

/**
 * 声明式 UI 层的 SwiftUI 视图。
 * 它通过 UIViewRepresentable 包装了 MetalKit 的 MTKView。
 * 为什么用 MTKView 和 CIContext？
 * 因为处理后的 CVPixelBuffer 在 iOS 上最好的渲染方式就是转成 CIImage，
 * 然后用 Metal GPU 零拷贝地渲染到屏幕上。这比传统的 OpenGL GLKView 要高效得多。
 */
public struct FilterCameraView: View {
    let filterManager: VideoFilterManager

    @State private var intensity: Float = 1.0

    public init(filterManager: VideoFilterManager) {
        self.filterManager = filterManager
    }

    public var body: some View {
        ZStack(alignment: .bottom) {

            // 底部视频渲染区域
            CameraPreviewRepresentable(filterManager: filterManager)
                .edgesIgnoringSafeArea(.all)
                .task {
                    // 视图挂载时，默认加载电影级 LUT 滤镜
                    await filterManager.addFilter(.cinematicLookup)
                }

            // 顶层 UI 控制浮层
            VStack {
                Text("Cinematic Intensity: \(String(format: "%.2f", intensity))")
                    .foregroundColor(.white)
                    .shadow(radius: 2)

                // Slider 绑定：用户滑动时，通过 Actor 的 async 接口实时更新底层 C++ 的 uniform 参数
                Slider(value: Binding(get: {
                    self.intensity
                }, set: { newValue in
                    self.intensity = newValue
                    Task {
                        await filterManager.updateParameter(key: "intensity", value: newValue)
                    }
                }), in: 0.0...1.0)
                .padding()

                // 故障模拟器：触发底层返回空纹理，从而测试 UI 层的防黑屏 Bypass 降级逻辑
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

/// 包装 MTKView 以供 SwiftUI 使用
struct CameraPreviewRepresentable: UIViewRepresentable {
    let filterManager: VideoFilterManager

    func makeUIView(context: Context) -> MTKView {
        let mtkView = MTKView()
        mtkView.device = MTLCreateSystemDefaultDevice() // 获取 iOS 设备的默认 Metal GPU
        mtkView.framebufferOnly = false
        mtkView.delegate = context.coordinator
        mtkView.clearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: 1)

        // 开启一个后台 Task，持续从 AsyncStream 中获取底层抛出的视频帧
        let task = Task {
            for await result in filterManager.processedFrames {
                switch result {
                case .success(let pixelBuffer):
                    context.coordinator.currentPixelBuffer = pixelBuffer
                    // 手动通知 MTKView 在下一次屏幕刷新时触发 draw() 回调
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

        // CoreImage 的上下文，利用传入的 Metal Device 进行高性能渲染
        lazy var ciContext = CIContext(mtlDevice: MTLCreateSystemDefaultDevice()!)
        let colorSpace = CGColorSpaceCreateDeviceRGB()

        // MTKView 的实际绘制回调
        func draw(in view: MTKView) {
            guard let pixelBuffer = currentPixelBuffer,
                  let drawable = view.currentDrawable,
                  let commandBuffer = ciContext.commandQueue?.makeCommandBuffer() else {
                return
            }

            // 将 CVPixelBuffer 转为 CIImage (零内存拷贝)
            let image = CIImage(cvPixelBuffer: pixelBuffer)
            let viewSize = view.drawableSize
            let destinationBounds = CGRect(x: 0, y: 0, width: viewSize.width, height: viewSize.height)

            // 将 CIImage 绘制到 MTKView 提供的可绘制纹理 (drawable.texture) 上
            ciContext.render(
                image,
                to: drawable.texture,
                commandBuffer: commandBuffer,
                bounds: destinationBounds,
                colorSpace: colorSpace
            )

            // 提交渲染指令并展示在屏幕上
            commandBuffer.present(drawable)
            commandBuffer.commit()
        }

        func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}
    }
}
