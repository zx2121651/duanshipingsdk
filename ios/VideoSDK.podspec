Pod::Spec.new do |s|
  s.name             = 'VideoSDK'
  s.version          = '1.0.0'
  s.summary          = 'High-performance short video editing and effects SDK for iOS.'

  s.description      = <<-DESC
                       VideoSDK wraps the cross-platform C++ video engine for iOS.
                       It provides native Swift and Objective-C++ bridge APIs for
                       camera filters, timeline editing, effects, and video export.
                       DESC

  s.homepage         = 'https://github.com/your-org/duanshipingsdk'
  s.license          = { :type => 'Apache-2.0', :file => '../LICENSE' }
  s.author           = { 'SDK Team' => 'sdk-team@example.com' }
  s.source           = { :git => 'https://github.com/your-org/duanshipingsdk.git', :tag => s.version.to_s }

  s.ios.deployment_target = '12.0'
  s.swift_version = '5.0'

  s.source_files = 'Classes/**/*.{h,m,mm,swift}'
  s.public_header_files = 'Classes/**/*.h'

  s.frameworks = 'UIKit', 'AVFoundation', 'Metal', 'MetalKit', 'QuartzCore'

  s.pod_target_xcconfig = {
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++17',
    'CLANG_CXX_LIBRARY' => 'libc++',
    'HEADER_SEARCH_PATHS' => '"$(PODS_TARGET_SRCROOT)/../sdk-core/core/include"'
  }

  # For a binary release, place the generated library under ios/Libraries
  # and enable the matching vendored library or framework declaration.
  # s.vendored_libraries = 'Libraries/libvideo_sdk_core.a'
  # s.vendored_frameworks = 'Libraries/VideoSDK.xcframework'
end
