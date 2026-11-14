#pragma once

// Phantasm - NeuralStillCore: the pixel side of running NVIDIA DLSS NR on an actor still, engine-free
// (native/build.ps1 -Tests). NeuralStills.cpp feeds a shot through it around NR's own call.
//
// A Cutout or Portrait shot is colour premultiplied by alpha in linear light, sRGB-encoded, on transparent black
// (MannequinRenderer::PortraitShot). NR takes an opaque display picture, so the shot is laid over a plain ground, and
// the ground is taken off its answer again: an unchanged answer gives the shot back, and a pixel's colour never
// exceeds its coverage, so what NR does to the ground cannot leak past the figure's edge.
//
// Author: Severause

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Phantasm::NeuralStillCore
{
    /// The ground under the figure while NR runs: a dark grey, in linear light (sRGB about 0.30).
    inline constexpr float kGroundLinear = 0.0732f;
    /// Depth NR is given for the figure and for the ground (0 near, 1 far): the shot keeps no depth of its own.
    inline constexpr float kFigureDepth = 0.35f;
    inline constexpr float kGroundDepth = 1.0f;

    inline float SrgbToLinear(float c) noexcept
    {
        return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }

    inline float LinearToSrgb(float c) noexcept
    {
        c = std::clamp(c, 0.0f, 1.0f);
        return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    }

    inline const std::array<float, 256>& DecodeTable() noexcept
    {
        static const std::array<float, 256> t = [] {
            std::array<float, 256> a{};
            for (int i = 0; i < 256; ++i) a[static_cast<std::size_t>(i)] = SrgbToLinear(i / 255.0f);
            return a;
        }();
        return t;
    }

    inline std::uint8_t EncodeByte(float linear) noexcept
    {
        return static_cast<std::uint8_t>(std::lround(LinearToSrgb(linear) * 255.0f));
    }

    /// NR's input from a shot: the opaque picture (RGBA8 sRGB, alpha 255) and its depth. Both sized width x height.
    inline void ToInput(const std::vector<std::uint8_t>& shot, std::uint32_t width, std::uint32_t height,
                        std::vector<std::uint8_t>& picture, std::vector<float>& depth)
    {
        const std::size_t n  = static_cast<std::size_t>(width) * height;
        const auto&       lut = DecodeTable();
        picture.assign(n * 4, 255);
        depth.assign(n, kGroundDepth);
        if (shot.size() < n * 4) return;
        for (std::size_t i = 0; i < n; ++i) {
            const float a = shot[i * 4 + 3] / 255.0f;
            for (int c = 0; c < 3; ++c) {
                picture[i * 4 + c] = EncodeByte(lut[shot[i * 4 + c]] + kGroundLinear * (1.0f - a));
            }
            depth[i] = a >= 0.5f ? kFigureDepth : kGroundDepth;
        }
    }

    /// The shot again from NR's answer (RGBA8 sRGB, opaque): the ground taken off, each channel kept within the pixel's
    /// coverage, the shot's own alpha kept. A pixel the shot does not cover stays transparent black.
    inline void FromOutput(std::vector<std::uint8_t>& shot, std::uint32_t width, std::uint32_t height,
                           const std::vector<std::uint8_t>& answer)
    {
        const std::size_t n   = static_cast<std::size_t>(width) * height;
        const auto&       lut = DecodeTable();
        if (shot.size() < n * 4 || answer.size() < n * 4) return;
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint8_t alpha = shot[i * 4 + 3];
            if (alpha == 0) continue;
            const float a = alpha / 255.0f;
            for (int c = 0; c < 3; ++c) {
                const float premul = std::clamp(lut[answer[i * 4 + c]] - kGroundLinear * (1.0f - a), 0.0f, a);
                shot[i * 4 + c]    = EncodeByte(premul);
            }
        }
    }

    /// Phantasm.ini [NeuralRendering]'s effect settings, clamped by the functions below.
    struct Settings
    {
        int   model     = 1;      // NR's style: 0, 1 or 2 (NVIDIA's models A, B, C)
        float intensity = 1.0f;   // 0..1
        float tone      = 1.0f;   // 0..1, local tone
        float structure = 1.0f;   // 0..1, local structure
        float skin      = 1.0f;   // 0..1, skin structure (NR's own skin mask)
        int   passes    = 4;      // evaluations of the same frame, 1..8: NR builds on its history
    };

    inline int ClampModel(int m) noexcept { return std::clamp(m, 0, 2); }
    inline float ClampStrength(float v) noexcept { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 1.0f; }
    inline int ClampPasses(int p) noexcept { return std::clamp(p, 1, 8); }
}
