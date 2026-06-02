#include "GLTypes.h"
#include <jni.h>
#include <string>
#include <memory>

#include "timeline/Timeline.h"
#include "timeline/Track.h"
#include "timeline/Clip.h"
#include "timeline/TimelineDraft.h"
#include "timeline/AudioEffects.h"
#include "timeline/DecoderPool.h"
#include "ai/TfliteInferenceEngine.h"
#include "ai/BeautyFilter.h"
#include "ai/SegmentationFilter.h"

using namespace sdk::video::timeline;

// 全局单例推理引擎（由 nativeLoadAIModel 初始化）
static std::shared_ptr<sdk::video::ai::TfliteInferenceEngine> g_inferenceEngine;

extern "C" {

// ------------------------------------------------------------------------
// [JNI] Timeline NLE Engine (时间线核心编辑引擎暴露给上层 UI)
// 这里的 `jlong handle` 传的是一个 `std::shared_ptr<Timeline>*` 指针的地址
// ------------------------------------------------------------------------

JNIEXPORT jlong JNICALL
Java_com_sdk_video_timeline_TimelineManager_nativeCreateTimeline(JNIEnv *env, jobject thiz, jint width, jint height, jint fps) {
    auto timelinePtr = new std::shared_ptr<Timeline>(std::make_shared<Timeline>(width, height, fps));
    return reinterpret_cast<jlong>(timelinePtr);
}

JNIEXPORT void JNICALL
Java_com_sdk_video_timeline_TimelineManager_nativeReleaseTimeline(JNIEnv *env, jobject thiz, jlong handle) {
    auto timelinePtr = reinterpret_cast<std::shared_ptr<Timeline>*>(handle);
    if (timelinePtr) {
        timelinePtr->reset();
        delete timelinePtr;
    }
}

JNIEXPORT jint JNICALL
Java_com_sdk_video_timeline_TimelineManager_nativeAddTrack(JNIEnv *env, jobject thiz, jlong handle, jint zIndex, jint trackType) {
    auto timelinePtr = reinterpret_cast<std::shared_ptr<Timeline>*>(handle);
    if (timelinePtr && *timelinePtr) {
        (*timelinePtr)->addTrack(zIndex, static_cast<Track::TrackType>(trackType));
        return 0; // Success
    }
    return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_NULL);
}

// ========================================================================
// 剪辑片段操作 (Clip Operations) - 暴露给安卓上层的拖拽条 UI 模块使用
// 例如：用户插入一个视频，或者缩短/拉长了一个视频的时长。
// ========================================================================

JNIEXPORT jint JNICALL
Java_com_sdk_video_timeline_TimelineManager_nativeAddClip(
    JNIEnv *env, jobject thiz, jlong handle, jint zIndex,
    jstring clipId, jstring sourcePath, jint mediaType,
    jlong trimInUs, jlong trimOutUs, jlong timelineInUs)
{
    auto timelinePtr = reinterpret_cast<std::shared_ptr<Timeline>*>(handle);
    if (!timelinePtr || !(*timelinePtr)) return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_NULL);

    TrackPtr track = (*timelinePtr)->getTrack(zIndex);
    if (track) {
        const char *cId = env->GetStringUTFChars(clipId, nullptr);
        const char *cPath = env->GetStringUTFChars(sourcePath, nullptr);

        auto clip = std::make_shared<Clip>(cId, cPath, static_cast<Clip::MediaType>(mediaType));
        clip->setTrimIn(trimInUs * 1000);
        clip->setTrimOut(trimOutUs * 1000);
        clip->setTimelineIn(timelineInUs * 1000);

        track->addClip(clip);

        env->ReleaseStringUTFChars(clipId, cId);
        env->ReleaseStringUTFChars(sourcePath, cPath);
        return 0; // Success
    }
    return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_TRACK_NOT_FOUND);
}


JNIEXPORT jint JNICALL
Java_com_sdk_video_timeline_TimelineManager_nativeSetClipSpeed(JNIEnv *env, jobject thiz, jlong handle, jint zIndex, jstring clipId, jfloat speed) {
    auto timelinePtr = reinterpret_cast<std::shared_ptr<Timeline>*>(handle);
    if (!timelinePtr || !(*timelinePtr)) return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_NULL);

    auto track = (*timelinePtr)->getTrack(zIndex);
    if (!track) return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_TRACK_NOT_FOUND);

    const char* idStr = env->GetStringUTFChars(clipId, nullptr);
    auto clip = track->getClip(std::string(idStr));
    env->ReleaseStringUTFChars(clipId, idStr);

    if (!clip) return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_CLIP_NOT_FOUND);

    clip->setSpeed(speed);
    return 0; // SUCCESS
}

JNIEXPORT jint JNICALL
Java_com_sdk_video_timeline_TimelineManager_nativeSetClipTransition(JNIEnv *env, jobject thiz, jlong handle, jint zIndex, jstring clipId, jint type, jlong durationUs) {
    auto timelinePtr = reinterpret_cast<std::shared_ptr<Timeline>*>(handle);
    if (!timelinePtr || !(*timelinePtr)) return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_NULL);

    auto track = (*timelinePtr)->getTrack(zIndex);
    if (!track) return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_TRACK_NOT_FOUND);

    const char* idStr = env->GetStringUTFChars(clipId, nullptr);
    auto clip = track->getClip(std::string(idStr));
    env->ReleaseStringUTFChars(clipId, idStr);

    if (!clip) return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_CLIP_NOT_FOUND);

    clip->setInTransition(static_cast<TransitionType>(type), durationUs * 1000);
    return 0;
}

JNIEXPORT jint JNICALL
Java_com_sdk_video_timeline_TimelineManager_nativeAddClipKeyframe(JNIEnv *env, jobject thiz, jlong handle, jint zIndex, jstring clipId, jstring property, jlong relativeTimeUs, jfloat value) {
    auto timelinePtr = reinterpret_cast<std::shared_ptr<Timeline>*>(handle);
    if (!timelinePtr || !(*timelinePtr)) return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_NULL);

    auto track = (*timelinePtr)->getTrack(zIndex);
    if (!track) return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_TRACK_NOT_FOUND);

    const char* idStr = env->GetStringUTFChars(clipId, nullptr);
    auto clip = track->getClip(std::string(idStr));
    env->ReleaseStringUTFChars(clipId, idStr);

    if (!clip) return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_CLIP_NOT_FOUND);

    const char* propStr = env->GetStringUTFChars(property, nullptr);
    clip->addKeyframe(std::string(propStr), relativeTimeUs * 1000, value);
    env->ReleaseStringUTFChars(property, propStr);

    return 0;
}

JNIEXPORT jint JNICALL
Java_com_sdk_video_timeline_TimelineManager_nativeRemoveClip(JNIEnv *env, jobject thiz, jlong handle, jint zIndex, jstring clipId) {
    auto timelinePtr = reinterpret_cast<std::shared_ptr<Timeline>*>(handle);
    if (!timelinePtr || !(*timelinePtr)) return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_NULL);

    auto track = (*timelinePtr)->getTrack(zIndex);
    if (!track) return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_TRACK_NOT_FOUND);

    const char* idStr = env->GetStringUTFChars(clipId, nullptr);
    track->removeClip(std::string(idStr));
    env->ReleaseStringUTFChars(clipId, idStr);

    return 0;
}

// P1-4: Reverse playback
JNIEXPORT jint JNICALL
Java_com_sdk_video_timeline_TimelineManager_nativeSetClipReversed(JNIEnv *env, jobject thiz, jlong handle, jint zIndex, jstring clipId, jboolean reversed) {
    auto timelinePtr = reinterpret_cast<std::shared_ptr<Timeline>*>(handle);
    if (!timelinePtr || !(*timelinePtr)) return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_NULL);

    auto track = (*timelinePtr)->getTrack(zIndex);
    if (!track) return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_TRACK_NOT_FOUND);

    const char* idStr = env->GetStringUTFChars(clipId, nullptr);
    auto clip = track->getClip(std::string(idStr));
    env->ReleaseStringUTFChars(clipId, idStr);

    if (!clip) return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_CLIP_NOT_FOUND);

    clip->setReversed(reversed == JNI_TRUE);
    return 0;
}

// P2-音频变声: Set pitch shift (semitones) for a clip
JNIEXPORT jint JNICALL
Java_com_sdk_video_timeline_TimelineManager_nativeSetClipPitchShift(
    JNIEnv *env, jobject thiz, jlong handle, jint zIndex, jstring clipId, jfloat semitones)
{
    auto timelinePtr = reinterpret_cast<std::shared_ptr<Timeline>*>(handle);
    if (!timelinePtr || !(*timelinePtr))
        return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_NULL);

    auto track = (*timelinePtr)->getTrack(zIndex);
    if (!track) return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_TRACK_NOT_FOUND);

    const char* idStr = env->GetStringUTFChars(clipId, nullptr);
    auto clip = track->getClip(std::string(idStr));
    env->ReleaseStringUTFChars(clipId, idStr);
    if (!clip) return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_CLIP_NOT_FOUND);

    clip->setPitchShift(static_cast<float>(semitones));
    return 0;
}

// 降噪强度 [0,1]
JNIEXPORT jint JNICALL
Java_com_sdk_video_timeline_TimelineManager_nativeSetClipNoiseReduction(
    JNIEnv *env, jobject thiz, jlong handle, jint zIndex, jstring clipId, jfloat strength)
{
    auto timelinePtr = reinterpret_cast<std::shared_ptr<Timeline>*>(handle);
    if (!timelinePtr || !(*timelinePtr))
        return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_NULL);
    auto track = (*timelinePtr)->getTrack(zIndex);
    if (!track) return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_TRACK_NOT_FOUND);
    const char* idStr = env->GetStringUTFChars(clipId, nullptr);
    auto clip = track->getClip(std::string(idStr));
    env->ReleaseStringUTFChars(clipId, idStr);
    if (!clip) return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_CLIP_NOT_FOUND);
    clip->setNoiseReduction(static_cast<float>(strength));
    return 0;
}

// 淡入
JNIEXPORT jint JNICALL
Java_com_sdk_video_timeline_TimelineManager_nativeSetClipFadeIn(
    JNIEnv *env, jobject thiz, jlong handle, jint zIndex, jstring clipId, jlong durationUs)
{
    auto timelinePtr = reinterpret_cast<std::shared_ptr<Timeline>*>(handle);
    if (!timelinePtr || !(*timelinePtr))
        return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_NULL);
    auto track = (*timelinePtr)->getTrack(zIndex);
    if (!track) return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_TRACK_NOT_FOUND);
    const char* idStr = env->GetStringUTFChars(clipId, nullptr);
    auto clip = track->getClip(std::string(idStr));
    env->ReleaseStringUTFChars(clipId, idStr);
    if (!clip) return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_CLIP_NOT_FOUND);
    clip->setFadeIn(static_cast<int64_t>(durationUs) * 1000LL);
    return 0;
}

// 淡出（startRelUs = clip 内相对起点偏移，单位微秒）
JNIEXPORT jint JNICALL
Java_com_sdk_video_timeline_TimelineManager_nativeSetClipFadeOut(
    JNIEnv *env, jobject thiz, jlong handle, jint zIndex, jstring clipId,
    jlong startRelUs, jlong durationUs)
{
    auto timelinePtr = reinterpret_cast<std::shared_ptr<Timeline>*>(handle);
    if (!timelinePtr || !(*timelinePtr))
        return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_NULL);
    auto track = (*timelinePtr)->getTrack(zIndex);
    if (!track) return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_TRACK_NOT_FOUND);
    const char* idStr = env->GetStringUTFChars(clipId, nullptr);
    auto clip = track->getClip(std::string(idStr));
    env->ReleaseStringUTFChars(clipId, idStr);
    if (!clip) return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_CLIP_NOT_FOUND);
    clip->setFadeOut(static_cast<int64_t>(startRelUs) * 1000LL,
                     static_cast<int64_t>(durationUs) * 1000LL);
    return 0;
}

// PCM 波形峰值提取（纯工具函数）
// pcmData: 16-bit 小端 PCM 字节数组（Kotlin 侧通过 MediaExtractor 解码后传入）
// 返回 numBars 个归一化峰值 float[]，值域 [0, 1]
JNIEXPORT jfloatArray JNICALL
Java_com_sdk_video_timeline_TimelineManager_nativeExtractWaveformPeaks(
    JNIEnv *env, jobject thiz, jbyteArray pcmData, jint numBars, jint channels)
{
    if (!pcmData || numBars <= 0 || channels <= 0) return nullptr;

    jsize byteLen = env->GetArrayLength(pcmData);
    if (byteLen < 2) return nullptr;

    jbyte* bytes = env->GetByteArrayElements(pcmData, nullptr);
    const int16_t* samples = reinterpret_cast<const int16_t*>(bytes);
    const int sampleCount = static_cast<int>(byteLen / sizeof(int16_t));

    std::vector<int16_t> pcm(samples, samples + sampleCount);
    env->ReleaseByteArrayElements(pcmData, bytes, JNI_ABORT);

    auto peaks = sdk::video::timeline::AudioEffects::extractWaveformPeaks(pcm, numBars, channels);

    jfloatArray result = env->NewFloatArray(static_cast<jsize>(peaks.size()));
    if (result) env->SetFloatArrayRegion(result, 0, static_cast<jsize>(peaks.size()), peaks.data());
    return result;
}

// P2-5: Draft serialization
JNIEXPORT jint JNICALL
Java_com_sdk_video_timeline_TimelineManager_nativeSaveDraft(
    JNIEnv *env, jobject thiz, jlong handle, jstring filePath)
{
    auto timelinePtr = reinterpret_cast<std::shared_ptr<Timeline>*>(handle);
    if (!timelinePtr || !(*timelinePtr))
        return static_cast<int>(sdk::video::ErrorCode::ERR_TIMELINE_NULL);

    const char* path = env->GetStringUTFChars(filePath, nullptr);
    bool ok = sdk::video::timeline::saveDraft(**timelinePtr, std::string(path));
    env->ReleaseStringUTFChars(filePath, path);
    return ok ? 0 : static_cast<int>(sdk::video::ErrorCode::ERR_EXPORTER_IO_ERROR);
}

JNIEXPORT jlong JNICALL
Java_com_sdk_video_timeline_TimelineManager_nativeLoadDraft(
    JNIEnv *env, jobject thiz, jstring filePath)
{
    const char* path = env->GetStringUTFChars(filePath, nullptr);
    auto tl = sdk::video::timeline::loadDraft(std::string(path));
    env->ReleaseStringUTFChars(filePath, path);
    if (!tl) return 0L;
    auto* ptr = new std::shared_ptr<Timeline>(tl);
    return reinterpret_cast<jlong>(ptr);
}

// ---------------------------------------------------------------------------
// Decoder Fallback Strategy — FFmpeg 软解策略 API
// ---------------------------------------------------------------------------

// NativeBridge 中 EngineWrapper 持有的 DecoderPool 指针通过 Timeline 获取
// 此处：jlong decoderPoolHandle 直接由 TimelineManager 管理的 DecoderPool* 传入
JNIEXPORT void JNICALL
Java_com_sdk_video_timeline_TimelineManager_nativeSetDecoderFallbackStrategy(
    JNIEnv *env, jobject thiz, jlong decoderPoolHandle, jint strategy)
{
    auto* pool = reinterpret_cast<sdk::video::timeline::DecoderPool*>(decoderPoolHandle);
    if (!pool) return;
    pool->setFallbackStrategy(static_cast<sdk::video::timeline::DecoderFallbackStrategy>(strategy));
}

// 运行时查询 FFmpeg 软解是否可用（编译期宏决定）
JNIEXPORT jboolean JNICALL
Java_com_sdk_video_timeline_TimelineManager_nativeIsSoftwareDecoderAvailable(
    JNIEnv *env, jobject thiz)
{
#ifdef HAS_FFMPEG_DECODER
    return JNI_TRUE;
#else
    return JNI_FALSE;
#endif
}

// ---------------------------------------------------------------------------
// AI 美颜 / 人像分割 — JNI API
// ---------------------------------------------------------------------------

/**
 * 加载 TFLite 模型（.tflite 文件路径）。
 * 首次调用会创建全局推理引擎实例。
 * @return JNI_TRUE = 成功；JNI_FALSE = TFLite 未编译 or 模型文件不存在
 */
JNIEXPORT jboolean JNICALL
Java_com_sdk_video_timeline_TimelineManager_nativeLoadAIModel(
    JNIEnv* env, jobject thiz, jstring modelPath)
{
    const char* path = env->GetStringUTFChars(modelPath, nullptr);
    if (!g_inferenceEngine)
        g_inferenceEngine = std::make_shared<sdk::video::ai::TfliteInferenceEngine>();

    bool ok = g_inferenceEngine->loadModel(std::string(path));
    env->ReleaseStringUTFChars(modelPath, path);
    return ok ? JNI_TRUE : JNI_FALSE;
}

/**
 * 查询 AI 推理引擎是否可用（TFLite 编译期宏 + 模型已加载）。
 */
JNIEXPORT jboolean JNICALL
Java_com_sdk_video_timeline_TimelineManager_nativeIsAIAvailable(
    JNIEnv* env, jobject thiz)
{
#ifdef HAS_TFLITE
    return (g_inferenceEngine && g_inferenceEngine->isLoaded()) ? JNI_TRUE : JNI_FALSE;
#else
    return JNI_FALSE;
#endif
}

/**
 * 设置美颜参数（smoothStrength / whitenStrength）。
 * 下次 processFrame() 时生效。
 * @param filterHandle  BeautyFilter* 指针（jlong 形式）
 */
JNIEXPORT void JNICALL
Java_com_sdk_video_timeline_TimelineManager_nativeSetBeautyParams(
    JNIEnv* env, jobject thiz, jlong filterHandle, jfloat smooth, jfloat whiten)
{
    auto* f = reinterpret_cast<sdk::video::BeautyFilter*>(filterHandle);
    if (!f) return;
    f->setParameter("smoothStrength", static_cast<float>(smooth));
    f->setParameter("whitenStrength", static_cast<float>(whiten));
}

/**
 * 设置人像分割模式与参数。
 * @param filterHandle  SegmentationFilter* 指针（jlong 形式）
 * @param mode          0=BLUR_BG, 1=REPLACE_BG
 * @param bgColorArgb   REPLACE_BG 背景色（ARGB 格式）
 * @param edgeSoften    边缘软化强度 [0.0, 1.0]
 */
JNIEXPORT void JNICALL
Java_com_sdk_video_timeline_TimelineManager_nativeSetSegmentationParams(
    JNIEnv* env, jobject thiz, jlong filterHandle,
    jint mode, jint bgColorArgb, jfloat edgeSoften)
{
    auto* f = reinterpret_cast<sdk::video::SegmentationFilter*>(filterHandle);
    if (!f) return;
    f->setParameter("mode",       static_cast<int>(mode));
    f->setParameter("bgColor",    static_cast<uint32_t>(bgColorArgb));
    f->setParameter("edgeSoften", static_cast<float>(edgeSoften));
}

} // extern "C"
