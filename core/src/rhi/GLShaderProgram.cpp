#include "GLShaderProgram.h"
#include <iostream>
#include <cstdlib>

namespace sdk {
namespace video {
namespace rhi {

GLShaderProgram::GLShaderProgram(const std::string& vertexSource, const std::string& fragmentSource) {
    m_programId = createProgram(vertexSource.c_str(), fragmentSource.c_str());
}

GLShaderProgram::~GLShaderProgram() {
    if (m_programId != 0) {
        glDeleteProgram(m_programId);
        m_programId = 0;
    }
}

void GLShaderProgram::bindUniformBlock(const std::string& blockName, uint32_t bindingPoint) {
    if (m_programId == 0) return;
    GLuint blockIndex = glGetUniformBlockIndex(m_programId, blockName.c_str());
    if (blockIndex != GL_INVALID_INDEX) {
        glUniformBlockBinding(m_programId, blockIndex, bindingPoint);
    }
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
