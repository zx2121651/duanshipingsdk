#include "../include/Filter.h"
#include <iostream>
#include <cstdlib>

namespace sdk {
namespace video {

// A standard passthrough vertex shader for full-screen quads.
const char* kDefaultVertexShader = R"(
#version 300 es
layout(location = 0) in vec4 position;
layout(location = 1) in vec2 inputTextureCoordinate;
out vec2 textureCoordinate;
void main() {
    gl_Position = position;
    textureCoordinate = inputTextureCoordinate;
}
)";

Filter::Filter() : m_programId(0) {}

Filter::~Filter() {
    release();
}

void Filter::initialize() {
    m_programId = createProgram(getVertexShaderSource(), getFragmentShaderSource());
    if (m_programId == 0) {
        std::cerr << "Failed to create program." << std::endl;
        return;
    }

    m_positionHandle = glGetAttribLocation(m_programId, "position");
    m_texCoordHandle = glGetAttribLocation(m_programId, "inputTextureCoordinate");
    m_inputImageTextureHandle = glGetUniformLocation(m_programId, "inputImageTexture");
}

void Filter::release() {
    if (m_programId) {
        glDeleteProgram(m_programId);
        m_programId = 0;
    }
}

Texture Filter::processFrame(const Texture& inputTexture, FrameBufferPtr outputFb) {
    if (m_programId == 0) {
        std::cerr << "Filter not initialized or program invalid." << std::endl;
        return inputTexture; // Passthrough if error
    }

    if (!outputFb) {
        std::cerr << "Output framebuffer is null." << std::endl;
        return inputTexture;
    }

    onDraw(inputTexture, outputFb);
    return outputFb->getTexture();
}

void Filter::setParameter(const std::string& key, const std::any& value) {
    m_parameters[key] = value;
}

const char* Filter::getVertexShaderSource() const {
    return kDefaultVertexShader;
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

} // namespace video
} // namespace sdk
