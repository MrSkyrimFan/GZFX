#pragma once

#include <algorithm>
#include <cstdint>

namespace gzfx::render::product_control
{
    struct HorizonQualityBudget
    {
        std::uint32_t slices = 3;
        std::uint32_t steps = 6;
    };

    inline float HorizonStrengthMultiplier(
        const float setting) noexcept
    {
        return std::clamp(
            setting / 0.25f,
            0.0f,
            4.0f);
    }

    inline float HorizonRadiusWorld(
        const float radiusPixels) noexcept
    {
        return std::clamp(
            240.0f *
                radiusPixels /
                28.0f,
            48.0f,
            960.0f);
    }

    inline HorizonQualityBudget
    HorizonBudget(
        const std::uint32_t quality) noexcept
    {
        switch (std::min<std::uint32_t>(
            quality,
            3u))
        {
        case 0:
            return { 2u, 4u };
        case 1:
            return { 3u, 4u };
        case 2:
            return { 3u, 6u };
        default:
            return { 4u, 8u };
        }
    }

    inline bool LegacyAoEnabled(
        const bool horizonEnabled,
        const bool wave4OwnsAo) noexcept
    {
        return
            horizonEnabled &&
            !wave4OwnsAo;
    }
}
