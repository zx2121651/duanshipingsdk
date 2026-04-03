import Foundation
import AVFoundation
import CoreVideo

/// A commercial-grade, hardware-accelerated Video Encoder for iOS.
/// Implements the AVFoundation / VideoToolbox tech stack requirements from AGENTS.md.
/// It uses AVAssetWriter to asynchronously encode raw CVPixelBuffers to H.264/AAC.
public class VideoEncoder {

    private var assetWriter: AVAssetWriter?
    private var videoInput: AVAssetWriterInput?
    private var pixelBufferAdaptor: AVAssetWriterInputPixelBufferAdaptor?

    private let outputURL: URL
    private let width: Int
    private let height: Int
    private let bitRate: Int
    private let fps: Int

    private var isRecording = false
    private var hasStartedSession = false
    private var firstFrameTime: CMTime = .invalid

    // Serial queue for all encoding operations to prevent deadlocks and frame drops
    private let encodingQueue = DispatchQueue(label: "com.sdk.video.encodingQueue", qos: .userInitiated)

    public init(outputURL: URL, width: Int = 1080, height: Int = 1920, bitRate: Int = 4000000, fps: Int = 30) {
        self.outputURL = outputURL
        self.width = width
        self.height = height
        self.bitRate = bitRate
        self.fps = fps
    }

    /// Starts the hardware recording pipeline.
    /// Returns 0 on success, or a negative error code (e.g., -1 for AVAssetWriter failure).
    public func startRecording() -> Int {
        guard !isRecording else { return 0 }

        do {
            if FileManager.default.fileExists(atPath: outputURL.path) {
                try FileManager.default.removeItem(at: outputURL)
            }

            assetWriter = try AVAssetWriter(outputURL: outputURL, fileType: .mp4)
        } catch {
            print("VideoEncoder Error: Failed to initialize AVAssetWriter: \(error.localizedDescription)")
            return -1 // Setup failure
        }

        // 1. Configure VideoToolbox H.264 Compression Settings
        let videoSettings: [String: Any] = [
            AVVideoCodecKey: AVVideoCodecType.h264,
            AVVideoWidthKey: width,
            AVVideoHeightKey: height,
            AVVideoCompressionPropertiesKey: [
                AVVideoAverageBitRateKey: bitRate,
                AVVideoMaxKeyFrameIntervalKey: fps, // Keyframe every 1 second
                AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel // High Profile for best quality-to-size ratio
            ]
        ]

        videoInput = AVAssetWriterInput(mediaType: .video, outputSettings: videoSettings)
        videoInput?.expectsMediaDataInRealTime = true // Critical for live camera feed

        // 2. Setup PixelBuffer Adaptor to seamlessly ingest CoreVideo buffers
        let sourcePixelBufferAttributes: [String: Any] = [
            kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA,
            kCVPixelBufferWidthKey as String: width,
            kCVPixelBufferHeightKey as String: height
        ]

        pixelBufferAdaptor = AVAssetWriterInputPixelBufferAdaptor(
            assetWriterInput: videoInput!,
            sourcePixelBufferAttributes: sourcePixelBufferAttributes
        )

        if assetWriter?.canAdd(videoInput!) == true {
            assetWriter?.add(videoInput!)
        } else {
            return -2 // Failed to add video input
        }

        // Note: Audio track setup (AVAssetWriterInput for .audio) would go here similarly.
        // It should consume CMSampleBuffer from AVCaptureAudioDataOutput.

        if assetWriter?.startWriting() == true {
            isRecording = true
            hasStartedSession = false
            firstFrameTime = .invalid
            print("VideoEncoder: Recording started successfully.")
            return 0
        } else {
            print("VideoEncoder Error: Failed to start writing. \(String(describing: assetWriter?.error))")
            return -3 // Failed to start writing
        }
    }

    /// Asynchronously append a processed CVPixelBuffer to the hardware encoder.
    public func processFrame(_ pixelBuffer: CVPixelBuffer, presentationTime: CMTime) {
        guard isRecording, let assetWriter = assetWriter, let videoInput = videoInput, let adaptor = pixelBufferAdaptor else {
            return
        }

        encodingQueue.async { [weak self] in
            guard let self = self, self.isRecording else { return }

            if !self.hasStartedSession {
                assetWriter.startSession(atSourceTime: presentationTime)
                self.hasStartedSession = true
                self.firstFrameTime = presentationTime
            }

            if videoInput.isReadyForMoreMediaData {
                let success = adaptor.append(pixelBuffer, withPresentationTime: presentationTime)
                if !success {
                    print("VideoEncoder Warning: Failed to append pixel buffer at time \(presentationTime.seconds). Error: \(String(describing: assetWriter.error))")
                }
            } else {
                // Drop frame if encoder is struggling (prevents memory bloat / OOM)
                print("VideoEncoder Warning: Video input not ready. Dropping frame at time \(presentationTime.seconds).")
            }
        }
    }

    /// Stops recording and finalizes the MP4 file.
    public func stopRecording(completion: @escaping (Result<URL, Error>) -> Void) {
        guard isRecording, let assetWriter = assetWriter, let videoInput = videoInput else {
            completion(.failure(NSError(domain: "VideoEncoder", code: -4, userInfo: [NSLocalizedDescriptionKey: "Not recording"])))
            return
        }

        isRecording = false

        encodingQueue.async {
            videoInput.markAsFinished()
            assetWriter.finishWriting {
                if assetWriter.status == .completed {
                    print("VideoEncoder: Recording finished and saved to \(self.outputURL.path).")
                    completion(.success(self.outputURL))
                } else {
                    let error = assetWriter.error ?? NSError(domain: "VideoEncoder", code: -5, userInfo: [NSLocalizedDescriptionKey: "Unknown error during finishWriting"])
                    print("VideoEncoder Error: Failed to finish writing. \(error.localizedDescription)")
                    completion(.failure(error))
                }
            }
        }
    }
}
