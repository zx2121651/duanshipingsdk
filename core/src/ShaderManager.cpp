#include "../include/ShaderManager.h"
#include <iostream>

namespace sdk {
namespace video {

ShaderManager::ShaderManager() = default;

ShaderManager::~ShaderManager() = default;

void ShaderManager::setAssetProvider(std::shared_ptr<IAssetProvider> provider) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_assetProvider = provider;
}

std::string ShaderManager::getShaderSource(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Check if we have a hot-updated or cached version
    auto it = m_shaderCache.find(name);
    if (it != m_shaderCache.end()) {
        return it->second;
    }

    // Otherwise load from asset
    if (m_assetProvider) {
        std::string source = m_assetProvider->loadShader(name);
        if (!source.empty()) {
            m_shaderCache[name] = source;
            return source;
        }
    }

    std::cerr << "ShaderManager: Failed to find shader source for " << name << std::endl;
    return "";
}

void ShaderManager::updateShaderSource(const std::string& name, const std::string& source) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_shaderCache[name] = source;
}

} // namespace video
} // namespace sdk
