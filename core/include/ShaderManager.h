#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include "IAssetProvider.h"

namespace sdk {
namespace video {

class ShaderManager {
public:
    ShaderManager();
    ~ShaderManager();

    // Set the asset provider for loading initial shader sources
    void setAssetProvider(std::shared_ptr<IAssetProvider> provider);

    // Retrieve the shader source for a given name/path.
    // Uses the cached hot-updated version if available, otherwise falls back to asset loading.
    std::string getShaderSource(const std::string& name);

    // Update the shader source directly (Hot Update API)
    void updateShaderSource(const std::string& name, const std::string& source);

private:
    std::shared_ptr<IAssetProvider> m_assetProvider;
    std::unordered_map<std::string, std::string> m_shaderCache;
    std::mutex m_mutex;
};

} // namespace video
} // namespace sdk
