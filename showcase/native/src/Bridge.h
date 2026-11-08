#pragma once

// Phantasm Showcase - Bridge: the page protocol (docs/PROTOCOL.md). Requests arrive on the render thread inside Present,
// are copied and posted to the game thread with Magelight's PostGameTask, handled there, and answered with InteropCall
// (callable from any thread).

#include "MagelightUI_API.h"

#include <string>

namespace Showcase::Bridge
{
    /// Listens on the view's "showcase" channel. Once, after the view is created.
    void Attach(const MAGELIGHT_API::MagelightApi4* v4, MAGELIGHT_API::ViewId view);

    /// Another host's way to the page (PrismaUI's InteropCall on "showcaseEvent"), instead of Attach. Once.
    using SendFn = void (*)(const char* text);
    void AttachSender(SendFn send);

    /// A request from another host's page, already on the game thread.
    void HandleText(const std::string& text);

    /// A push to the page ({ event, ... }). Any thread.
    void Push(const nlohmann::json& message);

    /// The menu closed: every view closes, a try-on reverts, a turntable stops. Game thread.
    void OnMenuClosed();
}
