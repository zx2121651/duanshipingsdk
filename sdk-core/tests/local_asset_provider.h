#include "../../core/include/ShaderManager.h"
#include <fstream>
#include <sstream>

namespace sdk {
namespace video {

class LocalAssetProvider : public IAssetProvider {
private:
    std::string m_basePath;
public:
    LocalAssetProvider(const std::string& basePath) : m_basePath(basePath) {}

    std::string readAsset(const std::string& path) override {
        std::string fullPath = m_basePath + "/" + path;
        std::ifstream file(fullPath);
        if (!file.is_open()) {
            // try looking up from parent directory, if test runs from build
            fullPath = "../" + m_basePath + "/" + path;
            file.open(fullPath);
            if (!file.is_open()) {
                std::cerr << "LocalAssetProvider: Failed to open asset: " << fullPath << std::endl;
                return "";
            }
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }
};

}
}
