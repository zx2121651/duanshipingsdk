/**
 * TimelinePreviewBridge.cpp
 *
 * JNI 桥接层：在 GLSurfaceView 的 GL 线程上调用 Compositor::renderFrameAtTime，
 * 将时间线当前帧直接渲染到 GLSurfaceView 的默认帧缓冲（FBO 0）。
 *
 * 线程约束：
 *   nativeCreate / nativeRenderFrame / nativeRelease 必须全部在同一个 GL 线程调用。
 *   (GLSurfaceView.Renderer 的三个回调均在同一 GL 线程，满足此约束。)
 */

#include <jni.h>
#include <android/log.h>
#include <memory>
#include <GLES3/gl3.h>

#include "../../../../core/include/FilterEngine.h"
#include "../../../../core/include/FrameBuffer.h"
#include "../../../../core/include/timeline/Timeline.h"
#include "../../../../core/include/timeline/Compositor.h"
#include "../../../../core/include/timeline/DecoderPool.h"

#define LOG_TAG "TimelinePreviewBridge"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace sdk::video;
using namespace sdk::video::timeline;

// ---------------------------------------------------------------------------
// PreviewContext — per-surface state, lives on the GL thread
// ---------------------------------------------------------------------------
struct PreviewContext {
    std::shared_ptr<Timeline>     timeline;
    std::shared_ptr<FilterEngine> filterEngine;
    std::shared_ptr<Compositor>   compositor;
    std::shared_ptr<DecoderPool>  decoderPool;

    // Wraps the GLSurfaceView default framebuffer (FBO 0).
    // Updated by nativeRenderFrame when the surface dimensions change.
    std::shared_ptr<FrameBuffer>  defaultFb;

    int surfaceWidth  = 0;
    int surfaceHeight = 0;
};

// ---------------------------------------------------------------------------
// Helper: clear the current FBO to black
// ---------------------------------------------------------------------------
static void clearBlack(int w, int h) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

// ---------------------------------------------------------------------------
// JNI implementation
// ---------------------------------------------------------------------------
extern "C" {

/**
 * Create the preview context. Must be called on the GL thread after EGL surface is ready
 * (i.e., from GLSurfaceView.Renderer.onSurfaceCreated / onSurfaceChanged).
 *
 * @param timelineHandle  Pointer to std::shared_ptr<Timeline> managed by TimelineManager.kt
 * @param width           Initial surface width
 * @param height          Initial surface height
 * @return               Native handle (PreviewContext*), or 0 on failure
 */
JNIEXPORT jlong JNICALL
Java_com_sdk_video_timeline_TimelinePreviewBridge_nativeCreate(
    JNIEnv* env, jobject /*thiz*/, jlong timelineHandle, jint width, jint height)
{
    if (timelineHandle == 0L) {
        LOGE("nativeCreate: null timelineHandle");
        return 0L;
    }

    auto* timelinePtr = reinterpret_cast<std::shared_ptr<Timeline>*>(timelineHandle);
    if (!timelinePtr || !(*timelinePtr)) {
        LOGE("nativeCreate: invalid timeline pointer");
        return 0L;
    }

    auto ctx = std::make_unique<PreviewContext>();
    ctx->timeline = *timelinePtr;
    ctx->surfaceWidth  = static_cast<int>(width);
    ctx->surfaceHeight = static_cast<int>(height);

    // Create a dedicated FilterEngine for the preview surface.
    // This must be called on the GL thread so that EGL is current.
    ctx->filterEngine = std::make_shared<FilterEngine>();
    auto initRes = ctx->filterEngine->initialize();
    if (!initRes.isOk()) {
        LOGE("nativeCreate: FilterEngine::initialize() failed [%d]: %s",
             initRes.getErrorCode(), initRes.getMessage().c_str());
        return 0L;
    }

    ctx->compositor  = std::make_shared<Compositor>(ctx->timeline, ctx->filterEngine);
    ctx->decoderPool = std::make_shared<DecoderPool>();
    ctx->compositor->setDecoderPool(ctx->decoderPool);

    // Wrap FBO 0 (the GLSurfaceView default framebuffer).
    ctx->defaultFb = std::make_shared<FrameBuffer>(
        ctx->surfaceWidth, ctx->surfaceHeight, static_cast<GLuint>(0));

    LOGD("nativeCreate: preview context ready (%dx%d)", ctx->surfaceWidth, ctx->surfaceHeight);
    return reinterpret_cast<jlong>(ctx.release());
}

/**
 * Update surface dimensions and re-wrap FBO 0. Call from onSurfaceChanged.
 */
JNIEXPORT void JNICALL
Java_com_sdk_video_timeline_TimelinePreviewBridge_nativeSurfaceChanged(
    JNIEnv* /*env*/, jobject /*thiz*/, jlong handle, jint width, jint height)
{
    auto* ctx = reinterpret_cast<PreviewContext*>(handle);
    if (!ctx) return;

    ctx->surfaceWidth  = static_cast<int>(width);
    ctx->surfaceHeight = static_cast<int>(height);

    // Re-wrap the default framebuffer with the new dimensions.
    ctx->defaultFb = std::make_shared<FrameBuffer>(
        ctx->surfaceWidth, ctx->surfaceHeight, static_cast<GLuint>(0));

    glViewport(0, 0, ctx->surfaceWidth, ctx->surfaceHeight);
    LOGD("nativeSurfaceChanged: %dx%d", ctx->surfaceWidth, ctx->surfaceHeight);
}

/**
 * Render one frame at the given timeline position. Call from onDrawFrame.
 *
 * @param positionNs  Current playback position in nanoseconds.
 */
JNIEXPORT void JNICALL
Java_com_sdk_video_timeline_TimelinePreviewBridge_nativeRenderFrame(
    JNIEnv* /*env*/, jobject /*thiz*/, jlong handle, jlong positionNs)
{
    auto* ctx = reinterpret_cast<PreviewContext*>(handle);
    if (!ctx || !ctx->compositor || !ctx->defaultFb) {
        return;
    }

    glViewport(0, 0, ctx->surfaceWidth, ctx->surfaceHeight);

    auto res = ctx->compositor->renderFrameAtTime(
        static_cast<int64_t>(positionNs), ctx->defaultFb);

    if (!res.isOk()) {
        // Decoder not available yet (media not registered or FFmpeg missing).
        // Fall back to black — not a crash, just a silent frame.
        clearBlack(ctx->surfaceWidth, ctx->surfaceHeight);
    }
}

/**
 * Register a video clip with the decoder pool so the Compositor can fetch its frames.
 * Thread-safe — may be called from any thread (including the Compose main thread).
 *
 * @param clipId     Clip identifier (must match what was passed to TimelineManager.addClip)
 * @param sourcePath Absolute path or content URI of the video file
 */
JNIEXPORT void JNICALL
Java_com_sdk_video_timeline_TimelinePreviewBridge_nativeRegisterClip(
    JNIEnv* env, jobject /*thiz*/, jlong handle, jstring clipId, jstring sourcePath)
{
    auto* ctx = reinterpret_cast<PreviewContext*>(handle);
    if (!ctx || !ctx->decoderPool) return;

    const char* id   = env->GetStringUTFChars(clipId,     nullptr);
    const char* path = env->GetStringUTFChars(sourcePath, nullptr);
    if (id && path) {
        ctx->decoderPool->registerMedia(std::string(id), std::string(path));
        LOGD("nativeRegisterClip: %s -> %s", id, path);
    }
    if (id)   env->ReleaseStringUTFChars(clipId,     id);
    if (path) env->ReleaseStringUTFChars(sourcePath, path);
}

/**
 * Frame-accurate seek: enables exact-seek mode on the DecoderPool, renders the frame
 * at [positionNs], then restores normal mode.
 * Must be on the GL thread.
 */
JNIEXPORT void JNICALL
Java_com_sdk_video_timeline_TimelinePreviewBridge_nativeSeekTo(
    JNIEnv* /*env*/, jobject /*thiz*/, jlong handle, jlong positionNs)
{
    auto* ctx = reinterpret_cast<PreviewContext*>(handle);
    if (!ctx || !ctx->compositor || !ctx->decoderPool || !ctx->defaultFb) return;

    // Enable exact-seek: DecoderPool will set requiresExactSeek=true on next getFrame() calls
    ctx->decoderPool->setExactSeekMode(true);

    glViewport(0, 0, ctx->surfaceWidth, ctx->surfaceHeight);
    auto res = ctx->compositor->renderFrameAtTime(
        static_cast<int64_t>(positionNs), ctx->defaultFb);
    if (!res.isOk()) {
        clearBlack(ctx->surfaceWidth, ctx->surfaceHeight);
    }

    // Single-shot — restore normal playback mode
    ctx->decoderPool->setExactSeekMode(false);
    LOGD("nativeSeekTo: pos=%.3fs exact=done", positionNs / 1e9);
}

/**
 * Fast scrub: disables exact-seek (nearest I-frame, minimal latency).
 * Suitable for continuous drag input; call seekTo() on drag-end for frame accuracy.
 * Must be on the GL thread.
 */
JNIEXPORT void JNICALL
Java_com_sdk_video_timeline_TimelinePreviewBridge_nativeScrubTo(
    JNIEnv* /*env*/, jobject /*thiz*/, jlong handle, jlong positionNs)
{
    auto* ctx = reinterpret_cast<PreviewContext*>(handle);
    if (!ctx || !ctx->compositor || !ctx->defaultFb) return;

    // Leave exact-seek mode OFF — use nearest buffered / I-frame decode
    glViewport(0, 0, ctx->surfaceWidth, ctx->surfaceHeight);
    auto res = ctx->compositor->renderFrameAtTime(
        static_cast<int64_t>(positionNs), ctx->defaultFb);
    if (!res.isOk()) {
        clearBlack(ctx->surfaceWidth, ctx->surfaceHeight);
    }
}

JNIEXPORT void JNICALL
Java_com_sdk_video_timeline_TimelinePreviewBridge_nativeRelease(
    JNIEnv* /*env*/, jobject /*thiz*/, jlong handle)
{
    auto* ctx = reinterpret_cast<PreviewContext*>(handle);
    if (!ctx) return;

    ctx->defaultFb.reset();
    if (ctx->filterEngine) {
        ctx->filterEngine->release();
    }
    ctx->compositor.reset();
    ctx->filterEngine.reset();
    ctx->timeline.reset();
    delete ctx;
    LOGD("nativeRelease: preview context destroyed");
}

} // extern "C"
