#pragma once

// Phantasm Showcase - Shots: a still's pixels written as a PNG, to a file (the Studio's Save) or a data URL (the
// wardrobe's turntable). One worker thread encodes them in order through WIC, so a still's done callback (the game
// thread) only copies and queues.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Showcase::Shots
{
    /// The folder pictures are saved to (My Games/<game>/SKSE/PhantasmShowcase/Shots).
    std::filesystem::path Folder();

    /// Writes the pixels as a PNG under Folder() named "<stem>_<time>.png". premultiplied: a Cutout's pixels, made
    /// straight first. done(ok, path or error) runs on the worker thread.
    void Save(std::vector<std::uint8_t> rgba, std::uint32_t width, std::uint32_t height, bool premultiplied,
              std::string stem, std::function<void(bool, const std::string&)> done);

    /// The pixels as a "data:image/png;base64,..." URL. done(ok, url) runs on the worker thread.
    void DataUrl(std::vector<std::uint8_t> rgba, std::uint32_t width, std::uint32_t height, bool premultiplied,
                 std::function<void(bool, std::string)> done);
}
