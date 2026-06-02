#pragma once

#include <string>

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
};

} // namespace video
} // namespace sdk
