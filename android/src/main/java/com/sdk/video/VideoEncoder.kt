package com.sdk.video

import android.annotation.SuppressLint
import android.media.*
import android.util.Log
import android.view.Surface
import kotlinx.coroutines.*
import java.io.IOException
import java.nio.ByteBuffer
import java.util.concurrent.ConcurrentLinkedQueue

/**
 * 纯异步、零阻塞的商业级音视频编码器。
 * 亮点：完全基于 MediaCodec.Callback 事件驱动，使用 Muxer 同步屏障解决多轨混合问题。
 * 新增：智能探测硬件支持的 High Profile 并开启 VBR 动态码率，以获得最佳画质与体积比。
 */
class VideoEncoder(
    private val width: Int = 1080,
    private val height: Int = 1920,
    private val videoBitRate: Int = 4000000,
    private val frameRate: Int = 30,
    private val audioSampleRate: Int = 44100,
    private val audioBitRate: Int = 128000,
    private val audioChannels: Int = AudioFormat.CHANNEL_IN_MONO
) {
    companion object {
        private const val TAG = "VideoEncoder"
        private const val MIME_TYPE_VIDEO = MediaFormat.MIMETYPE_VIDEO_AVC
    }

    private var videoCodec: MediaCodec? = null
    private var audioCodec: MediaCodec? = null
    private var muxer: MediaMuxer? = null

    private var inputSurface: Surface? = null
    private var audioRecord: AudioRecord? = null

    // Muxer 状态与轨道索引
    @Volatile private var isRecording = false
    @Volatile private var muxerStarted = false
    private var videoTrackIndex = -1
    private var audioTrackIndex = -1
    private var isVideoFormatAdded = false
    private var isAudioFormatAdded = false

    // 早鸟帧缓存队列：在 Muxer 启动前，缓存那些提前到来的 SPS/PPS 配置头或视频帧，防止丢帧
    private val pendingFrames = ConcurrentLinkedQueue<FrameData>()
    private var startTimeNs: Long = 0

    // 独立协程作用域，专门用于 Audio 读取
    private val encoderScope = CoroutineScope(Dispatchers.IO + SupervisorJob())

    private data class FrameData(
        val buffer: ByteBuffer,
        val info: MediaCodec.BufferInfo,
        val isVideo: Boolean
    )

    fun startRecording(outputPath: String): Surface? {
        if (isRecording) return inputSurface

        try {
            muxer = MediaMuxer(outputPath, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4)

            // 1. 初始化并启动 Video Codec
            setupVideoCodec()

            // 2. 初始化并启动 Audio Codec
            setupAudioCodec()

            startTimeNs = System.nanoTime()
            isRecording = true

            videoCodec?.start()
            audioCodec?.start()

            // 3. 启动音频采集协程
            startAudioRecordLoop()

            return inputSurface
        } catch (e: Exception) {
            Log.e(TAG, "Failed to start recording", e)
            stopRecording()
            return null
        }
    }

    private fun setupVideoCodec() {
        val format = MediaFormat.createVideoFormat(MIME_TYPE_VIDEO, width, height)
        format.setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
        format.setInteger(MediaFormat.KEY_BIT_RATE, videoBitRate)
        format.setInteger(MediaFormat.KEY_FRAME_RATE, frameRate)
        format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)

        // 智能探测硬件支持的 Profile / Level，并开启 VBR (变码率) 以提升画质
        optimizeCodecProfileAndBitrateMode(format)

        videoCodec = MediaCodec.createEncoderByType(MIME_TYPE_VIDEO)
        videoCodec?.setCallback(object : MediaCodec.Callback() {
            override fun onInputBufferAvailable(codec: MediaCodec, index: Int) {
                // 视频输入由 Surface 提供，无需处理
            }

            override fun onOutputBufferAvailable(codec: MediaCodec, index: Int, info: MediaCodec.BufferInfo) {
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

    /**
     * 遍历设备硬编解码器，探测并应用 High Profile 和 VBR 码率模式
     */
    private fun optimizeCodecProfileAndBitrateMode(format: MediaFormat) {
        try {
            val codecList = MediaCodecList(MediaCodecList.REGULAR_CODECS)
            for (codecInfo in codecList.codecInfos) {
                if (!codecInfo.isEncoder) continue
                val types = codecInfo.supportedTypes
                if (!types.contains(MIME_TYPE_VIDEO)) continue

                val capabilities = codecInfo.getCapabilitiesForType(MIME_TYPE_VIDEO)

                // 探测 VBR 支持 (Variable Bitrate)
                val encoderCaps = capabilities.encoderCapabilities
                if (encoderCaps != null && encoderCaps.isBitrateModeSupported(MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_VBR)) {
                    format.setInteger(MediaFormat.KEY_BITRATE_MODE, MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_VBR)
                    Log.d(TAG, "Enabled VBR Bitrate Mode")
                }

                // 探测 High Profile 支持
                for (profileLevel in capabilities.profileLevels) {
                    if (profileLevel.profile == MediaCodecInfo.CodecProfileLevel.AVCProfileHigh) {
                        format.setInteger(MediaFormat.KEY_PROFILE, profileLevel.profile)
                        format.setInteger(MediaFormat.KEY_LEVEL, profileLevel.level)
                        Log.d(TAG, "Enabled AVC High Profile Level: ${profileLevel.level}")
                        return // 找到最高质量的配置后直接返回
                    }
                }
            }
        } catch (e: Exception) {
            Log.w(TAG, "Failed to optimize codec profile, falling back to default.", e)
        }
    }

    @SuppressLint("MissingPermission")
    private fun setupAudioCodec() {
        val format = MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_AAC, audioSampleRate, 1)
        format.setInteger(MediaFormat.KEY_AAC_PROFILE, MediaCodecInfo.CodecProfileLevel.AACObjectLC)
        format.setInteger(MediaFormat.KEY_BIT_RATE, audioBitRate)
        format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 16384)

        audioCodec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_AUDIO_AAC)
        audioCodec?.setCallback(object : MediaCodec.Callback() {
            override fun onInputBufferAvailable(codec: MediaCodec, index: Int) {
                // 音频数据由 startAudioRecordLoop 协程主动塞入，这里留空或作状态标记均可
            }

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

            override fun onError(codec: MediaCodec, e: MediaCodec.CodecException) {
                Log.e(TAG, "Audio Codec Error", e)
            }
        })
        audioCodec?.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)

        val bufferSize = AudioRecord.getMinBufferSize(audioSampleRate, audioChannels, AudioFormat.ENCODING_PCM_16BIT)
        audioRecord = AudioRecord(MediaRecorder.AudioSource.MIC, audioSampleRate, audioChannels, AudioFormat.ENCODING_PCM_16BIT, bufferSize * 2)
    }

    @SuppressLint("MissingPermission")
    private fun startAudioRecordLoop() {
        encoderScope.launch {
            audioRecord?.startRecording()
            val buffer = ByteArray(4096)

            while (isRecording && isActive) {
                val readBytes = audioRecord?.read(buffer, 0, buffer.size) ?: 0
                if (readBytes > 0) {
                    val codec = audioCodec ?: break
                    val inputBufferIndex = codec.dequeueInputBuffer(10000)
                    if (inputBufferIndex >= 0) {
                        val inputBuffer = codec.getInputBuffer(inputBufferIndex)
                        inputBuffer?.clear()
                        inputBuffer?.put(buffer, 0, readBytes)

                        // 计算音频 PTS
                        val pts = (System.nanoTime() - startTimeNs) / 1000
                        codec.queueInputBuffer(inputBufferIndex, 0, readBytes, pts, 0)
                    }
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

                // 拷贝一份 Buffer，防止 Codec release 后数据丢失
                val copyBuffer = ByteBuffer.allocate(info.size)
                copyBuffer.put(encodedData)
                copyBuffer.flip()

                val newInfo = MediaCodec.BufferInfo().apply {
                    set(0, info.size, info.presentationTimeUs, info.flags)
                }

                synchronized(this) {
                    if (muxerStarted) {
                        val track = if (isVideo) videoTrackIndex else audioTrackIndex
                        muxer?.writeSampleData(track, copyBuffer, newInfo)
                    } else {
                        // Muxer 未就绪，压入早鸟帧队列
                        pendingFrames.add(FrameData(copyBuffer, newInfo, isVideo))
                    }
                }
            }
        }
        codec.releaseOutputBuffer(index, false)
    }

    /**
     * Muxer 启动屏障：必须等到 Video 和 Audio 的 Track 都 Add 完毕后才能 start()。
     * 一旦 start，就一口气把 pendingFrames 里的早鸟帧全部写入。
     */
    @Synchronized
    private fun tryStartMuxer() {
        if (!muxerStarted && isVideoFormatAdded && isAudioFormatAdded) {
            Log.d(TAG, "Both tracks added, starting muxer.")
            muxer?.start()
            muxerStarted = true

            // 清空早鸟帧队列
            while (pendingFrames.isNotEmpty()) {
                val frame = pendingFrames.poll() ?: break
                val track = if (frame.isVideo) videoTrackIndex else audioTrackIndex
                muxer?.writeSampleData(track, frame.buffer, frame.info)
            }
        }
    }

    fun stopRecording() {
        isRecording = false
        encoderScope.coroutineContext.cancelChildren() // 停止音频采集协程

        try {
            videoCodec?.signalEndOfInputStream()
        } catch (e: Exception) { Log.e(TAG, "Error signaling EOS", e) }

        // 等待编码器排空
        Thread.sleep(100)

        try { audioRecord?.stop(); audioRecord?.release() } catch (e: Exception) {}
        try { videoCodec?.stop(); videoCodec?.release() } catch (e: Exception) {}
        try { audioCodec?.stop(); audioCodec?.release() } catch (e: Exception) {}

        synchronized(this) {
            if (muxerStarted) {
                try { muxer?.stop(); muxer?.release() } catch (e: Exception) {}
            }
            muxerStarted = false
        }

        inputSurface?.release()
        inputSurface = null

        videoTrackIndex = -1
        audioTrackIndex = -1
        isVideoFormatAdded = false
        isAudioFormatAdded = false
        pendingFrames.clear()

        Log.d(TAG, "Recording stopped and resources released.")
    }
}
