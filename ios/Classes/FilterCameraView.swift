
import SwiftUI
import OpenGLES
import GLKit
import CoreVideo

/**
 * 声明式 UI 层的 SwiftUI 视图。
 * 它通过 UIViewRepresentable 包装了一个基于 CAEAGLLayer 的自定义 UIView。
 * 为了完全对齐跨平台 "OpenGL ES 3.0" 的核心渲染原则，我们放弃使用 Metal/CoreImage 进行上屏，
 * 改用原生的 OpenGL ES 渲染闭环。
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

class EAGLView: UIView {
    var context: EAGLContext?
    var renderBuffer: GLuint = 0
    var frameBuffer: GLuint = 0

    // CoreVideo Texture Cache for on-screen drawing
    var textureCache: CVOpenGLESTextureCache?

    override class var layerClass: AnyClass {
        return CAEAGLLayer.self
    }

    func setupGL() {
        guard let eaglLayer = self.layer as? CAEAGLLayer else { return }
        eaglLayer.isOpaque = true
        eaglLayer.drawableProperties = [
            kEAGLDrawablePropertyRetainedBacking: false,
            kEAGLDrawablePropertyColorFormat: kEAGLColorFormatRGBA8
        ]

        context = EAGLContext(api: .openGLES3)
        if context == nil {
            context = EAGLContext(api: .openGLES2)
        }

        guard let context = context else { return }
        EAGLContext.setCurrent(context)

        glGenFramebuffers(1, &frameBuffer)
        glBindFramebuffer(GLenum(GL_FRAMEBUFFER), frameBuffer)

        glGenRenderbuffers(1, &renderBuffer)
        glBindRenderbuffer(GLenum(GL_RENDERBUFFER), renderBuffer)

        context.renderbufferStorage(Int(GL_RENDERBUFFER), from: eaglLayer)
        glFramebufferRenderbuffer(GLenum(GL_FRAMEBUFFER), GLenum(GL_COLOR_ATTACHMENT0), GLenum(GL_RENDERBUFFER), renderBuffer)

        CVOpenGLESTextureCacheCreate(kCFAllocatorDefault, nil, context, nil, &textureCache)
    }

    func displayPixelBuffer(_ pixelBuffer: CVPixelBuffer) {
        guard let context = context, let textureCache = textureCache else { return }

        EAGLContext.setCurrent(context)
        glBindFramebuffer(GLenum(GL_FRAMEBUFFER), frameBuffer)
        glViewport(0, 0, GLsizei(bounds.size.width * contentScaleFactor), GLsizei(bounds.size.height * contentScaleFactor))

        let width = CVPixelBufferGetWidth(pixelBuffer)
        let height = CVPixelBufferGetHeight(pixelBuffer)

        var cvTexture: CVOpenGLESTexture?
        CVOpenGLESTextureCacheCreateTextureFromImage(
            kCFAllocatorDefault,
            textureCache,
            pixelBuffer,
            nil,
            GLenum(GL_TEXTURE_2D),
            GL_RGBA,
            GLsizei(width),
            GLsizei(height),
            GLenum(GL_BGRA),
            GLenum(GL_UNSIGNED_BYTE),
            0,
            &cvTexture
        )

        guard let cvTexture = cvTexture else { return }
        let textureName = CVOpenGLESTextureGetName(cvTexture)

        // Use a simple shader to draw the texture, or just blit if supported
        // Since we are moving fast to align with architecture, we use FBO blitting (requires GLES 3.0)

        var readFBO: GLuint = 0
        glGenFramebuffers(1, &readFBO)
        glBindFramebuffer(GLenum(GL_READ_FRAMEBUFFER), readFBO)
        glFramebufferTexture2D(GLenum(GL_READ_FRAMEBUFFER), GLenum(GL_COLOR_ATTACHMENT0), GLenum(GL_TEXTURE_2D), textureName, 0)

        glBindFramebuffer(GLenum(GL_DRAW_FRAMEBUFFER), frameBuffer)

        // Perform Blit
        glBlitFramebuffer(
            0, 0, GLint(width), GLint(height),
            0, 0, GLint(bounds.size.width * contentScaleFactor), GLint(bounds.size.height * contentScaleFactor),
            GLbitfield(GL_COLOR_BUFFER_BIT), GLenum(GL_LINEAR)
        )

        // Cleanup read FBO
        glBindFramebuffer(GLenum(GL_READ_FRAMEBUFFER), 0)
        glDeleteFramebuffers(1, &readFBO)

        // Present Renderbuffer
        glBindRenderbuffer(GLenum(GL_RENDERBUFFER), renderBuffer)
        context.presentRenderbuffer(Int(GL_RENDERBUFFER))

        CVOpenGLESTextureCacheFlush(textureCache, 0)
    }

    deinit {
        if frameBuffer != 0 { glDeleteFramebuffers(1, &frameBuffer) }
        if renderBuffer != 0 { glDeleteRenderbuffers(1, &renderBuffer) }
        if EAGLContext.current() == context {
            EAGLContext.setCurrent(nil)
        }
    }
}

/// 包装 EAGLView 以供 SwiftUI 使用
struct CameraPreviewRepresentable: UIViewRepresentable {
    let filterManager: VideoFilterManager

    func makeUIView(context: Context) -> EAGLView {
        let glView = EAGLView()
        glView.setupGL()

        let task = Task {
            for await result in filterManager.processedFrames {
                switch result {
                case .success(let pixelBuffer):
                    DispatchQueue.main.async {
                        glView.displayPixelBuffer(pixelBuffer)
                    }
                case .failure(let error):
                    print("Video render degradation triggered: \(error)")
                }
            }
        }

        context.coordinator.task = task
        return glView
    }

    func updateUIView(_ uiView: EAGLView, context: Context) {}

    func makeCoordinator() -> Coordinator {
        Coordinator()
    }

    static func dismantleUIView(_ uiView: EAGLView, coordinator: Coordinator) {
        coordinator.task?.cancel()
    }

    class Coordinator: NSObject {
        var task: Task<Void, Never>?
    }
}
