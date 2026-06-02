#pragma once
/**
 * ParticleSystem.h
 *
 * CPU 端粒子特效系统（烟花/雪花/火焰/爱心/泡泡等）。
 *
 * 架构：
 *   - 纯 C++ CPU 模拟，不依赖 GPU Compute Shader
 *   - 每帧 update() 更新粒子状态（位置/速度/生命值/颜色/大小）
 *   - 提供 getParticles() 供渲染层读取并绘制点精灵 / 四边形
 *   - 可与 AR 锚点结合（setEmitterPosition 跟随人脸/手部）
 *
 * 预设：
 *   SNOW         雪花飘落
 *   FIREWORKS    烟花绽放（爆发 + 尾迹）
 *   HEARTS       爱心漂浮
 *   BUBBLES      泡泡上升
 *   FIRE         火焰上升（暖色粒子 + 湍流）
 *   SPARKLE      金色闪光散射
 *   CUSTOM       自定义参数
 *
 * 用法：
 *   ParticleSystem ps;
 *   ps.setPreset(ParticleSystem::Preset::HEARTS);
 *   ps.setEmitterPosition(0.5f, 0.8f);   // 归一化屏幕坐标
 *   ps.setMaxParticles(200);
 *   // 每帧调用：
 *   ps.update(dtSeconds);
 *   for (auto& p : ps.getParticles()) {
 *       drawSprite(p.x, p.y, p.size, p.color, p.alpha);
 *   }
 */

#include <cstdint>
#include <vector>
#include <string>
#include <functional>
#include <random>

namespace sdk {
namespace video {
namespace ai {

// ---------------------------------------------------------------------------
// 单个粒子状态
// ---------------------------------------------------------------------------
struct Particle {
    float x = 0.f, y = 0.f;    ///< 位置（归一化屏幕坐标 [0,1]）
    float vx= 0.f, vy= 0.f;    ///< 速度（单位/秒）
    float ax= 0.f, ay= 0.f;    ///< 加速度
    float size    = 0.02f;      ///< 大小（归一化）
    float sizeDelta = 0.f;      ///< 每秒大小变化
    uint32_t color = 0xFFFFFFFFu; ///< ARGB
    float alpha   = 1.f;        ///< [0,1]，独立于 color.a
    float alphaFade = 0.f;      ///< 每秒 alpha 衰减
    float life    = 1.f;        ///< 剩余生命（秒）
    float maxLife = 1.f;
    float rotation= 0.f;        ///< 旋转角（弧度）
    float rotSpeed= 0.f;        ///< 旋转速度（rad/s）
    bool  active  = false;
};

// ---------------------------------------------------------------------------
// 粒子发射参数（自定义时使用）
// ---------------------------------------------------------------------------
struct EmitterConfig {
    // 发射速率
    float emitRate    = 20.f;   ///< 粒子/秒

    // 初始位置（归一化，以 emitter 为中心的随机范围）
    float spreadX     = 0.02f;
    float spreadY     = 0.02f;

    // 初始速度
    float speed       = 0.3f;   ///< 速度大小均值
    float speedVar    = 0.1f;   ///< 速度大小方差
    float dirAngle    = -90.f;  ///< 发射方向（角度，0=右，-90=上）
    float dirSpread   = 30.f;   ///< 方向随机扩散角度

    // 加速度
    float gravityX    = 0.f;
    float gravityY    = 0.15f;  ///< 重力（向下为正）

    // 生命
    float life        = 2.f;    ///< 均值（秒）
    float lifeVar     = 0.5f;

    // 外观
    float sizeStart   = 0.02f;
    float sizeEnd     = 0.005f;
    uint32_t colorStart = 0xFFFFFFFFu; ///< ARGB
    uint32_t colorEnd   = 0x00FFFFFFu; ///< fade to this color
    float alphaStart  = 1.f;
    float alphaEnd    = 0.f;

    // 旋转
    float rotSpeedMin = -2.f;
    float rotSpeedMax =  2.f;
};

// ---------------------------------------------------------------------------
// ParticleSystem
// ---------------------------------------------------------------------------
class ParticleSystem {
public:
    enum class Preset {
        SNOW      = 0,
        FIREWORKS = 1,
        HEARTS    = 2,
        BUBBLES   = 3,
        FIRE      = 4,
        SPARKLE   = 5,
        CUSTOM    = 6,
    };

    ParticleSystem();
    ~ParticleSystem() = default;

    // ── 配置 ──────────────────────────────────────────────────────────────

    void setPreset(Preset preset);
    Preset getPreset() const { return m_preset; }

    static const char* presetName(Preset p);

    /** 设置发射器位置（归一化屏幕坐标 [0,1]）。 */
    void setEmitterPosition(float x, float y) { m_ex = x; m_ey = y; }
    float getEmitterX() const { return m_ex; }
    float getEmitterY() const { return m_ey; }

    /** 粒子池上限，默认 500。 */
    void setMaxParticles(int n) { m_maxParticles = n; }

    /** 自定义发射参数（CUSTOM 预设时使用）。 */
    void setEmitterConfig(const EmitterConfig& cfg) { m_config = cfg; }
    const EmitterConfig& getEmitterConfig() const   { return m_config; }

    /** 系统时间缩放（1.0=正常，0.5=慢动作）。 */
    void setTimeScale(float s) { m_timeScale = s; }

    /** 全局透明度（叠加在每个粒子的 alpha 上）。 */
    void setGlobalAlpha(float a) { m_globalAlpha = a; }

    // ── 触发特效 ──────────────────────────────────────────────────────────

    /** 烟花：在指定位置触发一次爆发。 */
    void burst(float x, float y, int count = 80);

    // ── 模拟 ──────────────────────────────────────────────────────────────

    /**
     * 更新粒子状态。
     * @param dt  帧间隔（秒），建议传 deltaTimeSeconds
     */
    void update(float dt);

    /** 重置所有粒子。 */
    void reset();

    // ── 读取 ──────────────────────────────────────────────────────────────

    /** 当前活跃粒子列表（只读）。 */
    const std::vector<Particle>& getParticles() const { return m_particles; }

    /** 活跃粒子数。 */
    int activeCount() const { return m_activeCount; }

private:
    Preset       m_preset     = Preset::SNOW;
    EmitterConfig m_config;
    float        m_ex         = 0.5f;
    float        m_ey         = 0.5f;
    int          m_maxParticles = 500;
    float        m_timeScale  = 1.f;
    float        m_globalAlpha= 1.f;
    float        m_emitAccum  = 0.f;
    int          m_activeCount= 0;

    std::vector<Particle> m_particles;
    std::mt19937          m_rng;

    float randF(float lo, float hi);
    Particle spawnParticle() const;
    void applyPreset(Preset p);

    // Lerp helper for color
    static uint32_t lerpColor(uint32_t a, uint32_t b, float t);
};

} // namespace ai
} // namespace video
} // namespace sdk
