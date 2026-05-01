#include "GLShaderProgram.h"
#include <iostream>
#include <cstdlib>

#ifndef GL_COMPUTE_SHADER
#define GL_COMPUTE_SHADER 0x91B9
#endif

#ifndef __APPLE__
extern "C" {
    void glDispatchCompute(GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z) __attribute__((weak));
    void glDetachShader(GLuint program, GLuint shader) __attribute__((weak));
}
#endif

namespace sdk {
namespace video {
namespace rhi {

GLShaderProgram::GLShaderProgram(const std::string& vertexSource, const std::string& fragmentSource) {
    m_programId = createProgram(vertexSource.c_str(), fragmentSource.c_str());
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

void GLShaderProgram::dispatchCompute(uint32_t numGroupsX, uint32_t numGroupsY, uint32_t numGroupsZ) {
    if (m_programId == 0 || !m_isCompute) return;
#if defined(GL_ES_VERSION_3_1) || !defined(__APPLE__)
    glUseProgram(m_programId);
#ifndef __APPLE__
    if (glDispatchCompute) {
        glDispatchCompute(numGroupsX, numGroupsY, numGroupsZ);
    }
#endif
#endif
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
