#import <Foundation/Foundation.h>
#include "IOSAssetProvider.h"
#include <iostream>

namespace sdk {
namespace video {

std::string IOSAssetProvider::readAsset(const std::string& path) {
    @autoreleasepool {
        NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
        NSString* fileName = [nsPath lastPathComponent];
        NSString* dirPath = [nsPath stringByDeletingLastPathComponent];
        NSString* extension = [fileName pathExtension];
        NSString* name = [fileName stringByDeletingPathExtension];

        NSBundle* bundle = [NSBundle mainBundle];
        NSString* fullPath = [bundle pathForResource:name ofType:extension inDirectory:dirPath];

        if (!fullPath) {
            // Fallback, maybe the path is just flat in the bundle
            fullPath = [bundle pathForResource:name ofType:extension];
        }

        if (!fullPath) {
            std::cerr << "IOSAssetProvider: Failed to find asset: " << path << std::endl;
            return "";
        }

        NSError* error = nil;
        NSString* content = [NSString stringWithContentsOfFile:fullPath encoding:NSUTF8StringEncoding error:&error];

        if (error || !content) {
            std::cerr << "IOSAssetProvider: Failed to read asset: " << path << std::endl;
            return "";
        }

        return std::string([content UTF8String]);
    }
}

} // namespace video
} // namespace sdk
