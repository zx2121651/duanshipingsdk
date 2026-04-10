import Foundation
import AVFoundation

public enum VideoEncoderError: Error {
    case initializationFailed
    case recordingAlreadyStarted
    case recordingNotStarted
}

public class VideoEncoder {
    private var assetWriter: AVAssetWriter?
    private var videoInput: AVAssetWriterInput?
    private var audioInput: AVAssetWriterInput?
    private var pixelBufferAdaptor: AVAssetWriterInputPixelBufferAdaptor?

    private let config: VideoExportConfig

    public private(set) var isRecording = false
    private var isFirstFrame = true
    private var startTime: CMTime = .zero
    private var lastVideoPts: CMTime = .zero
    private var lastAudioPts: CMTime = .zero

    public init(config: VideoExportConfig) {
        self.config = config
    }

    public func startRecording() throws {
        let outputURL = config.outputURL
        guard !isRecording else { throw VideoEncoderError.recordingAlreadyStarted }

        // Remove existing file if necessary
        if FileManager.default.fileExists(atPath: outputURL.path) {
            try FileManager.default.removeItem(at: outputURL)
        }

        assetWriter = try AVAssetWriter(outputURL: outputURL, fileType: .mp4)

        let videoSettings: [String: Any] = [
            AVVideoCodecKey: AVVideoCodecType.h264,
            AVVideoWidthKey: config.width,
            AVVideoHeightKey: config.height,
            AVVideoCompressionPropertiesKey: [
                AVVideoAverageBitRateKey: config.videoBitrate,
                AVVideoMaxKeyFrameIntervalKey: config.fps * config.iFrameInterval,
                AVVideoExpectedSourceFrameRateKey: config.fps,
                AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel
            ]
        ]

        videoInput = AVAssetWriterInput(mediaType: .video, outputSettings: videoSettings)
        videoInput?.expectsMediaDataInRealTime = true

        let sourcePixelBufferAttributes: [String: Any] = [
            kCVPixelBufferPixelFormatTypeKey as String: Int(kCVPixelFormatType_32BGRA),
            kCVPixelBufferWidthKey as String: config.width,
            kCVPixelBufferHeightKey as String: config.height
        ]

        if let videoInput = videoInput {
            pixelBufferAdaptor = AVAssetWriterInputPixelBufferAdaptor(assetWriterInput: videoInput, sourcePixelBufferAttributes: sourcePixelBufferAttributes)
            if assetWriter?.canAdd(videoInput) == true {
                assetWriter?.add(videoInput)
            }
        }

        // Basic Audio Settings (AAC)
        var acl = AudioChannelLayout()
        memset(&acl, 0, MemoryLayout<AudioChannelLayout>.size)
        acl.mChannelLayoutTag = kAudioChannelLayoutTag_Stereo

        let audioSettings: [String: Any] = [
            AVFormatIDKey: kAudioFormatMPEG4AAC,
            AVNumberOfChannelsKey: 2,
            AVSampleRateKey: 44100,
            AVEncoderBitRateKey: 128000
        ]

        audioInput = AVAssetWriterInput(mediaType: .audio, outputSettings: audioSettings)
        audioInput?.expectsMediaDataInRealTime = true

        if let audioInput = audioInput, assetWriter?.canAdd(audioInput) == true {
            assetWriter?.add(audioInput)
        }

        assetWriter?.startWriting()
        isRecording = true
        isFirstFrame = true
    }

    public func appendVideoPixelBuffer(_ pixelBuffer: CVPixelBuffer, timestamp: CMTime) {
        guard isRecording, let assetWriter = assetWriter else { return }
        if assetWriter.status == .failed {
            stopRecording(isFallback: true)
            return
        }
        guard assetWriter.status == .writing || isFirstFrame else { return }

        var safeTimestamp = timestamp
        if isFirstFrame {
            assetWriter.startSession(atSourceTime: safeTimestamp)
            startTime = safeTimestamp
            isFirstFrame = false
        } else if safeTimestamp <= lastVideoPts {
            // A/V Sync monotonic enforcement
            safeTimestamp = CMTimeAdd(lastVideoPts, CMTimeMake(value: 1, timescale: Int32(config.fps)))
        }

        lastVideoPts = safeTimestamp

        guard let input = videoInput, input.isReadyForMoreMediaData, let adaptor = pixelBufferAdaptor else { return }
        adaptor.append(pixelBuffer, withPresentationTime: safeTimestamp)
    }

    public func appendAudioSampleBuffer(_ sampleBuffer: CMSampleBuffer) {
        guard isRecording, let audioInput = audioInput, !isFirstFrame else { return }

        let pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer)
        if pts <= lastAudioPts {
            // Drop abnormal sample buffer to enforce strict monotonic timeline safely
            return
        }
        lastAudioPts = pts

        if audioInput.isReadyForMoreMediaData {
            audioInput.append(sampleBuffer)
        }
    }

    public func stopRecording(isFallback: Bool = false, completion: ((URL?) -> Void)? = nil) {
        guard isRecording, let writer = assetWriter else {
            completion?(nil)
            return
        }

        isRecording = false
        videoInput?.markAsFinished()
        audioInput?.markAsFinished()

        writer.finishWriting {
            DispatchQueue.main.async {
                if writer.status == .completed && !isFallback {
                    completion?(writer.outputURL)
                } else {
                    // Cleanup failed/fallback files
                    try? FileManager.default.removeItem(at: self.config.outputURL)
                    completion?(nil)
                }

                self.assetWriter = nil
                self.videoInput = nil
                self.audioInput = nil
                self.pixelBufferAdaptor = nil
            }
        }
    }
}
