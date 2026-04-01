package com.sdk.video

import android.annotation.SuppressLint
import android.media.*
import android.os.Handler
import android.os.HandlerThread
import android.view.Surface
import kotlinx.coroutines.*
import java.io.IOException
import java.nio.ByteBuffer
import java.util.concurrent.ConcurrentLinkedQueue
import java.util.concurrent.atomic.AtomicBoolean

class VideoEncoder(
    private val width: Int = 1080,
    private val height: Int = 1920,
    private val videoBitRate: Int = 4000000,
    private val videoFrameRate: Int = 30,
    private val audioSampleRate: Int = 44100,
    private val audioBitRate: Int = 128000
) {
    private var videoCodec: MediaCodec? = null
    private var audioCodec: MediaCodec? = null
    private var muxer: MediaMuxer? = null

    private var videoTrackIndex = -1
    private var audioTrackIndex = -1

    private var inputSurface: Surface? = null
    private var audioRecord: AudioRecord? = null

    // Concurrency state
    private val isRecording = AtomicBoolean(false)
    private val muxerStarted = AtomicBoolean(false)

    // Muxer start barrier
    private var isVideoFormatAdded = false
    private var isAudioFormatAdded = false

    // Handlers for asynchronous callbacks
    private var videoHandlerThread: HandlerThread? = null
    private var audioHandlerThread: HandlerThread? = null

    // Coroutine scope for blocking audio reading
    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())
    private var audioRecordJob: Job? = null

    // Buffer queues to hold packets before muxer starts
    private val pendingVideoData = ConcurrentLinkedQueue<MuxerData>()
    private val pendingAudioData = ConcurrentLinkedQueue<MuxerData>()

    // Time tracking to align PTS
    private var startTimeNs: Long = 0

    private class MuxerData(
        val bufferInfo: MediaCodec.BufferInfo,
        val byteBuffer: ByteBuffer
    )

    @SuppressLint("MissingPermission")
    fun startRecording(outputPath: String): Surface? {
        if (isRecording.get()) return inputSurface

        try {
            muxerStarted.set(false)
            isVideoFormatAdded = false
            isAudioFormatAdded = false
            videoTrackIndex = -1
            audioTrackIndex = -1
            startTimeNs = System.nanoTime()

            muxer = MediaMuxer(outputPath, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4)

            // 1. Setup Video Codec
            videoHandlerThread = HandlerThread("VideoCodecCallback").apply { start() }
            val videoFormat = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, width, height).apply {
                setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
                setInteger(MediaFormat.KEY_BIT_RATE, videoBitRate)
                setInteger(MediaFormat.KEY_FRAME_RATE, videoFrameRate)
                setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)
            }
            videoCodec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC)
            videoCodec?.setCallback(VideoCodecCallback(), Handler(videoHandlerThread!!.looper))
            videoCodec?.configure(videoFormat, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
            inputSurface = videoCodec?.createInputSurface()

            // 2. Setup Audio Codec
            audioHandlerThread = HandlerThread("AudioCodecCallback").apply { start() }
            val audioFormat = MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_AAC, audioSampleRate, 1).apply {
                setInteger(MediaFormat.KEY_AAC_PROFILE, MediaCodecInfo.CodecProfileLevel.AACObjectLC)
                setInteger(MediaFormat.KEY_BIT_RATE, audioBitRate)
                setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 16384)
            }
            audioCodec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_AUDIO_AAC)
            audioCodec?.setCallback(AudioCodecCallback(), Handler(audioHandlerThread!!.looper))
            audioCodec?.configure(audioFormat, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)

            // 3. Setup AudioRecord
            val bufferSize = AudioRecord.getMinBufferSize(
                audioSampleRate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT
            ) * 2

            audioRecord = AudioRecord(
                MediaRecorder.AudioSource.MIC,
                audioSampleRate,
                AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_16BIT,
                bufferSize
            )

            // Start everything
            isRecording.set(true)
            videoCodec?.start()
            audioCodec?.start()
            audioRecord?.startRecording()

            // Start blocking read loop for Audio PCM data
            startAudioRecordLoop(bufferSize)

            return inputSurface

        } catch (e: Exception) {
            e.printStackTrace()
            stopRecording()
            return null
        }
    }

    private fun startAudioRecordLoop(bufferSize: Int) {
        audioRecordJob = scope.launch {
            val pcmBuffer = ByteArray(bufferSize)
            while (isActive && isRecording.get()) {
                val readResult = audioRecord?.read(pcmBuffer, 0, bufferSize) ?: -1
                if (readResult > 0) {
                    val codec = audioCodec ?: break
                    // We must dequeue input buffer synchronously because we are pumping data from AudioRecord
                    val inputBufferIndex = try {
                        codec.dequeueInputBuffer(10000)
                    } catch (e: Exception) { -1 }

                    if (inputBufferIndex >= 0) {
                        val inputBuffer = codec.getInputBuffer(inputBufferIndex)
                        if (inputBuffer != null) {
                            inputBuffer.clear()
                            inputBuffer.put(pcmBuffer, 0, readResult)
                            val ptsUsec = (System.nanoTime() - startTimeNs) / 1000
                            codec.queueInputBuffer(inputBufferIndex, 0, readResult, ptsUsec, 0)
                        }
                    }
                }
            }
        }
    }

    // --- Codec Callbacks ---

    private inner class VideoCodecCallback : MediaCodec.Callback() {
        override fun onInputBufferAvailable(codec: MediaCodec, index: Int) {
            // Ignored, using Surface
        }

        override fun onOutputBufferAvailable(codec: MediaCodec, index: Int, info: MediaCodec.BufferInfo) {
            handleOutputBuffer(codec, index, info, isVideo = true)
        }

        override fun onOutputFormatChanged(codec: MediaCodec, format: MediaFormat) {
            handleOutputFormatChanged(format, isVideo = true)
        }

        override fun onError(codec: MediaCodec, e: MediaCodec.CodecException) {
            e.printStackTrace()
        }
    }

    private inner class AudioCodecCallback : MediaCodec.Callback() {
        override fun onInputBufferAvailable(codec: MediaCodec, index: Int) {
            // We pump data manually in the startAudioRecordLoop
        }

        override fun onOutputBufferAvailable(codec: MediaCodec, index: Int, info: MediaCodec.BufferInfo) {
            handleOutputBuffer(codec, index, info, isVideo = false)
        }

        override fun onOutputFormatChanged(codec: MediaCodec, format: MediaFormat) {
            handleOutputFormatChanged(format, isVideo = false)
        }

        override fun onError(codec: MediaCodec, e: MediaCodec.CodecException) {
            e.printStackTrace()
        }
    }

    // --- Muxer Synchronization ---

    @Synchronized
    private fun handleOutputFormatChanged(format: MediaFormat, isVideo: Boolean) {
        if (!isRecording.get() || muxer == null) return

        if (isVideo) {
            videoTrackIndex = muxer!!.addTrack(format)
            isVideoFormatAdded = true
        } else {
            audioTrackIndex = muxer!!.addTrack(format)
            isAudioFormatAdded = true
        }

        // Muxer Barrier
        if (isVideoFormatAdded && isAudioFormatAdded && !muxerStarted.get()) {
            muxer!!.start()
            muxerStarted.set(true)

            // Drain pending queues
            drainPendingQueue(pendingVideoData, videoTrackIndex)
            drainPendingQueue(pendingAudioData, audioTrackIndex)
        }
    }

    @Synchronized
    private fun handleOutputBuffer(codec: MediaCodec, index: Int, info: MediaCodec.BufferInfo, isVideo: Boolean) {
        if (!isRecording.get()) {
            try { codec.releaseOutputBuffer(index, false) } catch (e: Exception) {}
            return
        }

        val outputBuffer = codec.getOutputBuffer(index)
        if (outputBuffer != null && info.size != 0 && (info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG) == 0) {

            // Ensure PTS is aligned to our startTimeNs (if not coming from GL perfectly)
            if (isVideo) {
                // Video PTS comes from GL Surface Presentation Time (already in nanoseconds originally,
                // but MediaCodec outputs microseconds usually, we just accept what codec gives if we align at source,
                // or we enforce System.nanoTime difference here for fallback).
                // In our NativeBridge.cpp we used `eglPresentationTimeANDROID`.
            }

            outputBuffer.position(info.offset)
            outputBuffer.limit(info.offset + info.size)

            if (muxerStarted.get()) {
                val trackIndex = if (isVideo) videoTrackIndex else audioTrackIndex
                try {
                    muxer?.writeSampleData(trackIndex, outputBuffer, info)
                } catch (e: Exception) { e.printStackTrace() }
            } else {
                // Cache data until muxer starts
                val byteBuffer = ByteBuffer.allocate(info.size)
                byteBuffer.put(outputBuffer)
                byteBuffer.flip()

                val newInfo = MediaCodec.BufferInfo().apply {
                    set(0, info.size, info.presentationTimeUs, info.flags)
                }

                val data = MuxerData(newInfo, byteBuffer)
                if (isVideo) {
                    pendingVideoData.add(data)
                } else {
                    pendingAudioData.add(data)
                }
            }
        }

        try { codec.releaseOutputBuffer(index, false) } catch (e: Exception) {}
    }

    private fun drainPendingQueue(queue: ConcurrentLinkedQueue<MuxerData>, trackIndex: Int) {
        while (queue.isNotEmpty()) {
            val data = queue.poll()
            if (data != null && muxer != null) {
                try {
                    muxer!!.writeSampleData(trackIndex, data.byteBuffer, data.bufferInfo)
                } catch (e: Exception) { e.printStackTrace() }
            }
        }
    }

    fun stopRecording() {
        if (!isRecording.getAndSet(false)) return

        audioRecordJob?.cancel()

        try {
            audioRecord?.stop()
            audioRecord?.release()
        } catch (e: Exception) { e.printStackTrace() }
        audioRecord = null

        try {
            videoCodec?.signalEndOfInputStream() // Important for proper MP4 closing
            videoCodec?.stop()
            videoCodec?.release()
        } catch (e: Exception) { e.printStackTrace() }
        videoCodec = null

        try {
            audioCodec?.stop()
            audioCodec?.release()
        } catch (e: Exception) { e.printStackTrace() }
        audioCodec = null

        try {
            if (muxerStarted.get()) {
                muxer?.stop()
            }
            muxer?.release()
        } catch (e: Exception) { e.printStackTrace() }
        muxer = null
        muxerStarted.set(false)

        inputSurface?.release()
        inputSurface = null

        videoHandlerThread?.quitSafely()
        audioHandlerThread?.quitSafely()
        videoHandlerThread = null
        audioHandlerThread = null

        pendingVideoData.clear()
        pendingAudioData.clear()
    }
}
