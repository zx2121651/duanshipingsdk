#include "GLShaderProgram.h"
#include <iostream>
#include <cstdlib>

#ifndef GL_COMPUTE_SHADER
#define GL_COMPUTE_SHADER 0x91B9
#endif

#if !defined(__APPLE__) && !defined(_MSC_VER) && !defined(USE_MOCK_GL)
extern "C" {
    void glDetachShader(GLuint program, GLuint shader) __attribute__((weak));
}
#endif
// On MSVC with USE_MOCK_GL, glDetachShader is provided as an inline stub in mock_gl/GLES3/gl3.h

namespace sdk {
namespace video {
namespace rhi {

GLShaderProgram::GLShaderProgram(const std::string& vertexSource, const std::string& fragmentSource) {
    m_programId = createProgram(vertexSource.c_str(), fragmentSource.c_str());
}

GLShaderProgram::GLShaderProgram(GLuint prelinkedProgramId) {
    m_programId = prelinkedProgramId;
}

GLShaderProgram::GLShaderProgram(const std::string& computeSource) {
    m_isCompute = true;
    GLuint computeShader = loadShader(GL_COMPUTE_SHADER, computeSource.c_str());
    if (!computeShader) {
        m_programId = 0;
        return;
    }

    m_programId = glCreateProgram();
    if (m_programId) {
        glAttachShader(m_programId, computeShader);
        glLinkProgram(m_programId);
        GLint linkStatus = GL_FALSE;
        glGetProgramiv(m_programId, GL_LINK_STATUS, &linkStatus);
        if (linkStatus != GL_TRUE) {
            GLint bufLength = 0;
            glGetProgramiv(m_programId, GL_INFO_LOG_LENGTH, &bufLength);
            if (bufLength) {
                char* buf = (char*)std::malloc(bufLength);
                if (buf) {
                    glGetProgramInfoLog(m_programId, bufLength, nullptr, buf);
                    std::cerr << "Could not link compute program:\n" << buf << std::endl;
                    std::free(buf);
                }
            }
            glDeleteProgram(m_programId);
            m_programId = 0;
        }
        if (m_programId) {
            glDetachShader(m_programId, computeShader);
        }
    }
    if (computeShader) {
        glDeleteShader(computeShader);
    }
}

GLShaderProgram::~GLShaderProgram() {
    if (m_programId != 0) {
        glDeleteProgram(m_programId);
        m_programId = 0;
    }
}

// --- Uniform helpers ---

GLint GLShaderProgram::getUniformLocation(const std::string& name) const {
    auto it = m_uniformCache.find(name);
    if (it != m_uniformCache.end()) return it->second;
    GLint loc = glGetUniformLocation(m_programId, name.c_str());
    m_uniformCache[name] = loc;
    return loc;
}

void GLShaderProgram::setUniform1i(const std::string& name, int value) {
    GLint loc = getUniformLocation(name);
    if (loc >= 0) glUniform1i(loc, value);
}

void GLShaderProgram::setUniform1f(const std::string& name, float value) {
    GLint loc = getUniformLocation(name);
    if (loc >= 0) glUniform1f(loc, value);
}

void GLShaderProgram::setUniform2f(const std::string& name, float x, float y) {
    GLint loc = getUniformLocation(name);
    if (loc >= 0) glUniform2f(loc, x, y);
}

void GLShaderProgram::setUniform4f(const std::string& name, float x, float y, float z, float w) {
    GLint loc = getUniformLocation(name);
    if (loc >= 0) glUniform4f(loc, x, y, z, w);
}

void GLShaderProgram::setUniformMat4(const std::string& name, const float* matrix4x4) {
    GLint loc = getUniformLocation(name);
    if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, matrix4x4);
}

void GLShaderProgram::bind() {
    glUseProgram(m_programId);
}

void GLShaderProgram::unbind() {
    glUseProgram(0);
}

GLuint GLShaderProgram::loadShader(GLenum shaderType, const char* pSource) {
    GLuint shader = glCreateShader(shaderType);
    if (shader) {
        glShaderSource(shader, 1, &pSource, nullptr);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLint infoLen = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen) {
                char* buf = (char*)std::malloc(infoLen);
                if (buf) {
                    glGetShaderInfoLog(shader, infoLen, nullptr, buf);
                    std::cerr << "Could not compile shader " << shaderType << ":\n" << buf << std::endl;
                    std::free(buf);
                }
                glDeleteShader(shader);
                shader = 0;
            }
        }
    }
    return shader;
}

GLuint GLShaderProgram::createProgram(const char* pVertexSource, const char* pFragmentSource) {
    GLuint vertexShader = loadShader(GL_VERTEX_SHADER, pVertexSource);
    if (!vertexShader) return 0;

    GLuint pixelShader = loadShader(GL_FRAGMENT_SHADER, pFragmentSource);
    if (!pixelShader) return 0;

    GLuint program = glCreateProgram();
    if (program) {
        glAttachShader(program, vertexShader);
        glAttachShader(program, pixelShader);
        glLinkProgram(program);
        GLint linkStatus = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
        if (linkStatus != GL_TRUE) {
            GLint bufLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
            if (bufLength) {
                char* buf = (char*)std::malloc(bufLength);
                if (buf) {
                    glGetProgramInfoLog(program, bufLength, nullptr, buf);
                    std::cerr << "Could not link program:\n" << buf << std::endl;
                    std::free(buf);
                }
            }
            glDeleteProgram(program);
            program = 0;
        }
    }
    return program;
}

} // namespace rhi
} // namespace video
} // namespace sdk
