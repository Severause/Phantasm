#pragma once

// Phantasm - NeverDestroyed: a static whose destructor never runs.
//
// Author: Severause

#include <utility>

namespace Phantasm
{
    /// A new T for a static that holds engine or D3D references: a static's destructor would run at
    /// DLL_PROCESS_DETACH, under the loader lock, against an engine and a driver that may already be torn down, so this
    /// one is never destroyed and the OS reclaims it. Bind the result once: `static T& x = NeverDestroyed<T>();`.
    template <class T, class... Args>
    [[nodiscard]] T& NeverDestroyed(Args&&... args)
    {
        return *new T(std::forward<Args>(args)...);
    }
}
