#pragma once

#include <android/asset_manager.h>
#include "../../../../../sdk-core/core/include/IAssetProvider.h"

namespace sdk {
namespace video {

class AndroidAssetProvider : public IAssetProvider {
public:
    explicit AndroidAssetProvider(AAssetManager* assetManager) : m_assetManager(assetManager) {}

    std::string readAsset(const std::string& path) override;

private:
    AAssetManager* m_assetManager;
};

} // namespace video
} // namespace sdk
