#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gzfx::render::wave3_math
{
    struct Float2
    {
        float x = 0.0f;
        float y = 0.0f;
    };


    inline std::uint64_t ContractToken(
        const std::uint32_t semantic,
        const std::uint32_t dimension,
        const std::uint32_t width,
        const std::uint32_t height,
        const std::uint32_t mipLevels,
        const std::uint32_t arraySize,
        const std::uint32_t sampleCount,
        const std::uint32_t sampleQuality,
        const std::uint32_t resourceFormat,
        const std::uint32_t viewFormat) noexcept
    {
        std::uint64_t hash = 1469598103934665603ull;

        const auto mix =
            [&](const std::uint64_t value)
            {
                for (std::uint32_t byte = 0; byte < 8; ++byte)
                {
                    hash ^=
                        static_cast<std::uint8_t>(
                            (value >> (byte * 8u)) & 0xFFu);

                    hash *= 1099511628211ull;
                }
            };

        mix(semantic);
        mix(dimension);
        mix(width);
        mix(height);
        mix(mipLevels);
        mix(arraySize);
        mix(sampleCount);
        mix(sampleQuality);
        mix(resourceFormat);
        mix(viewFormat);

        return hash;
    }

    inline std::uint32_t MipCount(
        std::uint32_t width,
        std::uint32_t height) noexcept
    {
        std::uint32_t extent = std::max(width, height);
        std::uint32_t count = 0;

        while (extent != 0)
        {
            ++count;
            extent >>= 1u;
        }

        return std::max<std::uint32_t>(count, 1u);
    }

    inline Float2 PreviousUv(
        const Float2 currentUv,
        const Float2 nativeMotion) noexcept
    {
        return Float2{
            currentUv.x + nativeMotion.x,
            currentUv.y + nativeMotion.y
        };
    }

    inline Float2 DynamicResolutionUv(
        const Float2 logicalUv,
        const Float2 ratio) noexcept
    {
        return Float2{
            logicalUv.x * ratio.x,
            logicalUv.y * ratio.y
        };
    }

    inline bool LogicalUvInBounds(
        const Float2 uv) noexcept
    {
        return
            uv.x >= 0.0f &&
            uv.x <= 1.0f &&
            uv.y >= 0.0f &&
            uv.y <= 1.0f;
    }

    inline float MotionPixels(
        const Float2 nativeMotion,
        const std::uint32_t activeWidth,
        const std::uint32_t activeHeight) noexcept
    {
        const float x =
            nativeMotion.x *
            static_cast<float>(activeWidth);

        const float y =
            nativeMotion.y *
            static_cast<float>(activeHeight);

        return std::sqrt(x * x + y * y);
    }

    inline float HistoryFrameCap(
        const float motionPixels,
        const float stillCap = 16.0f,
        const float fastCap = 4.0f,
        const float stillPixels = 0.5f,
        const float fastPixels = 32.0f) noexcept
    {
        if (!std::isfinite(motionPixels))
        {
            return fastCap;
        }

        if (motionPixels <= stillPixels)
        {
            return stillCap;
        }

        if (motionPixels >= fastPixels)
        {
            return fastCap;
        }

        const float t =
            (motionPixels - stillPixels) /
            std::max(fastPixels - stillPixels, 1e-6f);

        return stillCap + (fastCap - stillCap) * t;
    }
}