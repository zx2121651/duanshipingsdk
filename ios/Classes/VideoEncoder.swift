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

    private let width: Int
    private let height: Int

    public private(set) var isRecording = false
    private var isFirstFrame = true
    private var startTime: CMTime = .zero

    public init(width: Int, height: Int) {
        self.width = width
        self.height = height
    }

    public func startRecording(outputURL: URL) throws {
        guard !isRecording else { throw VideoEncoderError.recordingAlreadyStarted }

        // Remove existing file if necessary
        if FileManager.default.fileExists(atPath: outputURL.path) {
            try FileManager.default.removeItem(at: outputURL)
        }

        assetWriter = try AVAssetWriter(outputURL: outputURL, fileType: .mp4)

        let videoSettings: [String: Any] = [
            AVVideoCodecKey: AVVideoCodecType.h264,
            AVVideoWidthKey: width,
            AVVideoHeightKey: height
        ]

        videoInput = AVAssetWriterInput(mediaType: .video, outputSettings: videoSettings)
        videoInput?.expectsMediaDataInRealTime = true

        let sourcePixelBufferAttributes: [String: Any] = [
            kCVPixelBufferPixelFormatTypeKey as String: Int(kCVPixelFormatType_32BGRA),
            kCVPixelBufferWidthKey as String: width,
            kCVPixelBufferHeightKey as String: height
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
        guard isRecording, let assetWriter = assetWriter, let videoInput = videoInput, let adaptor = pixelBufferAdaptor else { return }

        if isFirstFrame {
            assetWriter.startSession(atSourceTime: timestamp)
            startTime = timestamp
            isFirstFrame = false
        }

        if videoInput.isReadyForMoreMediaData {
            adaptor.append(pixelBuffer, withPresentationTime: timestamp)
        }
    }

    public func appendAudioSampleBuffer(_ sampleBuffer: CMSampleBuffer) {
        guard isRecording, let audioInput = audioInput, !isFirstFrame else { return }

        if audioInput.isReadyForMoreMediaData {
            audioInput.append(sampleBuffer)
        }
    }

    public func stopRecording() async throws {
        guard isRecording, let assetWriter = assetWriter else { throw VideoEncoderError.recordingNotStarted }

        isRecording = false
        videoInput?.markAsFinished()
        audioInput?.markAsFinished()

        await assetWriter.finishWriting()

        self.assetWriter = nil
        self.videoInput = nil
        self.audioInput = nil
        self.pixelBufferAdaptor = nil
    }
}
