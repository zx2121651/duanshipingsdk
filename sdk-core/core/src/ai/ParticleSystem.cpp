/**
 * ParticleSystem.cpp
 */

#include "../../include/ai/ParticleSystem.h"

#define LOG_TAG "ParticleSystem"
#include "../../include/Log.h"

#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

namespace sdk {
namespace video {
namespace ai {

// ---------------------------------------------------------------------------
ParticleSystem::ParticleSystem()
    : m_rng(std::random_device{}())
{
    applyPreset(Preset::SNOW);
}

// ---------------------------------------------------------------------------
float ParticleSystem::randF(float lo, float hi) {
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(m_rng);
}

// ---------------------------------------------------------------------------
uint32_t ParticleSystem::lerpColor(uint32_t a, uint32_t b, float t) {
    auto lerp8 = [&](uint32_t ca, uint32_t cb, int shift) -> uint32_t {
        float va = (float)((ca >> shift) & 0xFF);
        float vb = (float)((cb >> shift) & 0xFF);
        return (uint32_t)(va + (vb - va) * t) & 0xFF;
    };
    return (lerp8(a,b,24)<<24)|(lerp8(a,b,16)<<16)|(lerp8(a,b,8)<<8)|lerp8(a,b,0);
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------
void ParticleSystem::applyPreset(Preset p) {
    m_preset = p;
    switch (p) {
    case Preset::SNOW:
        m_config.emitRate   = 15.f;
        m_config.spreadX    = 0.5f;
        m_config.spreadY    = 0.01f;
        m_config.speed      = 0.08f;
        m_config.speedVar   = 0.03f;
        m_config.dirAngle   = -90.f;
        m_config.dirSpread  = 20.f;
        m_config.gravityX   = 0.02f;
        m_config.gravityY   = 0.04f;
        m_config.life       = 6.f;
        m_config.lifeVar    = 2.f;
        m_config.sizeStart  = 0.012f;
        m_config.sizeEnd    = 0.008f;
        m_config.colorStart = 0xFFEEEEFFu;
        m_config.colorEnd   = 0x00EEEEFFu;
        m_config.alphaStart = 0.9f;
        m_config.alphaEnd   = 0.f;
        m_ex = 0.5f; m_ey = 0.f;
        break;

    case Preset::FIREWORKS:
        m_config.emitRate   = 0.f;   // burst-only
        m_config.speed      = 0.6f;
        m_config.speedVar   = 0.3f;
        m_config.dirAngle   = 0.f;
        m_config.dirSpread  = 180.f;
        m_config.gravityX   = 0.f;
        m_config.gravityY   = 0.3f;
        m_config.life       = 1.8f;
        m_config.lifeVar    = 0.5f;
        m_config.sizeStart  = 0.015f;
        m_config.sizeEnd    = 0.002f;
        m_config.colorStart = 0xFFFF8800u;
        m_config.colorEnd   = 0x00FF0000u;
        m_config.alphaStart = 1.f;
        m_config.alphaEnd   = 0.f;
        break;

    case Preset::HEARTS:
        m_config.emitRate   = 8.f;
        m_config.spreadX    = 0.04f;
        m_config.spreadY    = 0.02f;
        m_config.speed      = 0.18f;
        m_config.speedVar   = 0.05f;
        m_config.dirAngle   = -80.f;
        m_config.dirSpread  = 25.f;
        m_config.gravityX   = 0.f;
        m_config.gravityY   = -0.02f; // slight float up
        m_config.life       = 3.f;
        m_config.lifeVar    = 1.f;
        m_config.sizeStart  = 0.04f;
        m_config.sizeEnd    = 0.01f;
        m_config.colorStart = 0xFFFF2266u;
        m_config.colorEnd   = 0x00FF88AAu;
        m_config.alphaStart = 1.f;
        m_config.alphaEnd   = 0.f;
        m_config.rotSpeedMin= -0.5f;
        m_config.rotSpeedMax=  0.5f;
        break;

    case Preset::BUBBLES:
        m_config.emitRate   = 5.f;
        m_config.spreadX    = 0.3f;
        m_config.spreadY    = 0.02f;
        m_config.speed      = 0.1f;
        m_config.speedVar   = 0.04f;
        m_config.dirAngle   = -85.f;
        m_config.dirSpread  = 15.f;
        m_config.gravityX   = 0.f;
        m_config.gravityY   = -0.05f;
        m_config.life       = 5.f;
        m_config.lifeVar    = 2.f;
        m_config.sizeStart  = 0.03f;
        m_config.sizeEnd    = 0.04f;
        m_config.colorStart = 0x8088CCFFu;
        m_config.colorEnd   = 0x2088FFFFu;
        m_config.alphaStart = 0.6f;
        m_config.alphaEnd   = 0.f;
        m_ex = 0.5f; m_ey = 1.0f;
        break;

    case Preset::FIRE:
        m_config.emitRate   = 40.f;
        m_config.spreadX    = 0.04f;
        m_config.spreadY    = 0.01f;
        m_config.speed      = 0.25f;
        m_config.speedVar   = 0.1f;
        m_config.dirAngle   = -90.f;
        m_config.dirSpread  = 20.f;
        m_config.gravityX   = 0.f;
        m_config.gravityY   = -0.1f;
        m_config.life       = 1.2f;
        m_config.lifeVar    = 0.4f;
        m_config.sizeStart  = 0.025f;
        m_config.sizeEnd    = 0.005f;
        m_config.colorStart = 0xFFFF6600u;
        m_config.colorEnd   = 0x00880000u;
        m_config.alphaStart = 0.9f;
        m_config.alphaEnd   = 0.f;
        break;

    case Preset::SPARKLE:
        m_config.emitRate   = 30.f;
        m_config.spreadX    = 0.02f;
        m_config.spreadY    = 0.02f;
        m_config.speed      = 0.2f;
        m_config.speedVar   = 0.15f;
        m_config.dirAngle   = 0.f;
        m_config.dirSpread  = 180.f;
        m_config.gravityX   = 0.f;
        m_config.gravityY   = 0.05f;
        m_config.life       = 1.f;
        m_config.lifeVar    = 0.3f;
        m_config.sizeStart  = 0.018f;
        m_config.sizeEnd    = 0.f;
        m_config.colorStart = 0xFFFFDD44u;
        m_config.colorEnd   = 0x00FFAA00u;
        m_config.alphaStart = 1.f;
        m_config.alphaEnd   = 0.f;
        m_config.rotSpeedMin= -3.f;
        m_config.rotSpeedMax=  3.f;
        break;

    default: break;
    }
}

void ParticleSystem::setPreset(Preset p) {
    applyPreset(p);
}

const char* ParticleSystem::presetName(Preset p) {
    switch (p) {
    case Preset::SNOW:      return "雪花";
    case Preset::FIREWORKS: return "烟花";
    case Preset::HEARTS:    return "爱心";
    case Preset::BUBBLES:   return "泡泡";
    case Preset::FIRE:      return "火焰";
    case Preset::SPARKLE:   return "闪光";
    case Preset::CUSTOM:    return "自定义";
    default: return "";
    }
}

// ---------------------------------------------------------------------------
// spawnParticle
// ---------------------------------------------------------------------------
Particle ParticleSystem::spawnParticle() const {
    // Need non-const rng — workaround via const_cast for const method
    auto* self = const_cast<ParticleSystem*>(this);
    const EmitterConfig& c = m_config;

    Particle p;
    p.x = m_ex + self->randF(-c.spreadX, c.spreadX);
    p.y = m_ey + self->randF(-c.spreadY, c.spreadY);

    float speed = std::max(0.f, c.speed + self->randF(-c.speedVar, c.speedVar));
    float angleRad = (c.dirAngle + self->randF(-c.dirSpread, c.dirSpread))
                     * (float)M_PI / 180.f;
    p.vx = speed * std::cos(angleRad);
    p.vy = speed * std::sin(angleRad);
    p.ax = c.gravityX;
    p.ay = c.gravityY;

    p.maxLife = p.life = std::max(0.1f, c.life + self->randF(-c.lifeVar, c.lifeVar));
    p.size      = c.sizeStart;
    p.sizeDelta = (c.sizeEnd - c.sizeStart) / p.maxLife;
    p.color     = c.colorStart;
    p.alpha     = c.alphaStart;
    p.alphaFade = (c.alphaEnd - c.alphaStart) / p.maxLife;
    p.rotation  = self->randF(0.f, 6.28f);
    p.rotSpeed  = self->randF(c.rotSpeedMin, c.rotSpeedMax);
    p.active    = true;
    return p;
}

// ---------------------------------------------------------------------------
// burst()
// ---------------------------------------------------------------------------
void ParticleSystem::burst(float x, float y, int count) {
    float savedEx = m_ex, savedEy = m_ey;
    m_ex = x; m_ey = y;

    for (int i = 0; i < count; ++i) {
        // Find free slot or expand
        bool placed = false;
        for (auto& p : m_particles) {
            if (!p.active) {
                p = spawnParticle();
                placed = true;
                ++m_activeCount;
                break;
            }
        }
        if (!placed && (int)m_particles.size() < m_maxParticles) {
            m_particles.push_back(spawnParticle());
            ++m_activeCount;
        }
    }
    m_ex = savedEx; m_ey = savedEy;
}

// ---------------------------------------------------------------------------
// update()
// ---------------------------------------------------------------------------
void ParticleSystem::update(float dt) {
    float scaledDt = dt * m_timeScale;
    if (scaledDt <= 0.f) return;

    const EmitterConfig& c = m_config;

    // Emit new particles
    if (c.emitRate > 0.f) {
        m_emitAccum += c.emitRate * scaledDt;
        int toEmit = (int)m_emitAccum;
        m_emitAccum -= toEmit;

        for (int i = 0; i < toEmit; ++i) {
            if (m_activeCount >= m_maxParticles) break;
            bool placed = false;
            for (auto& p : m_particles) {
                if (!p.active) { p = spawnParticle(); ++m_activeCount; placed = true; break; }
            }
            if (!placed && (int)m_particles.size() < m_maxParticles) {
                m_particles.push_back(spawnParticle());
                ++m_activeCount;
            }
        }
    }

    // Update existing particles
    m_activeCount = 0;
    for (auto& p : m_particles) {
        if (!p.active) continue;
        p.life -= scaledDt;
        if (p.life <= 0.f) { p.active = false; continue; }

        p.vx += p.ax * scaledDt;
        p.vy += p.ay * scaledDt;
        p.x  += p.vx * scaledDt;
        p.y  += p.vy * scaledDt;
        p.size = std::max(0.f, p.size + p.sizeDelta * scaledDt);
        p.alpha = std::max(0.f, p.alpha + p.alphaFade * scaledDt);
        p.rotation += p.rotSpeed * scaledDt;

        // Color lerp based on remaining life
        float t = 1.f - (p.life / p.maxLife);
        p.color = lerpColor(c.colorStart, c.colorEnd, t);

        ++m_activeCount;
    }
}

// ---------------------------------------------------------------------------
void ParticleSystem::reset() {
    m_particles.clear();
    m_emitAccum  = 0.f;
    m_activeCount = 0;
}

} // namespace ai
} // namespace video
} // namespace sdk
