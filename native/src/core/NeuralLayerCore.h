#pragma once

// Neural layer rules: keeping other programs' Vulkan layers out of the Vulkan instance NR makes. A Vulkan instance
// loads every implicit layer registered on the machine (overlays, capture hooks, a global ReShade) into the process
// that makes it, and each starts inside the loader lock. Engine-free (tests/NeuralLayerCoreTest.cpp).

#include <cstdint>
#include <string_view>

namespace Phantasm::NeuralLayerCore
{
    /// The Vulkan loader's switch for layers, and the value that leaves every implicit layer out.
    inline constexpr std::wstring_view kDisableVariable = L"VK_LOADER_LAYERS_DISABLE";
    inline constexpr std::wstring_view kDisableImplicit = L"~implicit~";

    /// A Vulkan version as vkEnumerateInstanceVersion gives it.
    constexpr std::uint32_t Version(std::uint32_t major, std::uint32_t minor, std::uint32_t patch) noexcept
    {
        return (major << 22) | (minor << 12) | patch;
    }

    /// The first loader that reads kDisableVariable. A loader answers vkEnumerateInstanceVersion with the version of
    /// the headers it was built from, which is its own.
    inline constexpr std::uint32_t kFirstLoader = Version(1, 3, 234);

    /// True when the loader answering `version` leaves implicit layers out when kDisableVariable asks.
    constexpr bool KeepsLayersOut(std::uint32_t version) noexcept
    {
        return (version & 0x1FFFFFFFu) >= kFirstLoader;   // the top three bits are a variant, 0 for Vulkan itself
    }
}
