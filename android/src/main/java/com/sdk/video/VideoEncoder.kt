package com.sdk.video

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.media.MediaMuxer
import android.view.Surface
import java.io.IOException
import java.nio.ByteBuffer

class VideoEncoder(
    private val width: Int = 1080,
    private val height: Int = 1920,
    private val bitRate: Int = 4000000,
    private val frameRate: Int = 30
) {

    private var encoder: MediaCodec? = null
    private var muxer: MediaMuxer? = null
    private var trackIndex: Int = -1
    private var isRecording = false
    private var inputSurface: Surface? = null
    private val bufferInfo = MediaCodec.BufferInfo()

    fun startRecording(outputPath: String): Surface? {
        if (isRecording) return inputSurface

        try {
            val format = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, width, height)
            format.setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
            format.setInteger(MediaFormat.KEY_BIT_RATE, bitRate)
            format.setInteger(MediaFormat.KEY_FRAME_RATE, frameRate)
            format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)

            encoder = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC)
            encoder?.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
            inputSurface = encoder?.createInputSurface()
            encoder?.start()

            muxer = MediaMuxer(outputPath, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4)
            trackIndex = -1
            isRecording = true

            // Start a thread to drain the encoder
            Thread { drainEncoderLoop() }.start()

            return inputSurface

        } catch (e: IOException) {
            e.printStackTrace()
            stopRecording()
            return null
        }
    }

    private fun drainEncoderLoop() {
        while (isRecording) {
            drainEncoder(false)
            Thread.sleep(10) // Small sleep to prevent busy polling
        }
        drainEncoder(true)
    }

    private fun drainEncoder(endOfStream: Boolean) {
        val codec = encoder ?: return

        if (endOfStream) {
            codec.signalEndOfInputStream()
        }

        while (true) {
            val encoderStatus = codec.dequeueOutputBuffer(bufferInfo, 10000)
            if (encoderStatus == MediaCodec.INFO_TRY_AGAIN_LATER) {
                if (!endOfStream) break // Output is not ready yet
            } else if (encoderStatus == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                if (muxer != null && trackIndex == -1) {
                    val newFormat = codec.outputFormat
                    trackIndex = muxer!!.addTrack(newFormat)
                    muxer!!.start()
                }
            } else if (encoderStatus >= 0) {
                val encodedData = codec.getOutputBuffer(encoderStatus)
                if (encodedData == null) {
                    throw RuntimeException("encoderOutputBuffer $encoderStatus was null")
                }

                if ((bufferInfo.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0) {
                    bufferInfo.size = 0
                }

                if (bufferInfo.size != 0) {
                    if (muxer != null && trackIndex != -1) {
                        encodedData.position(bufferInfo.offset)
                        encodedData.limit(bufferInfo.offset + bufferInfo.size)
                        muxer!!.writeSampleData(trackIndex, encodedData, bufferInfo)
                    }
                }

                codec.releaseOutputBuffer(encoderStatus, false)
                if ((bufferInfo.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
                    break // Stream ended
                }
            }
        }
    }

    fun stopRecording() {
        isRecording = false
        // Let the drain thread finish
        Thread.sleep(50)

        try {
            encoder?.stop()
            encoder?.release()
        } catch (e: Exception) {
            e.printStackTrace()
        }
        encoder = null

        try {
            muxer?.stop()
            muxer?.release()
        } catch (e: Exception) {
            e.printStackTrace()
        }
        muxer = null

        inputSurface?.release()
        inputSurface = null
    }
}
