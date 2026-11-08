#pragma once

// Phantasm Showcase - Underlay: on PrismaUI, the page's backdrop, its stages' backgrounds and the live views' pictures,
// drawn into the game's frame under the page.
//
// PrismaUI shows no outside texture, so the page leaves its backdrop and stages transparent and reports where the
// stages are (stageRects), and an engine menu of ours draws them from its PostDisplay. The engine draws every menu
// during its UI render, inside the frame; PrismaUI draws its pages from its hook on the frame's Present, after that, so
// the page always lands on top of what this draws, whatever the plugins' load order. On Magelight nothing here runs.
//
// Threads: SetImage / ClearImage come from Phantasm's events (game thread), SetBoxes from the bridge (game thread);
// the draw is on the main thread during the UI render. One mutex guards the state; the draw holds it only to copy.

#include "HostCore.h"

#include <string>
#include <vector>

struct ID3D11ShaderResourceView;

namespace Showcase::Underlay
{
    /// Registers the menu with the engine. Game thread, at kDataLoaded, before any Show.
    void Register();

    /// Opens the menu (the page opened) or closes it and forgets every box and picture (the page closed).
    void Show(bool on);

    /// A slot's picture: Phantasm's offscreen, AddRef'd and kept until the slot's next picture or ClearImage.
    void SetImage(const std::string& slot, ID3D11ShaderResourceView* srv);
    void ClearImage(const std::string& slot);

    struct PageBox
    {
        std::string    slot;
        HostCore::Box  box;
    };

    /// The page's stages, in page pixels of a page pageW x pageH, replacing the last set.
    void SetBoxes(float pageW, float pageH, std::vector<PageBox> boxes);
}
