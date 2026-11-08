#pragma once

// Phantasm Showcase - Anims: the loose animation files under Data/meshes/actors, found once on a worker thread
// started at kDataLoaded, so the list is ready before the menu first opens (the walk takes seconds on a large
// load order). AnimPathCore decides what counts and what the player hid. Files packed in a BSA are not listed: the
// game reads them through its archives, which a folder walk cannot see. Any thread.

#include <string_view>

namespace Showcase::Anims
{
    /// { state: "scanning" | "ready", total, folder, folders: [{ name, count }], files: [{ name, path }] } for the
    /// folder (a path under meshes, "" for the top). Starts the scan if Prewarm has not.
    nlohmann::json List(std::string_view folder);

    /// Starts the scan in the background; call after Settings::Load (it reads the hidden folders).
    void Prewarm();

    /// { state, total, files: [{ name, path }] }: at most `limit` files whose path has every word of `text`.
    nlohmann::json Search(std::string_view text, std::size_t limit);
}
