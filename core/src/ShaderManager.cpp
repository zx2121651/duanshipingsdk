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

std::string ShaderManager::getShaderSource(const std::string& name, const std::string& fallbackSource) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Check if we have a hot-updated or cached version
    auto it = m_shaderCache.find(name);
    if (it != m_shaderCache.end()) {
        return it->second;
    }

    // Otherwise load from asset
    if (m_assetProvider) {
        std::string path = "shaders/" + name;
        std::string source = m_assetProvider->readAsset(path);
        if (!source.empty()) {
            m_shaderCache[name] = source;
            return source;
        }
    }

    if (!fallbackSource.empty()) {
        std::cerr << "ShaderManager: Falling back to built-in source for " << name << std::endl;
        m_shaderCache[name] = fallbackSource;
        return fallbackSource;
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
