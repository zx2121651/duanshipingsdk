# iOS Integration

The current iOS package is source-based and lives under `ios/`.

## CocoaPods

In your app `Podfile`:

```ruby
pod 'VideoSDK', :path => 'path/to/ShortVideoSDK/ios'
```

Then run:

```bash
pod install
```

## Swift Package Manager

The workspace includes:

```text
ios/Package.swift
```

Add the local package in Xcode or point Swift Package Manager at the repository once it is hosted.

## Requirements

- iOS 12.0 or later
- Swift 5
- C++17
- Frameworks: UIKit, AVFoundation, Metal, MetalKit, QuartzCore

## Future Binary Form

For a more standard commercial SDK delivery, package the iOS layer as:

```text
ios/
├─ VideoSDK.xcframework
├─ VideoSDK.podspec
└─ README.md
```

Until that binary package exists, ship the `Classes/` source bridge, `VideoSDK.podspec`, and `Package.swift`.
