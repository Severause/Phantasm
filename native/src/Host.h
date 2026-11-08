#pragma once

// Phantasm - Host: the API's clients and views over the renderer. It keeps the registry (ViewRegistryCore) under one
// mutex, so every API call can answer at once from any thread, and does the renderer work in tasks it posts to the
// game thread through PresentHandoff's pump, which never posts to SKSE from the caller's thread (a client may call
// from inside Magelight's Present). Events go to clients from those tasks, on the game thread, outside any render
// phase; no lock is held while a client's callback runs.

#include "Phantasm_API.h"

#include <cstdint>
#include <string>

namespace Phantasm::Host
{
    using namespace PHANTASM_API;

    // The API (Api.cpp). Any thread, except RebuildActor3D (Phantasm_API.h says where it may run).
    Result RegisterClient(const char* name, EventFn onEvent, void* user, ClientId* out);
    Result ReleaseClient(ClientId client);
    Result OpenView(ClientId client, const char* slot, std::uint32_t actorFormId, std::uint32_t width,
                    std::uint32_t height, Framing framing, ViewId* out);
    Result CloseView(ViewId view);
    Result ResizeView(ViewId view, std::uint32_t width, std::uint32_t height);
    Result Orbit(ViewId view, float dx, float dy);
    Result Zoom(ViewId view, float dz, float u, float v);
    Result Refresh(ViewId view);
    Result SetHint(ViewId view, const char* key, const char* value);
    Result QueryHint(ViewId view, const char* key);
    Result RequestStill(ClientId client, std::uint32_t actorFormId, std::uint32_t width, std::uint32_t height,
                        Framing framing, StillFn done, void* user, std::uint32_t* requestId);
    Result RebuildActor3D(std::uint32_t actorFormId, const char* who);
    Result OpenItemView(ClientId client, const char* slot, std::uint32_t itemFormId, std::uint32_t width,
                        std::uint32_t height, ViewId* out);
    Result CancelStill(ClientId client, std::uint32_t requestId);
    Result RequestStillHinted(ClientId client, std::uint32_t actorFormId, std::uint32_t width, std::uint32_t height,
                              Framing framing, const char* hints, StillFn done, void* user, std::uint32_t* requestId);
    Result RequestItemStill(ClientId client, std::uint32_t itemFormId, std::uint32_t width, std::uint32_t height,
                            const char* hints, StillFn done, void* user, std::uint32_t* requestId);

    /// Wires the renderer's events to the host. Once, at plugin load.
    void Initialize();
    /// A save is about to load or a new game starts (the SKSE revert callback): every view ends ("game-load").
    void OnRevert();
}
