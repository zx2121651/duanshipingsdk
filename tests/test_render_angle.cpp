#include <iostream>
#include <vector>
#include <cstdint>
#include <memory>

// Depending on the platform, include the correct EGL header
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "../core/include/FilterEngine.h"
#include "../core/include/Filters.h"

using namespace sdk::video;

void checkEGLError(const char* operation) {
    EGLint error = eglGetError();
    if (error != EGL_SUCCESS) {
        std::cerr << "EGL Error after " << operation << ": 0x" << std::hex << error << std::dec << std::endl;
        exit(-1);
    }
}

int main() {
    std::cout << "Starting EGL/GLES Render Test Pipeline..." << std::endl;

    // 1. Initialize EGL off-screen context
    EGLDisplay eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (eglDisplay == EGL_NO_DISPLAY) {
        std::cerr << "Failed to get EGL display." << std::endl;
        return -1;
    }

    EGLint majorVersion, minorVersion;
    if (!eglInitialize(eglDisplay, &majorVersion, &minorVersion)) {
        std::cerr << "Failed to initialize EGL." << std::endl;
        return -1;
    }

    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_NONE
    };

    EGLint numConfigs;
    EGLConfig eglConfig;
    eglChooseConfig(eglDisplay, configAttribs, &eglConfig, 1, &numConfigs);
    checkEGLError("eglChooseConfig");

    EGLint pbufferAttribs[] = {
        EGL_WIDTH, 256,
        EGL_HEIGHT, 256,
        EGL_NONE
    };
    EGLSurface eglSurface = eglCreatePbufferSurface(eglDisplay, eglConfig, pbufferAttribs);
    checkEGLError("eglCreatePbufferSurface");

    EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };
    EGLContext eglContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, contextAttribs);
    checkEGLError("eglCreateContext");

    eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);
    checkEGLError("eglMakeCurrent");

    std::cout << "EGL Initialized (ES " << majorVersion << "." << minorVersion << ")" << std::endl;

    // 2. Prepare test texture (solid dark grey color)
    int width = 256;
    int height = 256;
    std::vector<uint8_t> pixels(width * height * 4, 100); // R=100, G=100, B=100, A=100

    GLuint inputTextureId;
    glGenTextures(1, &inputTextureId);
    glBindTexture(GL_TEXTURE_2D, inputTextureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // 3. Setup Filter Engine
    FilterEngine engine;
    engine.initialize();

    // Add a brightness filter to double the brightness
    auto brightnessFilter = std::make_shared<BrightnessFilter>();
    engine.addFilter(brightnessFilter);
    engine.updateParameter("brightness", 100.0f / 255.0f); // Add roughly 100 to RGB values

    // 4. Process frame through C++ Pipeline
    Texture inputTex = {inputTextureId, width, height};
    Texture outputTex = engine.processFrame(inputTex, width, height);

    // 5. Read output and verify
    // Create an FBO to read the output texture pixels
    GLuint readFbo;
    glGenFramebuffers(1, &readFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, readFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTex.id, 0);

    std::vector<uint8_t> resultPixels(width * height * 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, resultPixels.data());

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &readFbo);

    // 6. Verification check
    std::cout << "Input pixel color: R=" << (int)pixels[0] << ", G=" << (int)pixels[1] << ", B=" << (int)pixels[2] << std::endl;
    std::cout << "Output pixel color: R=" << (int)resultPixels[0] << ", G=" << (int)resultPixels[1] << ", B=" << (int)resultPixels[2] << std::endl;

    bool passed = true;
    // Expected value: 100 + 100 = 200
    if (abs((int)resultPixels[0] - 200) > 2) passed = false;

    if (passed) {
        std::cout << "Test passed: Real rendering pipeline executed correctly." << std::endl;
    } else {
        std::cerr << "Test failed: Pixel output does not match expected result." << std::endl;
    }

    // Cleanup
    engine.release();
    glDeleteTextures(1, &inputTextureId);

    eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(eglDisplay, eglContext);
    eglDestroySurface(eglDisplay, eglSurface);
    eglTerminate(eglDisplay);

    return passed ? 0 : -1;
}
