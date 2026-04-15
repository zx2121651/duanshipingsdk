#include <array>
#include "../include/Filter.h"
#include <iostream>
#include <cstdlib>

namespace sdk {
namespace video {

Filter::Filter() : m_programId(0) {}

Filter::~Filter() {
    release();
}

Result Filter::initialize() {
    release();

    std::string vertexShaderSource = getVertexShaderSource();
    std::string fragmentShaderSource = getFragmentShaderSource();

    if (m_shaderManager) {
        vertexShaderSource = m_shaderManager->getShaderSource(getVertexShaderName(), vertexShaderSource);
        fragmentShaderSource = m_shaderManager->getShaderSource(getFragmentShaderName(), fragmentShaderSource);
    }

    m_programId = createProgram(vertexShaderSource.c_str(), fragmentShaderSource.c_str());
    if (m_programId == 0) {
        return Result::error(ErrorCode::ERR_INIT_SHADER_FAILED, "Failed to create program for " + getFragmentShaderName());
    }

    m_positionHandle = glGetAttribLocation(m_programId, "position");
    m_texCoordHandle = glGetAttribLocation(m_programId, "inputTextureCoordinate");
    m_inputImageTextureHandle = glGetUniformLocation(m_programId, "inputImageTexture");

    return Result::ok();
}

void Filter::release() {
    if (m_programId) {
        glDeleteProgram(m_programId);
        m_programId = 0;
    }
}

ResultPayload<Texture> Filter::processFrame(const Texture& inputTexture, FrameBufferPtr outputFb) {
    if (m_programId == 0) {
        return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Filter not initialized or program invalid.");
    }

    if (!outputFb) {
        return ResultPayload<Texture>::error(ErrorCode::ERR_RENDER_INVALID_STATE, "Output framebuffer is null.");
    }

    onDraw(inputTexture, outputFb);
    return ResultPayload<Texture>::ok(outputFb->getTexture());
}

void Filter::setParameter(const std::string& key, const std::any& value) {
    m_parameters[key] = value;
}

void Filter::setParameterMat4(const std::string& key, const float* matrix) {
    if (matrix) {
        std::array<float, 16> arr;
        std::copy(matrix, matrix + 16, arr.begin());
        m_mat4Parameters[key] = arr;
    }
}


std::string Filter::getVertexShaderName() const {
    return "default.vert";
}

std::string Filter::getVertexShaderSource() const {
    return "";
}

GLuint Filter::loadShader(GLenum shaderType, const char* pSource) {
    GLuint shader = glCreateShader(shaderType);
    if (shader) {
        glShaderSource(shader, 1, &pSource, NULL);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLint infoLen = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen) {
                char* buf = (char*)std::malloc(infoLen);
                if (buf) {
                    glGetShaderInfoLog(shader, infoLen, NULL, buf);
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

GLuint Filter::createProgram(const char* pVertexSource, const char* pFragmentSource) {
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
                    glGetProgramInfoLog(program, bufLength, NULL, buf);
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



void Filter::recompileProgram() {
    std::string vertexShaderSource = getVertexShaderSource();
    std::string fragmentShaderSource = getFragmentShaderSource();

    if (m_shaderManager) {
        vertexShaderSource = m_shaderManager->getShaderSource(getVertexShaderName(), vertexShaderSource);
        fragmentShaderSource = m_shaderManager->getShaderSource(getFragmentShaderName(), fragmentShaderSource);
    }

    // Create new program
    GLuint newProgramId = createProgram(vertexShaderSource.c_str(), fragmentShaderSource.c_str());
    if (newProgramId != 0) {
        // Delete old
        if (m_programId != 0) {
            glDeleteProgram(m_programId);
        }
        m_programId = newProgramId;

        // Update locations
        m_positionHandle = glGetAttribLocation(m_programId, "position");
        m_texCoordHandle = glGetAttribLocation(m_programId, "inputTextureCoordinate");
        m_inputImageTextureHandle = glGetUniformLocation(m_programId, "inputImageTexture");

        onProgramRecompiled();
    }
}

} // namespace video
} // namespace sdk
