import Foundation

public struct VideoExportConfig {
    public let width: Int
    public let height: Int
    public let fps: Int
    public let videoBitrate: Int
    public let audioBitrate: Int
    public let audioSampleRate: Int
    public let iFrameInterval: Int
    public let outputURL: URL

    public init(
        width: Int = 1080,
        height: Int = 1920,
        fps: Int = 30,
        videoBitrate: Int = 10_000_000,
        audioBitrate: Int = 128_000,
        audioSampleRate: Int = 44100,
        iFrameInterval: Int = 1,
        outputURL: URL
    ) {
        self.width = width
        self.height = height
        self.fps = fps
        self.videoBitrate = videoBitrate
        self.audioBitrate = audioBitrate
        self.audioSampleRate = audioSampleRate
        self.iFrameInterval = iFrameInterval
        self.outputURL = outputURL
    }
}
