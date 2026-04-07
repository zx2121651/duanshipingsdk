#pragma once

#include "../../core/include/IAssetProvider.h"

namespace sdk {
namespace video {

class IOSAssetProvider : public IAssetProvider {
public:
    std::string readAsset(const std::string& path) override;
};

} // namespace video
} // namespace sdk
