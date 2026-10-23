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

    enum class Editable : std::uint32_t
    {
        AmbientContactStrength = 0,
        AmbientContactRadiusPixels,
        DefaultMode,
        ActivationDrawIndexedThreshold,
        AccessibleDiagnostics,
        CaptureOnModeChange,
        Count
    };

    const Values& Get() noexcept;
    bool Reload() noexcept;

    bool Adjust(Editable setting, int direction) noexcept;
    bool Toggle(Editable setting) noexcept;
}
