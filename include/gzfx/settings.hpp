#pragma once

#include <cstdint>

namespace gzfx::settings
{
    struct Values
    {
        float ambientContactStrength = 0.62f;
        float ambientContactRadiusPixels = 4.0f;
        std::uint32_t defaultMode = 1;
        std::uint64_t activationDrawIndexedThreshold = 1000;
        bool accessibleDiagnostics = true;
        bool captureOnModeChange = true;
    };

    const Values& Get() noexcept;
    bool Reload() noexcept;
}
