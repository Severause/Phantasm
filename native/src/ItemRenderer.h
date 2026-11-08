#pragma once
// ItemRenderer — the live item view (the API's OpenItemView; beside the actor view it shares code with).
// Implemented in MannequinRenderer.cpp (its collector and effects after the actor collectors, its entry points at
// the end), whose file-static geometry collector, input-layout cache, GPU bundle and CbPayload layout it reuses: do
// not move it out. Its particle shaders are a stage of their own, whose failure costs only its particles.
// BSModelDB::Demand primes the world model (Inventory3DManager's async load only completes for resident models),
// then Inventory3DManager builds the display model with texture swaps (a clone of the primed model is the fallback,
// and what is drawn while an engine item menu owns the manager: BSModelDB's copy is never changed);
// UnloadInventoryItem() + End3D() run on every manager exit path. The offscreen is the Magelight texture image the
// API view names (the backbuffer is never touched), or with no Magelight, for a hostless view, an SRV the client draws
// itself, and itemLiveStatus {formId, ok, imageUrl, view, seq} is sent from the first drawn frame, when the SRV exists
// (imageUrl empty for a hostless view). A start is refused with an immediate ok:false where the view could not be
// drawn (LiveStartRefusal in MannequinRenderer.cpp: a host without texture images such as Magelight's CPU renderer, no
// Magelight for a view that is not hostless, or an unavailable GPU bundle).
//
// Author: Severause

#include <RE/Skyrim.h>

#include "StillShotCore.h"

#include <cstdint>
#include <string_view>

namespace Phantasm
{
    class ItemRenderer
    {
    public:
        static ItemRenderer* GetSingleton()
        {
            static ItemRenderer instance;
            return &instance;
        }

        /// Save-revert hook (Host.cpp): ends the live viewport so its NiPointer and geometry never cross saves.
        void ResetTransientState();

        /// ── LIVE VIEWPORT ───────────────────────────────────────────────
        /// The carrier's PostDisplay draws the item with the renderer's own pipeline every frame (the engine's
        /// item renderer stays inert outside real item menus): the stage's dark card, then the cached
        /// geometry under a yaw+pitch orbit camera. The caller streams the view's size and drag
        /// deltas; w / h are view pixels (CSS px x devicePixelRatio), which size the offscreen (a side under 4 is
        /// drawn at 4).
        /// CARD-ONLY mode: when the model can't resolve, LiveStart still ACTIVATES with empty
        /// geometry, so the image shows the dark card rather than a failed start.
        /// imageName, view and openedSeq: the API view that draws (MannequinRenderer::LiveStart's; 0 is an item
        /// shot's), stamped on its statuses. The other views, a shot's included, go on drawing.
        /// worn (an item shot's): an armor's worn model for a man or a woman instead of its ground model, without the
        /// body skin the file carries.
        /// hostless: the view's `hostless` hint (MannequinRenderer::LiveStart's).
        void LiveStart(RE::FormID itemFormId, float w, float h, std::string_view imageName, std::uint32_t view,
                       std::uint64_t openedSeq, StillShotCore::Worn worn = StillShotCore::Worn::kGround,
                       bool hostless = false);
        // The camera and size calls do nothing for a view the renderer does not hold.
        void LiveSetRect(std::uint32_t view, float w, float h);
        // A drag or zoom step that is not finite is ignored (MirrorCameraCore).
        void LiveDrag(std::uint32_t view, float dx, float dy);
        // u / v: the pointer's place in the view (0..1 from the top left), which the zoom moves toward.
        void LiveZoom(std::uint32_t view, float dz, float u = 0.5f, float v = 0.5f);
        // Ends the view and forgets its state; LiveEnd() ends every item view.
        void LiveEnd(std::uint32_t view);
        void LiveEnd();

    private:
        ItemRenderer() = default;
    };
}
