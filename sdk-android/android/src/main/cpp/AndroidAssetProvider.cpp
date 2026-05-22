#include "AndroidAssetProvider.h"
#include <vector>

namespace sdk {
namespace video {

std::string AndroidAssetProvider::readAsset(const std::string& path) {
    if (!m_assetManager) {
        return "";
    }

    AAsset* asset = AAssetManager_open(m_assetManager, path.c_str(), AASSET_MODE_BUFFER);
    if (!asset) {
        return "";
    }

    off_t length = AAsset_getLength(asset);
    if (length == 0) {
        AAsset_close(asset);
        return "";
    }

    std::vector<char> buffer(length);
    AAsset_read(asset, buffer.data(), length);
    AAsset_close(asset);

    return std::string(buffer.begin(), buffer.end());
}

} // namespace video
} // namespace sdk
