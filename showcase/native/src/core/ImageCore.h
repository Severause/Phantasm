#pragma once

// Phantasm Showcase - ImageCore: a still's pixels made ready for a PNG, and base64 for a data URL. Engine-free.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Showcase::ImageCore
{
    inline float SrgbToLinear(float s) noexcept
    {
        return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
    }

    inline float LinearToSrgb(float l) noexcept
    {
        l = std::clamp(l, 0.0f, 1.0f);
        return l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
    }

    /// A Cutout's premultiplied RGBA8 back to straight alpha in place (a PNG stores straight alpha). Phantasm
    /// premultiplies in linear light and then encodes sRGB, so a channel is decoded, divided by the alpha and encoded
    /// again: dividing the encoded value would brighten every soft edge. A clear pixel stays all zero.
    inline void UnpremultiplySrgb(std::uint8_t* rgba, std::size_t pixels) noexcept
    {
        for (std::size_t i = 0; i < pixels; ++i) {
            std::uint8_t* p = rgba + i * 4;
            const unsigned a = p[3];
            if (a == 0) {
                p[0] = p[1] = p[2] = 0;
                continue;
            }
            if (a == 255) continue;
            const float alpha = static_cast<float>(a) / 255.0f;
            for (int c = 0; c < 3; ++c) {
                const float lin = SrgbToLinear(static_cast<float>(p[c]) / 255.0f) / alpha;
                p[c] = static_cast<std::uint8_t>(std::lround(LinearToSrgb(lin) * 255.0f));
            }
        }
    }

    /// RGBA to BGRA in place (WIC's PNG encoder takes BGRA on every Windows version).
    inline void SwapRedBlue(std::uint8_t* rgba, std::size_t pixels) noexcept
    {
        for (std::size_t i = 0; i < pixels; ++i) std::swap(rgba[i * 4], rgba[i * 4 + 2]);
    }

    inline std::string Base64(const std::uint8_t* data, std::size_t size)
    {
        static constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve((size + 2) / 3 * 4);
        std::size_t i = 0;
        for (; i + 2 < size; i += 3) {
            const std::uint32_t v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
            out.push_back(kTable[(v >> 18) & 63]);
            out.push_back(kTable[(v >> 12) & 63]);
            out.push_back(kTable[(v >> 6) & 63]);
            out.push_back(kTable[v & 63]);
        }
        if (const auto left = size - i; left > 0) {
            const std::uint32_t v = (data[i] << 16) | (left == 2 ? data[i + 1] << 8 : 0);
            out.push_back(kTable[(v >> 18) & 63]);
            out.push_back(kTable[(v >> 12) & 63]);
            out.push_back(left == 2 ? kTable[(v >> 6) & 63] : '=');
            out.push_back('=');
        }
        return out;
    }
}
