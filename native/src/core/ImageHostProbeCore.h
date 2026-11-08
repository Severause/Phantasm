#pragma once

// Phantasm - ImageHostProbeCore: whether Magelight can take the live views' texture images, engine-free
// (native/build.ps1 -Tests). LiveImageApi in MannequinRenderer.cpp asks through it.
//
// Magelight answers IsGpuAccelerated() false until its renderer is up (its first in-game frame), and false for good on
// its CPU path; the two cannot be told apart. So only a yes is kept: until one comes, every probe asks again, and a
// view refused "no-image" before Magelight's first frame can be opened again once it renders. Magelight never takes a
// yes back. Whether the API answered at all (no Magelight, an old host) cannot change within a process.
//
// Author: Severause

namespace Phantasm::ImageHostProbeCore
{
    enum class Answer
    {
        NoHost,      // no Magelight, or one too old for texture images
        NotYet,      // Magelight without GPU images so far: its renderer is not up yet, or it runs on the CPU
        Available,   // GPU texture images, for the rest of the process
    };

    struct Latch
    {
        bool   confirmed = false;
        bool   asked     = false;   // Probe has answered at least once
        Answer last      = Answer::NoHost;

        /// This probe's answer. askGpu() (Magelight's IsGpuAccelerated) is called only while the host is present and
        /// no yes has been seen. `changed` is true on the first probe and whenever the answer differs from the last
        /// one's: the caller logs then.
        template <class AskGpu>
        Answer Probe(bool hostPresent, AskGpu&& askGpu, bool& changed)
        {
            Answer a = Answer::NoHost;
            if (hostPresent) {
                if (!confirmed && askGpu()) confirmed = true;
                a = confirmed ? Answer::Available : Answer::NotYet;
            }
            changed = !asked || a != last;
            asked   = true;
            last    = a;
            return a;
        }
    };

    /// Whether a live view draws with no UI-host image, its client drawing the SRV itself: only with no Magelight
    /// that can take texture images at all. A Magelight not up yet, or on its CPU path, still refuses "no-image", so
    /// a client of Magelight never gets a Shown without an image URL.
    inline bool DrawsHostless(Answer a) noexcept { return a == Answer::NoHost; }
}
