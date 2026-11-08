#pragma once

// Runs a NIF's particle system (NifParticleCore::System) in the renderer's own clock: spawns from its emitter, moves
// particles under its gravity and drag, ages them out, and reports each live one with its colour, size, rotation and
// flipbook frame over its life. Positions are in the space of the frames the caller gives (the model's world).
// Engine-free and deterministic for a seed; tested by NifParticleCoreTest.cpp.

#include "NifParticleCore.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Phantasm::ParticleSimCore
{
    namespace NPC = NifParticleCore;

    /// A node's world: its origin, its unit axes and its uniform scale.
    struct Frame
    {
        float pos[3]{ 0, 0, 0 };
        float x[3]{ 1, 0, 0 }, y[3]{ 0, 1, 0 }, z[3]{ 0, 0, 1 };
        float scale = 1.0f;
    };

    /// Where the system's nodes stand this step: its emitter's, and one per gravity and drag modifier (in their
    /// order; a missing one counts as the emitter's).
    struct Env
    {
        Frame              emitter;
        std::vector<Frame> gravity, drag;
        std::uint32_t      maxParticles = 256;
    };

    /// One live particle as drawn.
    struct Drawn
    {
        float pos[3];
        float halfSize;       // in the frames' units: the emitter's scale is already in it
        float color[4];
        float angle;          // radians
        std::uint32_t frame;  // flipbook frame
    };

    inline constexpr float kMaxEmitStep = 0.25f;   // a stall never emits more than a quarter second's worth

    class Sim
    {
    public:
        Sim(NPC::System def, std::uint32_t seed) : m_def(std::move(def)), m_rng(seed ? seed : 0x9E3779B9u) {}

        const NPC::System& Def() const noexcept { return m_def; }
        std::size_t        Live() const noexcept { return m_parts.size(); }

        void Step(float dt, const Env& env)
        {
            if (!(dt > 0.0f)) return;
            m_time += dt;
            // Ageing out.
            for (std::size_t i = 0; i < m_parts.size();) {
                auto& q = m_parts[i];
                q.age += dt;
                if (q.age >= q.life) {
                    q = m_parts.back();
                    m_parts.pop_back();
                } else {
                    ++i;
                }
            }
            // Forces, then motion and spin.
            for (auto& q : m_parts) {
                for (std::size_t g = 0; g < m_def.gravity.size(); ++g) {
                    const auto&  gd = m_def.gravity[g];
                    const Frame& f  = g < env.gravity.size() ? env.gravity[g] : env.emitter;
                    float        a[3]{};
                    if (gd.forceType == 1) {
                        float       d[3] = { f.pos[0] - q.p[0], f.pos[1] - q.p[1], f.pos[2] - q.p[2] };
                        const float len  = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                        if (len > 1.0e-3f) {
                            const float k = gd.strength * (gd.decay > 0.0f ? std::exp(-gd.decay * len) : 1.0f) / len;
                            for (int c = 0; c < 3; ++c) a[c] = d[c] * k;
                        }
                    } else {
                        float axis[3];
                        Axis(gd.axis.data(), gd.worldAligned ? nullptr : &f, axis);
                        for (int c = 0; c < 3; ++c) a[c] = axis[c] * gd.strength;
                    }
                    if (gd.turbulence > 0.0f) {
                        for (int c = 0; c < 3; ++c) a[c] += Sym() * gd.turbulence * std::fabs(gd.strength);
                    }
                    for (int c = 0; c < 3; ++c) q.v[c] += a[c] * dt;
                }
                for (std::size_t d = 0; d < m_def.drag.size(); ++d) {
                    const auto&  dd = m_def.drag[d];
                    const Frame& f  = d < env.drag.size() ? env.drag[d] : env.emitter;
                    float        axis[3];
                    Axis(dd.axis.data(), &f, axis);
                    const float along = q.v[0] * axis[0] + q.v[1] * axis[1] + q.v[2] * axis[2];
                    // The percentage is lost per 1/30 s, the rate the content was made at.
                    const float keep = std::pow(std::clamp(1.0f - dd.percentage, 0.0f, 1.0f), dt * 30.0f);
                    for (int c = 0; c < 3; ++c) q.v[c] -= axis[c] * along * (1.0f - keep);
                }
                for (int c = 0; c < 3; ++c) q.p[c] += q.v[c] * dt;
                q.angle += q.spin * dt;
            }
            // Birth.
            const float t = m_def.emitTime.Scaled(m_time);
            if (!m_def.visible.Eval(t)) return;
            const float rate = std::max(0.0f, m_def.birthRate.Eval(t));
            m_carry          = std::min(m_carry + rate * std::min(dt, kMaxEmitStep), rate * kMaxEmitStep + 1.0f);
            while (m_carry >= 1.0f) {
                m_carry -= 1.0f;
                if (m_parts.size() >= env.maxParticles) break;
                Spawn(env);
            }
        }

        /// Every live particle as drawn this step.
        void Collect(std::vector<Drawn>& out) const
        {
            const auto frames = Frames();
            for (const auto& q : m_parts) {
                const float life = q.life > 1.0e-4f ? q.life : 1.0e-4f;
                const float t    = std::clamp(q.age / life, 0.0f, 1.0f);
                Drawn       d{};
                for (int c = 0; c < 3; ++c) d.pos[c] = q.p[c];
                d.halfSize = q.radius * ScaleAt(t);
                ColorAt(t, d.color);
                d.angle = q.angle;
                if (m_def.subTex.on && frames > 1) {
                    const float f = m_def.subTex.start + (m_def.subTex.end - m_def.subTex.start) * t;
                    d.frame       = static_cast<std::uint32_t>(std::clamp(f, 0.0f, static_cast<float>(frames - 1)));
                } else {
                    d.frame = frames > 1 ? q.frame % frames : 0;
                }
                out.push_back(d);
            }
        }

        /// Moves every live particle with its system's node from `from` to `to`, as a local-space system's particles
        /// go with it (System::worldSpace false): position, velocity and size keep their place in the node's frame.
        void Carry(const Frame& from, const Frame& to) noexcept
        {
            const float k = from.scale > 1.0e-6f ? to.scale / from.scale : 1.0f;
            if (!std::isfinite(k)) return;
            const auto local = [&](const float* v, float out[3]) {   // into from's unit axes
                out[0] = v[0] * from.x[0] + v[1] * from.x[1] + v[2] * from.x[2];
                out[1] = v[0] * from.y[0] + v[1] * from.y[1] + v[2] * from.y[2];
                out[2] = v[0] * from.z[0] + v[1] * from.z[1] + v[2] * from.z[2];
            };
            const auto world = [&](const float* l, float out[3]) {   // out of to's unit axes
                for (int c = 0; c < 3; ++c) out[c] = (to.x[c] * l[0] + to.y[c] * l[1] + to.z[c] * l[2]) * k;
            };
            for (auto& q : m_parts) {
                float d[3] = { q.p[0] - from.pos[0], q.p[1] - from.pos[1], q.p[2] - from.pos[2] };
                float l[3], w[3];
                local(d, l);
                world(l, w);
                for (int c = 0; c < 3; ++c) q.p[c] = to.pos[c] + w[c];
                local(q.v, l);
                world(l, q.v);
                q.radius *= k;
            }
        }

        /// The size multiplier at t (0-1 of the life): the scale curve, evenly spaced, or 1.
        float ScaleAt(float t) const noexcept
        {
            const auto& s = m_def.scales;
            if (s.empty()) return 1.0f;
            if (s.size() == 1) return s[0];
            const float       x = std::clamp(t, 0.0f, 1.0f) * static_cast<float>(s.size() - 1);
            const std::size_t i = std::min(static_cast<std::size_t>(x), s.size() - 2);
            return s[i] + (s[i + 1] - s[i]) * (x - static_cast<float>(i));
        }

        /// The colour at t: the colour modifier's keys and fades, or the emitter's colour.
        void ColorAt(float t, float out[4]) const noexcept
        {
            const auto& cf = m_def.color;
            if (!cf.on) {
                for (int c = 0; c < 4; ++c) out[c] = m_def.emitter.color[c];
                return;
            }
            const auto lerp = [&](const std::array<float, 4>& a, const std::array<float, 4>& b, float lo, float hi) {
                const float k = hi > lo ? std::clamp((t - lo) / (hi - lo), 0.0f, 1.0f) : 1.0f;
                for (int c = 0; c < 4; ++c) out[c] = a[c] + (b[c] - a[c]) * k;
            };
            if (t <= cf.c1End) lerp(cf.colors[0], cf.colors[0], 0, 1);
            else if (t < cf.c1Start) lerp(cf.colors[0], cf.colors[1], cf.c1End, cf.c1Start);
            else if (t <= cf.c2End) lerp(cf.colors[1], cf.colors[1], 0, 1);
            else if (t < cf.c2Start) lerp(cf.colors[1], cf.colors[2], cf.c2End, cf.c2Start);
            else lerp(cf.colors[2], cf.colors[2], 0, 1);
            float fade = 1.0f;
            if (cf.fadeIn > 0.0f && t < cf.fadeIn) fade = t / cf.fadeIn;
            else if (cf.fadeOut < 1.0f && t > cf.fadeOut) fade = (1.0f - t) / (1.0f - cf.fadeOut);
            out[3] = std::clamp(out[3] * fade, 0.0f, 1.0f);
        }

    private:
        struct Particle
        {
            float         p[3], v[3];
            float         age, life, radius, angle, spin;
            std::uint32_t frame;
        };

        float Rand() noexcept
        {
            m_rng ^= m_rng << 13;
            m_rng ^= m_rng >> 17;
            m_rng ^= m_rng << 5;
            return static_cast<float>(m_rng & 0xFFFFFF) / 16777216.0f;
        }
        float Sym() noexcept { return Rand() * 2.0f - 1.0f; }

        // The texture's flipbook frames (0 or 1: none).
        std::uint32_t Frames() const noexcept { return static_cast<std::uint32_t>(m_def.frames.size()); }

        // An axis in a frame's space (or as given, with none), unit length.
        static void Axis(const float* in, const Frame* f, float out[3]) noexcept
        {
            if (f) {
                for (int c = 0; c < 3; ++c) out[c] = f->x[c] * in[0] + f->y[c] * in[1] + f->z[c] * in[2];
            } else {
                for (int c = 0; c < 3; ++c) out[c] = in[c];
            }
            const float len = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
            if (len > 1.0e-6f)
                for (int c = 0; c < 3; ++c) out[c] /= len;
        }

        void Spawn(const Env& env)
        {
            const auto&  e = m_def.emitter;
            const Frame& f = env.emitter;
            float        l[3]{};
            switch (e.shape) {
            case NPC::EmitterShape::kBox:
                for (int c = 0; c < 3; ++c) l[c] = Sym() * e.dims[c] * 0.5f;
                break;
            case NPC::EmitterShape::kSphere: {
                float u[3] = { Sym(), Sym(), Sym() };
                Axis(u, nullptr, u);
                const float r = e.dims[0] * std::cbrt(Rand());
                for (int c = 0; c < 3; ++c) l[c] = u[c] * r;
                break;
            }
            case NPC::EmitterShape::kCylinder: {
                const float a = Rand() * 6.2831853f;
                const float r = e.dims[0] * std::sqrt(Rand());
                l[0]          = std::cos(a) * r;
                l[1]          = std::sin(a) * r;
                l[2]          = Sym() * e.dims[1] * 0.5f;
                break;
            }
            case NPC::EmitterShape::kMesh:
                break;   // never simulated (NifParticleCore::Unsupported::kMeshEmitter): at the frame's origin
            }
            Particle q{};
            for (int c = 0; c < 3; ++c) q.p[c] = f.pos[c] + (f.x[c] * l[0] + f.y[c] * l[1] + f.z[c] * l[2]) * f.scale;
            const float decl  = e.declination + e.declinationVar * Sym();
            const float plan  = e.planar + e.planarVar * Sym();
            const float ld[3] = { std::sin(decl) * std::cos(plan), std::sin(decl) * std::sin(plan), std::cos(decl) };
            const float speed = (e.speed + e.speedVar * Sym()) * f.scale;
            for (int c = 0; c < 3; ++c) q.v[c] = (f.x[c] * ld[0] + f.y[c] * ld[1] + f.z[c] * ld[2]) * speed;
            q.age    = 0.0f;
            q.life   = std::max(0.01f, e.life + e.lifeVar * Sym());
            q.radius = std::max(0.0f, e.radius + e.radiusVar * Sym()) * f.scale;
            const auto& ro = m_def.rotation;
            q.angle        = ro.on ? ro.angle + ro.angleVar * Sym() : 0.0f;
            q.spin         = ro.on ? (ro.speed + ro.speedVar * Sym()) * (ro.randomSign && Rand() < 0.5f ? -1.0f : 1.0f)
                                   : 0.0f;
            q.frame        = Frames() > 1 ? static_cast<std::uint32_t>(Rand() * static_cast<float>(Frames())) : 0;
            m_parts.push_back(q);
        }

        NPC::System           m_def;
        std::vector<Particle> m_parts;
        float                 m_time  = 0.0f;
        float                 m_carry = 0.0f;
        std::uint32_t         m_rng;
    };
}
