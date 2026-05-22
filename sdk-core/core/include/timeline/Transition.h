#pragma once
#include <string>
#include <unordered_map>
#include <stdexcept>

namespace sdk {
namespace video {
namespace timeline {

/**
 * @brief 转场描述符
 * 包含唯一名称和完整的 GLSL 片段着色器源码。
 * 着色器必须声明以下 uniform 和输入/输出：
 *   uniform sampler2D texBackground;   // 背景层纹理
 *   uniform sampler2D texForeground;   // 前景层纹理
 *   uniform float     progress;        // 转场进度 [0.0, 1.0]
 *   in  vec2 v_texCoord;
 *   out vec4 fragColor;
 */
struct TransitionDesc {
    std::string name;
    std::string fragmentGLSL;
};

/**
 * @brief 转场注册表（单例）
 *
 * 内置注册两种基础转场：
 *  - "crossfade"  淡入淡出
 *  - "wipe_left"  左移擦除
 *
 * 扩展方式：
 *   TransitionRegistry::getInstance().registerTransition("zoom_blur", glslSource);
 * 无需修改 Compositor 或 Clip 的任何核心代码。
 */
class TransitionRegistry {
public:
    static TransitionRegistry& getInstance() {
        static TransitionRegistry s_instance;
        return s_instance;
    }

    // Non-copyable
    TransitionRegistry(const TransitionRegistry&) = delete;
    TransitionRegistry& operator=(const TransitionRegistry&) = delete;

    /**
     * @brief 注册或覆盖一个转场
     * @param name  唯一标识符（建议 snake_case）
     * @param glsl  完整的 GLSL ES 300 Fragment Shader 源码
     */
    void registerTransition(const std::string& name, const std::string& glsl) {
        m_registry[name] = TransitionDesc{name, glsl};
    }

    /**
     * @brief 查询转场描述符
     * @return 指向描述符的指针，未找到时返回 nullptr
     */
    const TransitionDesc* getTransition(const std::string& name) const {
        auto it = m_registry.find(name);
        return (it != m_registry.end()) ? &it->second : nullptr;
    }

    bool hasTransition(const std::string& name) const {
        return m_registry.count(name) > 0;
    }

private:
    TransitionRegistry() {
        registerBuiltins();
    }

    void registerBuiltins();

    std::unordered_map<std::string, TransitionDesc> m_registry;
};

// ---------------------------------------------------------------------------
// 内置转场 GLSL 实现（定义在 Transition.h 中，避免单独 .cpp）
// ---------------------------------------------------------------------------
inline void TransitionRegistry::registerBuiltins() {
    // --- crossfade：线性透明度过渡 ---
    registerTransition("crossfade", R"(#version 300 es
precision highp float;
in  vec2 v_texCoord;
uniform sampler2D texBackground;
uniform sampler2D texForeground;
uniform float     progress;
out vec4 fragColor;
void main() {
    vec4 bg = texture(texBackground, v_texCoord);
    vec4 fg = texture(texForeground, v_texCoord);
    fragColor = mix(bg, fg, progress);
}
)");

    // --- wipe_left：前景从右边缘向左擦入 ---
    registerTransition("wipe_left", R"(#version 300 es
precision highp float;
in  vec2 v_texCoord;
uniform sampler2D texBackground;
uniform sampler2D texForeground;
uniform float     progress;
out vec4 fragColor;
void main() {
    vec4 bg = texture(texBackground, v_texCoord);
    vec4 fg = texture(texForeground, v_texCoord);
    fragColor = (v_texCoord.x > (1.0 - progress)) ? fg : bg;
}
)");

    // --- wipe_right：前景从左边缘向右擦入 ---
    registerTransition("wipe_right", R"(#version 300 es
precision highp float;
in  vec2 v_texCoord;
uniform sampler2D texBackground;
uniform sampler2D texForeground;
uniform float     progress;
out vec4 fragColor;
void main() {
    vec4 bg = texture(texBackground, v_texCoord);
    vec4 fg = texture(texForeground, v_texCoord);
    fragColor = (v_texCoord.x < progress) ? fg : bg;
}
)");

    // --- wipe_up：前景从底部向上擦入 ---
    registerTransition("wipe_up", R"(#version 300 es
precision highp float;
in  vec2 v_texCoord;
uniform sampler2D texBackground;
uniform sampler2D texForeground;
uniform float     progress;
out vec4 fragColor;
void main() {
    vec4 bg = texture(texBackground, v_texCoord);
    vec4 fg = texture(texForeground, v_texCoord);
    fragColor = (v_texCoord.y < progress) ? fg : bg;
}
)");

    // --- wipe_down：前景从顶部向下擦入 ---
    registerTransition("wipe_down", R"(#version 300 es
precision highp float;
in  vec2 v_texCoord;
uniform sampler2D texBackground;
uniform sampler2D texForeground;
uniform float     progress;
out vec4 fragColor;
void main() {
    vec4 bg = texture(texBackground, v_texCoord);
    vec4 fg = texture(texForeground, v_texCoord);
    fragColor = ((1.0 - v_texCoord.y) < progress) ? fg : bg;
}
)");

    // --- slide_left：前景从右侧平移滑入，背景向左滑出 ---
    registerTransition("slide_left", R"(#version 300 es
precision highp float;
in  vec2 v_texCoord;
uniform sampler2D texBackground;
uniform sampler2D texForeground;
uniform float     progress;
out vec4 fragColor;
void main() {
    vec2 fgCoord = vec2(v_texCoord.x - (1.0 - progress), v_texCoord.y);
    vec2 bgCoord = vec2(v_texCoord.x + progress,          v_texCoord.y);
    bool showFg  = fgCoord.x >= 0.0 && fgCoord.x <= 1.0;
    fragColor = showFg ? texture(texForeground, fgCoord)
                       : texture(texBackground, bgCoord);
}
)");

    // --- slide_right：前景从左侧平移滑入，背景向右滑出 ---
    registerTransition("slide_right", R"(#version 300 es
precision highp float;
in  vec2 v_texCoord;
uniform sampler2D texBackground;
uniform sampler2D texForeground;
uniform float     progress;
out vec4 fragColor;
void main() {
    vec2 fgCoord = vec2(v_texCoord.x + (1.0 - progress), v_texCoord.y);
    vec2 bgCoord = vec2(v_texCoord.x - progress,          v_texCoord.y);
    bool showFg  = fgCoord.x >= 0.0 && fgCoord.x <= 1.0;
    fragColor = showFg ? texture(texForeground, fgCoord)
                       : texture(texBackground, bgCoord);
}
)");

    // --- zoom_in：前景从画面中心缩放放大显示 ---
    registerTransition("zoom_in", R"(#version 300 es
precision highp float;
in  vec2 v_texCoord;
uniform sampler2D texBackground;
uniform sampler2D texForeground;
uniform float     progress;
out vec4 fragColor;
void main() {
    float scale    = mix(0.3, 1.0, progress);
    vec2  centered = (v_texCoord - 0.5) / scale + 0.5;
    bool  inBounds = centered.x >= 0.0 && centered.x <= 1.0 &&
                     centered.y >= 0.0 && centered.y <= 1.0;
    vec4 fg = inBounds ? texture(texForeground, centered) : vec4(0.0);
    vec4 bg = texture(texBackground, v_texCoord);
    fragColor = mix(bg, fg, progress * float(inBounds));
}
)");

    // --- fade_black：先淡出到黑，再从黑淡入前景 ---
    registerTransition("fade_black", R"(#version 300 es
precision highp float;
in  vec2 v_texCoord;
uniform sampler2D texBackground;
uniform sampler2D texForeground;
uniform float     progress;
out vec4 fragColor;
void main() {
    float t = progress * 2.0;
    vec4 black = vec4(0.0, 0.0, 0.0, 1.0);
    if (t < 1.0) {
        fragColor = mix(texture(texBackground, v_texCoord), black, t);
    } else {
        fragColor = mix(black, texture(texForeground, v_texCoord), t - 1.0);
    }
}
)");

    // --- flash：白色闪光过渡 ---
    registerTransition("flash", R"(#version 300 es
precision highp float;
in  vec2 v_texCoord;
uniform sampler2D texBackground;
uniform sampler2D texForeground;
uniform float     progress;
out vec4 fragColor;
void main() {
    float peak    = 1.0 - abs(progress - 0.5) * 2.0;
    vec4  blended = mix(texture(texBackground, v_texCoord),
                        texture(texForeground, v_texCoord), progress);
    fragColor = mix(blended, vec4(1.0), peak * 0.85);
}
)");
}

} // namespace timeline
} // namespace video
} // namespace sdk
