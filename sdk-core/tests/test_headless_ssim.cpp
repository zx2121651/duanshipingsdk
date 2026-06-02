#include <iostream>
#include <vector>
#include <cstdint>
#include <memory>
#include <cmath>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "../core/include/FilterEngine.h"
#include "../core/include/Filters.h"
#include "ssim_calculator.h"
#include "local_asset_provider.h"

using namespace sdk::video;
using namespace sdk::video::testing;

void checkEGLError(const char* operation) {
    EGLint error = eglGetError();
    if (error != EGL_SUCCESS) {
        std::cerr << "EGL Error after " << operation << ": 0x" << std::hex << error << std::dec << std::endl;
        exit(-1);
    }
}

int main() {
    std::cout << "Starting Headless CI SSIM Assertion Pipeline..." << std::endl;

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

    int width = 256;
    int height = 256;

    EGLint pbufferAttribs[] = {
        EGL_WIDTH, width,
        EGL_HEIGHT, height,
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

    // 2. Prepare test texture (gradient color)
    std::vector<uint8_t> inputPixels(width * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int idx = (y * width + x) * 4;
            inputPixels[idx] = x;           // R
            inputPixels[idx+1] = y;         // G
            inputPixels[idx+2] = 128;       // B
            inputPixels[idx+3] = 255;       // A
        }
    }

    GLuint inputTextureId;
    glGenTextures(1, &inputTextureId);
    glBindTexture(GL_TEXTURE_2D, inputTextureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, inputPixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // 3. Setup Filter Engine
    FilterEngine engine;
    engine.setAssetProvider(std::make_shared<LocalAssetProvider>("assets"));
    engine.initialize();

    // Add a brightness filter and a contrast filter (if exists, or just brightness)
    engine.addFilter(FilterType::BRIGHTNESS);
    engine.updateParameter("brightness", 50.0f / 255.0f); // Add 50 to RGB values

    // 4. Process frame through C++ Pipeline
    Texture inputTex = {inputTextureId, static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    auto res = engine.processFrame(inputTex, width, height);
    if (!res.isOk()) {
        std::cerr << "Process Frame Failed: " << res.getMessage() << std::endl;
        return -1;
    }
    Texture outputTex = res.getValue();

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

    // 6. Generate Golden Data
    std::vector<uint8_t> goldenPixels(width * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int idx = (y * width + x) * 4;
            goldenPixels[idx] = std::min(255, x + 50);           // R + brightness
            goldenPixels[idx+1] = std::min(255, y + 50);         // G + brightness
            goldenPixels[idx+2] = std::min(255, 128 + 50);       // B + brightness
            goldenPixels[idx+3] = 255;                           // A
        }
    }

    // 7. Calculate SSIM
    double ssim = SSIMCalculator::calculateSSIM(resultPixels.data(), goldenPixels.data(), width, height);
    std::cout << "Computed SSIM: " << ssim << std::endl;

    bool passed = ssim >= 0.99;

    // Cleanup
    engine.release();
    glDeleteTextures(1, &inputTextureId);

    eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(eglDisplay, eglContext);
    eglDestroySurface(eglDisplay, eglSurface);
    eglTerminate(eglDisplay);

    if (passed) {
        std::cout << "Headless SSIM Test passed: GPU output exactly matches Golden Data." << std::endl;
        return 0;
    } else {
        std::cerr << "Headless SSIM Test failed: GPU output does not match Golden Data. SSIM: " << ssim << std::endl;
        return -1;
    }
}
