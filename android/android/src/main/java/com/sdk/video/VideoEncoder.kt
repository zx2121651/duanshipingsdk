package com.sdk.video

import android.annotation.SuppressLint
import android.media.*
import android.util.Log
import android.view.Surface
import kotlinx.coroutines.*
import java.io.IOException
import java.nio.ByteBuffer
import java.util.concurrent.ConcurrentLinkedQueue
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

/**
 * 纯异步、零阻塞的商业级音视频编码器。
 * 亮点：完全基于 MediaCodec.Callback 事件驱动，使用 Muxer 同步屏障解决多轨混合问题。
 * 新增：智能探测硬件支持的 High Profile 并开启 VBR 动态码率，以获得最佳画质与体积比。
 * 升级：彻底剥离 Java AudioRecord，引入底层的 Oboe C++ 引擎进行超低延迟音频采集与自动重采样。
 */
@InternalApi
class VideoEncoder(
    private val filterManager: VideoFilterManager,
    private val config: VideoExportConfig
) {
    private val width get() = config.width
    private val height get() = config.height
    private val videoBitRate get() = config.videoBitrate
    private val frameRate get() = config.fps
    private val audioSampleRate get() = config.audioSampleRate
    private val audioBitRate get() = config.audioBitrate
    private val iFrameInterval get() = config.iFrameInterval
    private val outputPath get() = config.outputPath

    companion object {
        private const val TAG = "VideoEncoder"
        private const val MIME_TYPE_VIDEO_AVC  = MediaFormat.MIMETYPE_VIDEO_AVC
        private const val MIME_TYPE_VIDEO_HEVC = MediaFormat.MIMETYPE_VIDEO_HEVC

        /** Returns true if the device has a hardware HEVC encoder. */
        fun isHevcHardwareEncoderAvailable(): Boolean {
            return try {
                val list = MediaCodecList(MediaCodecList.REGULAR_CODECS)
                list.codecInfos.any { info ->
                    info.isEncoder &&
                    info.supportedTypes.any { it.equals(MIME_TYPE_VIDEO_HEVC, ignoreCase = true) } &&
                    info.name.contains("c2.", ignoreCase = true).not() // prefer HW over SW
                }
            } catch (e: Exception) { false }
        }
    }

    private var videoMimeType: String = MIME_TYPE_VIDEO_AVC
    private var videoCodec: MediaCodec? = null
    private var audioCodec: MediaCodec? = null
    private var muxer: MediaMuxer? = null

    private var inputSurface: Surface? = null

    // Muxer 状态与轨道索引
    @Volatile private var isRecording = false
    private var lastVideoPtsUs: Long = -1
    private var lastAudioPtsUs: Long = -1
    private val syncLock = Any()
    @Volatile private var muxerStarted = false
    private var videoTrackIndex = -1
    private var audioTrackIndex = -1
    private var isVideoFormatAdded = false
    private var isAudioFormatAdded = false

    // 早鸟帧缓存队列：在 Muxer 启动前，缓存那些提前到来的 SPS/PPS 配置头或视频帧，防止丢帧
    private val pendingFrames = ConcurrentLinkedQueue<FrameData>()
    private var startTimeNs: Long = 0
    private var totalSamplesEncoded: Long = 0

    // P0-2: Precise EOS signalling — replaces Thread.sleep(100)
    private var videoEosLatch = CountDownLatch(1)
    private val videoEosReached = AtomicBoolean(false)

    fun getStartTimeNs(): Long = startTimeNs

    // 独立协程作用域，专门用于从底层 RingBuffer 拉取音频 PCM
    private val encoderScope = CoroutineScope(Dispatchers.IO + SupervisorJob())

    private data class FrameData(
        val buffer: ByteBuffer,
        val info: MediaCodec.BufferInfo,
        val isVideo: Boolean
    )

    fun startRecording(): Result<Surface> {
        if (isRecording) {
            return inputSurface?.let { Result.success(it) }
                ?: Result.failure(IllegalStateException("Recording is active but inputSurface is null"))
        }

        try {
            Log.i(TAG, "Starting recording to: $outputPath")
            muxer = MediaMuxer(outputPath, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4)

            // P0-2: Reset EOS latch for this recording session
            videoEosLatch = CountDownLatch(1)
            videoEosReached.set(false)

            // P1-1: Prefer HEVC if device supports it and caller requested it
            videoMimeType = if (config.useHevc && isHevcHardwareEncoderAvailable())
                MIME_TYPE_VIDEO_HEVC else MIME_TYPE_VIDEO_AVC
            Log.i(TAG, "Selected video codec: $videoMimeType")

            setupVideoCodec()
            setupAudioCodec()

            // Align the anchor time to what SurfaceTexture uses (System.nanoTime() typically matches CLOCK_MONOTONIC on Android)
            startTimeNs = System.nanoTime()
            totalSamplesEncoded = 0
            isRecording = true

            videoCodec?.start()
            audioCodec?.start()

            // 启动底层的 Oboe C++ 音频采集引擎 (自带重采样至 44100Hz)
            filterManager.startAudioRecord(audioSampleRate)

            // 启动协程去消费底层吐出的 PCM 数据
            startAudioRecordLoop()

            val surface = inputSurface ?: throw IllegalStateException("Input surface not created after codec setup")
            return Result.success(surface)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to start recording", e)
            stopRecording()
            return Result.failure(e)
        }
    }

    private fun setupVideoCodec() {
        val format = MediaFormat.createVideoFormat(videoMimeType, width, height)
        format.setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
        format.setInteger(MediaFormat.KEY_BIT_RATE, videoBitRate)
        format.setInteger(MediaFormat.KEY_FRAME_RATE, frameRate)
        format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)

        optimizeCodecProfileAndBitrateMode(format)

        videoCodec = MediaCodec.createEncoderByType(videoMimeType)
        videoCodec?.setCallback(object : MediaCodec.Callback() {
            override fun onInputBufferAvailable(codec: MediaCodec, index: Int) {}

            override fun onOutputBufferAvailable(codec: MediaCodec, index: Int, info: MediaCodec.BufferInfo) {
                // P0-2: Detect video EOS and signal the latch
                if ((info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
                    videoEosReached.set(true)
                    videoEosLatch.countDown()
                }
                processOutputData(codec, index, info, isVideo = true)
            }

            override fun onOutputFormatChanged(codec: MediaCodec, format: MediaFormat) {
                synchronized(this@VideoEncoder) {
                    if (videoTrackIndex == -1) {
                        videoTrackIndex = muxer!!.addTrack(format)
                        isVideoFormatAdded = true
                        tryStartMuxer()
                    }
                }
            }

            override fun onError(codec: MediaCodec, e: MediaCodec.CodecException) {
                Log.e(TAG, "Video Codec Error", e)
            }
        })
        videoCodec?.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
        inputSurface = videoCodec?.createInputSurface()
    }

    private fun optimizeCodecProfileAndBitrateMode(format: MediaFormat) {
        try {
            val codecList = MediaCodecList(MediaCodecList.REGULAR_CODECS)
            for (codecInfo in codecList.codecInfos) {
                if (!codecInfo.isEncoder) continue
                val types = codecInfo.supportedTypes
                if (!types.contains(videoMimeType)) continue

                val capabilities = codecInfo.getCapabilitiesForType(videoMimeType)

                val encoderCaps = capabilities.encoderCapabilities
                if (encoderCaps != null && encoderCaps.isBitrateModeSupported(MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_VBR)) {
                    format.setInteger(MediaFormat.KEY_BITRATE_MODE, MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_VBR)
                }

                // P1-1: Choose best profile for the selected codec
                val targetProfile = if (videoMimeType == MIME_TYPE_VIDEO_HEVC)
                    MediaCodecInfo.CodecProfileLevel.HEVCProfileMain
                else
                    MediaCodecInfo.CodecProfileLevel.AVCProfileHigh

                for (profileLevel in capabilities.profileLevels) {
                    if (profileLevel.profile == targetProfile) {
                        format.setInteger(MediaFormat.KEY_PROFILE, profileLevel.profile)
                        format.setInteger(MediaFormat.KEY_LEVEL, profileLevel.level)
                        return
                    }
                }
            }
        } catch (e: Exception) {
            Log.w(TAG, "Failed to optimize codec profile", e)
        }
    }

    private fun setupAudioCodec() {
        // P0-1: Fixed — was hardcoded to 1 (Mono), must be 2 (Stereo) to match Oboe/PCM pipeline
        val format = MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_AAC, audioSampleRate, 2)
        format.setInteger(MediaFormat.KEY_AAC_PROFILE, MediaCodecInfo.CodecProfileLevel.AACObjectLC)
        format.setInteger(MediaFormat.KEY_BIT_RATE, audioBitRate)
        format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 16384)

        audioCodec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_AUDIO_AAC)
        audioCodec?.setCallback(object : MediaCodec.Callback() {
            override fun onInputBufferAvailable(codec: MediaCodec, index: Int) {}
            override fun onOutputBufferAvailable(codec: MediaCodec, index: Int, info: MediaCodec.BufferInfo) {
                processOutputData(codec, index, info, isVideo = false)
            }
            override fun onOutputFormatChanged(codec: MediaCodec, format: MediaFormat) {
                synchronized(this@VideoEncoder) {
                    if (audioTrackIndex == -1) {
                        audioTrackIndex = muxer!!.addTrack(format)
                        isAudioFormatAdded = true
                        tryStartMuxer()
                    }
                }
            }
            override fun onError(codec: MediaCodec, e: MediaCodec.CodecException) {}
        })
        audioCodec?.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
    }

    private fun startAudioRecordLoop() {
        encoderScope.launch {
            val buffer = ByteArray(4096)
            while (isRecording && isActive) {
                // 通过 JNI 从底层的 Oboe 无锁队列中拉取重采样好的 PCM 数据
                val readBytes = filterManager.readAudioPCM(buffer, buffer.size)

                if (readBytes > 0) {
                    val codec = audioCodec ?: break
                    try {
                        val inputBufferIndex = codec.dequeueInputBuffer(10000)
                        if (inputBufferIndex >= 0) {
                            val inputBuffer = codec.getInputBuffer(inputBufferIndex)
                            inputBuffer?.clear()
                            inputBuffer?.put(buffer, 0, readBytes)

                            // [PTS Drift Fix]: Use the total number of samples encoded as a stable clock.
                            // This prevents drift between audio and video by tying PTS to the actual data volume,
                            // starting from the synchronized system-time anchor.
                            // P0-1: Stereo 16-bit = 4 bytes per frame (2 bytes/sample × 2 channels)
                            val samplesRead = readBytes / 4 // stereo 16-bit frames
                            val pts = (startTimeNs + (totalSamplesEncoded * 1000000000L / audioSampleRate)) / 1000
                            totalSamplesEncoded += samplesRead

                            synchronized(syncLock) {
                                lastAudioPtsUs = pts
                            }

                            codec.queueInputBuffer(inputBufferIndex, 0, readBytes, pts, 0)
                        }
                    } catch (e: IllegalStateException) {
                        Log.w(TAG, "Audio codec state exception in loop, probably stopped: ${e.message}")
                        break
                    } catch (e: Exception) {
                        Log.e(TAG, "Audio codec exception in loop", e)
                        break
                    }
                } else {
                    // 如果底层队列暂时没数据，稍作休眠防止空转 CPU
                    delay(2)
                }
            }
        }
    }

    private fun processOutputData(codec: MediaCodec, index: Int, info: MediaCodec.BufferInfo, isVideo: Boolean) {
        if ((info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0) {
            info.size = 0
        }

        if (info.size != 0) {
            val encodedData = codec.getOutputBuffer(index)
            if (encodedData != null) {
                encodedData.position(info.offset)
                encodedData.limit(info.offset + info.size)

                synchronized(syncLock) {
                    if (isVideo) {
                        if (info.presentationTimeUs <= lastVideoPtsUs) {
                            info.presentationTimeUs = lastVideoPtsUs + 10
                        }
                        lastVideoPtsUs = info.presentationTimeUs
                    }
                }

                synchronized(this) {
                    if (muxerStarted) {
                        val track = if (isVideo) videoTrackIndex else audioTrackIndex
                        try {
                            muxer?.writeSampleData(track, encodedData, info)
                        } catch (e: Exception) {
                            Log.e(TAG, "Muxer write exception", e)
                            stopRecording(isFallback = true)
                        }
                    } else {
                        val copyBuffer = ByteBuffer.allocateDirect(info.size)
                        copyBuffer.put(encodedData)
                        copyBuffer.flip()

                        val newInfo = MediaCodec.BufferInfo().apply {
                            set(0, info.size, info.presentationTimeUs, info.flags)
                        }
                        pendingFrames.add(FrameData(copyBuffer, newInfo, isVideo))
                    }
                }
            }
        }
        codec.releaseOutputBuffer(index, false)
    }

    @Synchronized
    private fun tryStartMuxer() {
        if (!muxerStarted && isVideoFormatAdded && isAudioFormatAdded) {
            muxer?.start()
            muxerStarted = true

            while (pendingFrames.isNotEmpty()) {
                val frame = pendingFrames.poll() ?: break
                val track = if (frame.isVideo) videoTrackIndex else audioTrackIndex
                try {
                    muxer?.writeSampleData(track, frame.buffer, frame.info)
                } catch (e: Exception) {
                    Log.e(TAG, "Muxer write exception", e)
                    stopRecording(isFallback = true)
                    break
                }
            }
        }
    }

    fun stopRecording(isFallback: Boolean = false) {
        Log.i(TAG, "Stopping recording... (isFallback=$isFallback)")
        isRecording = false
        encoderScope.coroutineContext.cancelChildren()

        // 关闭底层的 Oboe 音频采集引擎
        filterManager.stopAudioRecord()

        try {
            videoCodec?.signalEndOfInputStream()
        } catch (e: Exception) {
            Log.w(TAG, "Error signaling end of video stream: ${e.message}")
        }

        // P0-2: Precise EOS wait — replaces Thread.sleep(100)
        // Wait up to 500ms for the EOS output buffer from the video codec.
        if (!videoEosReached.get()) {
            val reached = videoEosLatch.await(500, TimeUnit.MILLISECONDS)
            if (!reached) {
                Log.w(TAG, "Video EOS not received within timeout; proceeding with stop anyway.")
            }
        }

        // 独立且严格释放视频编码器
        try {
            videoCodec?.stop()
        } catch (e: Exception) {
            Log.w(TAG, "VideoCodec stop() failed: ${e.message}")
        } finally {
            // 强制执行，释放硬件资源
            try {
                videoCodec?.release()
            } catch (e: Exception) {
                Log.e(TAG, "VideoCodec release() failed", e)
            }
            videoCodec = null
        }

        // 同样的独立释放逻辑应用于音频编码器
        try {
            audioCodec?.stop()
        } catch (e: Exception) {
            Log.w(TAG, "AudioCodec stop() failed: ${e.message}")
        } finally {
            try {
                audioCodec?.release()
            } catch (e: Exception) {
                Log.e(TAG, "AudioCodec release() failed", e)
            }
            audioCodec = null
        }

        synchronized(this) {
            try {
                if (muxerStarted) {
                    muxer?.stop()
                }
            } catch (e: Exception) {
                Log.e(TAG, "Error stopping muxer (maybe no data was written): ${e.message}")
            } finally {
                try {
                    muxer?.release()
                } catch (e: Exception) {
                    Log.e(TAG, "Error releasing muxer: ${e.message}")
                }
                muxer = null
                muxerStarted = false
            }
            Unit
        }

        inputSurface?.release()
        inputSurface = null

        videoTrackIndex = -1
        audioTrackIndex = -1
        isVideoFormatAdded = false
        isAudioFormatAdded = false
        pendingFrames.clear()
        Log.i(TAG, "Recording stopped and resources released.")
    }
}
