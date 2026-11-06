#pragma once

#include <cstdint>

namespace gzfx::engine::native_domain_anchors
{
    enum class ShadowKind : std::uint32_t
    {
        Unknown = 0,
        Directional,
        Spot,
        Omni
    };

    constexpr ShadowKind ClassifyShadowFlags(
        const bool directional,
        const bool frustum,
        const bool parabolic,
        const bool omni) noexcept
    {
        if (directional)
        {
            return ShadowKind::Directional;
        }

        if (frustum)
        {
            return ShadowKind::Spot;
        }

        if (parabolic || omni)
        {
            return ShadowKind::Omni;
        }

        return ShadowKind::Unknown;
    }

    struct Status
    {
        bool armed = false;
        bool testBypass = false;
        std::uint32_t highLevelHooks = 0;
        std::uint32_t imageEffectsBound = 0;
        std::uint32_t renderTargetsHooked = 0;
        std::uint32_t dispatchTargetsHooked = 0;
        std::uint32_t shadowTargetsHooked = 0;
        std::uint64_t events = 0;
        std::uint64_t lastFrame = 0;
    };

    bool ArmAfterDataLoaded() noexcept;
    void OnPresent(std::uint64_t frame) noexcept;
    Status GetStatus() noexcept;
}
