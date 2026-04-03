#include <jni.h>
#include <string>
#include <memory>

#include "../../../../core/include/timeline/Timeline.h"
#include "../../../../core/include/timeline/Track.h"
#include "../../../../core/include/timeline/Clip.h"

using namespace sdk::video::timeline;

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
    return -1; // Null pointer error
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
    if (!timelinePtr || !(*timelinePtr)) return -1; // Null pointer error

    TrackPtr track = (*timelinePtr)->getTrack(zIndex);
    if (track) {
        const char *cId = env->GetStringUTFChars(clipId, nullptr);
        const char *cPath = env->GetStringUTFChars(sourcePath, nullptr);

        auto clip = std::make_shared<Clip>(cId, cPath, static_cast<Clip::MediaType>(mediaType));
        clip->setTrimIn(trimInUs);
        clip->setTrimOut(trimOutUs);
        clip->setTimelineIn(timelineInUs);

        track->addClip(clip);

        env->ReleaseStringUTFChars(clipId, cId);
        env->ReleaseStringUTFChars(sourcePath, cPath);
        return 0; // Success
    }
    return -2; // Track not found error
}

} // extern "C"
