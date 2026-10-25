#pragma once

#include <cstdint>

namespace gzfx::render::ambient_visibility_v2
{
    struct TelemetryStatus
    {
        std::uint64_t frame = 0;
        std::uint64_t computes = 0;
        std::uint64_t applications = 0;
        std::uint64_t skips = 0;
        bool signalHistoryValid = false;
        std::uint64_t wave3HistoryGeneration = 0;
        std::uint64_t finalVisibilityFrame = 0;
        std::uint64_t lastAppliedFrame = 0;
        bool gpuTimingValid = false;
        double gpuTotalMs = 0.0;
    };

    bool Initialize() noexcept;
    bool IsInitialized() noexcept;

    bool WasAppliedFrame(
        std::uint64_t frame) noexcept;

    bool OwnsDevelopmentMode(
        std::uint32_t mode) noexcept;

    void RequestManualCapture() noexcept;

    TelemetryStatus GetTelemetryStatus() noexcept;
}
