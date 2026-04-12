#pragma once

#include <string>
#include "GeneratedShaders.h"

namespace sdk {
namespace video {

class IAssetProvider {
public:
    virtual ~IAssetProvider() = default;

    /**
     * Reads the entire content of an asset file.
     * @param path The relative path to the asset (e.g., "shaders/brightness.frag").
     * @return The string content of the asset, or an empty string if loading fails.
     */
    virtual std::string readAsset(const std::string& path) = 0;

    /**
     * Helper to load a shader, falling back to generated headers if not found in assets.
     */
    std::string loadShader(const std::string& name) {
        // Try file system / assets first if path-like
        std::string fullPath = "shaders/" + name;
        std::string content = readAsset(fullPath);
        if (!content.empty()) {
            return content;
        }

        // Fallback to generated header map
        const auto& shaders = GeneratedShaders::get();
        auto it = shaders.find(name);
        if (it != shaders.end()) {
            return it->second;
        }

        return "";
    }
};

} // namespace video
} // namespace sdk
