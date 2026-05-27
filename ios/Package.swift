// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "VideoSDK",
    platforms: [
        .iOS(.v12)
    ],
    products: [
        .library(
            name: "VideoSDK",
            targets: ["VideoSDK"]
        )
    ],
    dependencies: [],
    targets: [
        .target(
            name: "VideoSDK",
            dependencies: [
                // Link the generated native binary target here when publishing an XCFramework.
            ],
            path: "Classes",
            publicHeadersPath: ".",
            cxxSettings: [
                .headerSearchPath("../sdk-core/core/include"),
                .define("HAS_METAL"),
                .unsafeFlags(["-std=c++17"])
            ],
            linkerSettings: [
                .linkedFramework("Metal"),
                .linkedFramework("MetalKit"),
                .linkedFramework("QuartzCore"),
                .linkedFramework("AVFoundation")
            ]
        )
        // Binary release example:
        // .binaryTarget(
        //     name: "video_sdk_core",
        //     path: "Libraries/libvideo_sdk_core.xcframework"
        // )
    ]
)
