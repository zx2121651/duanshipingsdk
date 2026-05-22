#include "GLTexture.h"

#ifdef __APPLE__
    #include <OpenGLES/ES3/gl.h>
#else
    #include <GLES3/gl3.h>
#endif

namespace sdk {
namespace video {
namespace rhi {

GLTexture::~GLTexture() {
    if (m_ownsHandle && m_id != 0) {
        glDeleteTextures(1, &m_id);
    }
}

} // namespace rhi
} // namespace video
} // namespace sdk
