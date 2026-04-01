#import "FilterEngineWrapper.h"
#import <memory>
#include "../../core/include/FilterEngine.h"
#include "../../core/include/Filters.h"

using namespace sdk::video;

@interface FilterEngineWrapper () {
    std::shared_ptr<FilterEngine> _engine;
    EAGLContext *_context;
    CVOpenGLESTextureCacheRef _textureCache;
    CVPixelBufferPoolRef _pixelBufferPool;
}
@end

@implementation FilterEngineWrapper

- (instancetype)init {
    self = [super init];
    if (self) {
        _engine = std::make_shared<FilterEngine>();
        _textureCache = NULL;
        _pixelBufferPool = NULL;
    }
    return self;
}

- (void)initializeWithContext:(EAGLContext *)context {
    _context = context;
    [EAGLContext setCurrentContext:_context];

    CVReturn err = CVOpenGLESTextureCacheCreate(kCFAllocatorDefault, NULL, _context, NULL, &_textureCache);
    if (err) {
        NSLog(@"Error at CVOpenGLESTextureCacheCreate %d", err);
        return;
    }

    _engine->initialize();
}

- (CVPixelBufferRef)processFrame:(CVPixelBufferRef)pixelBuffer {
    if (!_engine || !_textureCache || !_context) return NULL;

    [EAGLContext setCurrentContext:_context];

    int width = (int)CVPixelBufferGetWidth(pixelBuffer);
    int height = (int)CVPixelBufferGetHeight(pixelBuffer);

    // 1. Map input CVPixelBuffer to OpenGL texture
    CVOpenGLESTextureRef inTextureRef = NULL;
    CVReturn err = CVOpenGLESTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                                                _textureCache,
                                                                pixelBuffer,
                                                                NULL,
                                                                GL_TEXTURE_2D,
                                                                GL_RGBA,
                                                                width,
                                                                height,
                                                                GL_BGRA,
                                                                GL_UNSIGNED_BYTE,
                                                                0,
                                                                &inTextureRef);
    if (err || !inTextureRef) {
        NSLog(@"Error creating input texture %d", err);
        return NULL;
    }

    GLuint inTextureId = CVOpenGLESTextureGetName(inTextureRef);
    Texture inputTexture = {inTextureId, width, height};

    // 2. Process frame with C++ engine (renders into FBO from FrameBufferPool)
    Texture outputTextureInfo = _engine->processFrame(inputTexture, width, height);

    // 3. To output a CVPixelBuffer on the fast path, we create a new CVPixelBuffer
    //    and a new CVOpenGLESTextureRef linked to it, bind it to an FBO, and draw the outputTextureInfo.id into it.
    //    We can't directly wrap an existing arbitrary OpenGL texture created by C++ into a CVPixelBuffer.

    if (!_pixelBufferPool) {
        NSDictionary *pixelBufferAttributes = @{
            (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
            (id)kCVPixelBufferWidthKey: @(width),
            (id)kCVPixelBufferHeightKey: @(height),
            (id)kCVPixelBufferIOSurfacePropertiesKey: @{}
        };
        CVPixelBufferPoolCreate(kCFAllocatorDefault, NULL, (__bridge CFDictionaryRef)pixelBufferAttributes, &_pixelBufferPool);
    }

    CVPixelBufferRef outputPixelBuffer = NULL;
    err = CVPixelBufferPoolCreatePixelBuffer(kCFAllocatorDefault, _pixelBufferPool, &outputPixelBuffer);
    if (err || !outputPixelBuffer) {
        CFRelease(inTextureRef);
        NSLog(@"Error creating output pixel buffer %d", err);
        return NULL;
    }

    CVOpenGLESTextureRef outTextureRef = NULL;
    err = CVOpenGLESTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                                       _textureCache,
                                                       outputPixelBuffer,
                                                       NULL,
                                                       GL_TEXTURE_2D,
                                                       GL_RGBA,
                                                       width,
                                                       height,
                                                       GL_BGRA,
                                                       GL_UNSIGNED_BYTE,
                                                       0,
                                                       &outTextureRef);
    if (err || !outTextureRef) {
        CVPixelBufferRelease(outputPixelBuffer);
        CFRelease(inTextureRef);
        NSLog(@"Error creating output texture from pixel buffer %d", err);
        return NULL;
    }

    GLuint outTextureId = CVOpenGLESTextureGetName(outTextureRef);

    // Fast path copy: Bind the output CVPixelBuffer's texture to an FBO, and blit/draw the C++ engine's output texture onto it.
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outTextureId, 0);

    // For simplicity here we assume the FBO blit extension is available (glBlitFramebuffer)
    // or we just readpixels. Let's use a full screen quad copy to be safe.

    // Compile simple passthrough program once (in real code, cache this)
    static GLuint copyProgram = 0;
    static GLuint posAttr, texAttr, texUniform;
    if (copyProgram == 0) {
        const char *vSrc = "#version 300 es\n layout(location=0) in vec4 pos; layout(location=1) in vec2 texC; out vec2 vTexC; void main(){gl_Position=pos;vTexC=texC;}";
        const char *fSrc = "#version 300 es\n precision mediump float; in vec2 vTexC; out vec4 outCol; uniform sampler2D tex; void main(){outCol=texture(tex,vTexC);}";

        auto compileShader = [](GLenum type, const char* src) -> GLuint {
            GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &src, NULL);
            glCompileShader(shader);
            return shader;
        };
        GLuint vs = compileShader(GL_VERTEX_SHADER, vSrc);
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, fSrc);
        copyProgram = glCreateProgram();
        glAttachShader(copyProgram, vs);
        glAttachShader(copyProgram, fs);
        glLinkProgram(copyProgram);
        posAttr = glGetAttribLocation(copyProgram, "pos");
        texAttr = glGetAttribLocation(copyProgram, "texC");
        texUniform = glGetUniformLocation(copyProgram, "tex");
    }

    glViewport(0, 0, width, height);
    glUseProgram(copyProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, outputTextureInfo.id);
    glUniform1i(texUniform, 0);

    static const float vertices[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    static const float texCoords[] = { 0, 0, 1, 0, 0, 1, 1, 1 };

    glEnableVertexAttribArray(posAttr);
    glVertexAttribPointer(posAttr, 2, GL_FLOAT, GL_FALSE, 0, vertices);
    glEnableVertexAttribArray(texAttr);
    glVertexAttribPointer(texAttr, 2, GL_FLOAT, GL_FALSE, 0, texCoords);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(posAttr);
    glDisableVertexAttribArray(texAttr);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);

    // Cleanup
    CFRelease(inTextureRef);
    CFRelease(outTextureRef);
    CVOpenGLESTextureCacheFlush(_textureCache, 0);

    return outputPixelBuffer; // Caller is responsible for releasing this
}

- (void)updateParameterFloat:(NSString *)key value:(float)value {
    if (_engine) {
        std::string cppKey([key UTF8String]);
        _engine->updateParameter(cppKey, std::any(value));
    }
}

- (void)updateParameterInt:(NSString *)key value:(int)value {
    if (_engine) {
        std::string cppKey([key UTF8String]);
        _engine->updateParameter(cppKey, std::any(value));
    }
}

- (void)addFilter:(FilterType)type {
    if (_engine) {
        FilterPtr filter;
        switch(type) {
            case FilterTypeBrightness: filter = std::make_shared<BrightnessFilter>(); break;
            case FilterTypeGaussianBlur: filter = std::make_shared<GaussianBlurFilter>(&(self->engine->m_frameBufferPool)); break;
            case FilterTypeLookup: filter = std::make_shared<LookupFilter>(); break;
        }
        if (filter) {
            _engine->addFilter(filter);
        }
    }
}

- (void)removeAllFilters {
    if (_engine) {
        _engine->removeAllFilters();
    }
}

- (void)releaseResources {
    if (_engine) {
        [EAGLContext setCurrentContext:_context];
        _engine->release();
    }
    if (_textureCache) {
        CFRelease(_textureCache);
        _textureCache = NULL;
    }
    if (_pixelBufferPool) {
        CFRelease(_pixelBufferPool);
        _pixelBufferPool = NULL;
    }
}

- (void)dealloc {
    [self releaseResources];
}

@end
