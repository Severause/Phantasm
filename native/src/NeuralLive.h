#pragma once

// Neural live views: DLSS NR over an actor live view's frame on the GPU, when Phantasm.ini [NeuralRendering] LiveViews
// is on (see NeuralStills.h). One view at a time holds it: the first to draw while none does, until it ends or stops
// drawing; the next view to draw then takes it over, with the same objects when it is the same size.
//
// A frame: the offscreen is scaled into a float texture shared with NR's own Vulkan device, with the depth; a shared
// D3D11 fence is signalled, NR is queued to wait
// for it and signal it again, the game's context waits for that on the GPU and scales NR's answer back into the
// offscreen. The wait is queued only once NR has queued the signal, so the game can never wait for a signal that will
// not come. NR runs with no motion vectors: it
// keeps its history and starts it again when the actor or the offscreen changes.

#include <d3d11.h>

#include <cstdint>

namespace Phantasm::NeuralLive
{
    /// Render thread, inside a live actor frame (its crash net and D3D state backup), after the frame is drawn and
    /// before it is published. offscreen is the R8G8B8A8 typeless target read through offscreenSrv (UNORM); depth is
    /// its D24S8 buffer and depthCopy / depthCopySrv the frame's readable copy of it (null: NR gets no depth).
    /// True when the frame stays as drawn only because NR is still being set up for this view: the caller draws the
    /// view again next frame (a view whose picture does not change would otherwise never go through NR).
    bool Process(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* offscreen,
                 ID3D11ShaderResourceView* offscreenSrv, ID3D11Texture2D* depth, ID3D11Texture2D* depthCopy,
                 ID3D11ShaderResourceView* depthCopySrv, std::uint32_t offW, std::uint32_t offH, std::uint32_t view,
                 std::uint32_t fid);

    /// A live actor view ended: when it held NR, the next view to draw takes over at once. Any thread.
    void OnViewEnded(std::uint32_t view);

    /// NR worker, when no job has come for a while: lets the live view's objects go once its view stopped drawing.
    void OnWorkerIdle();
}
