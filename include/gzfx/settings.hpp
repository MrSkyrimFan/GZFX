#pragma once

#include <cstdint>

namespace gzfx::settings
{
    struct Values
    {
        float ambientContactStrength = 0.62f;
        float ambientContactRadiusPixels = 4.0f;

        bool horizonOcclusionEnabled = true;
        float horizonOcclusionStrength = 0.90f;
        float horizonOcclusionRadiusPixels = 28.0f;
        std::uint32_t horizonOcclusionQuality = 2;

        bool indirectBounceEnabled = true;
        float indirectBounceStrength = 0.20f;
        float indirectBounceRadiusPixels = 36.0f;
        float indirectBounceSaturation = 0.85f;

        bool localGlowEnabled = true;
        float localGlowStrength = 0.18f;
        float localGlowThreshold = 1.10f;
        float localGlowRadiusPixels = 8.0f;

        std::uint32_t defaultMode = 1;
        std::uint64_t activationDrawIndexedThreshold = 1000;
        bool accessibleDiagnostics = true;
        bool captureOnModeChange = true;
    };

    enum class Editable : std::uint32_t
    {
        AmbientContactStrength = 0,
        AmbientContactRadiusPixels,
        HorizonOcclusionEnabled,
        HorizonOcclusionStrength,
        HorizonOcclusionRadiusPixels,
        HorizonOcclusionQuality,
        IndirectBounceEnabled,
        IndirectBounceStrength,
        IndirectBounceRadiusPixels,
        IndirectBounceSaturation,
        LocalGlowEnabled,
        LocalGlowStrength,
        LocalGlowThreshold,
        LocalGlowRadiusPixels,
        DefaultMode,
        ActivationDrawIndexedThreshold,
        AccessibleDiagnostics,
        CaptureOnModeChange,
        Count
    };

    const Values& Get() noexcept;
    bool Reload() noexcept;

    bool Adjust(Editable setting, int direction) noexcept;
    bool SetNormalized(Editable setting, float normalized) noexcept;
    float GetNormalized(Editable setting) noexcept;
    bool Toggle(Editable setting) noexcept;
}
