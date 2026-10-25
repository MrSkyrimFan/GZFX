#pragma once

#include <cstdint>

namespace gzfx::diagnostics::engine_epochs
{
    struct Status
    {
        std::uint64_t runtimeProfile = 0;
        bool rendererCoreSupported = true;
        bool runtimeKnown = false;
        bool engineProfileAvailable = false;
        bool postProcessingArmed = false;
        bool preUiArmed = false;
        std::uint64_t postProcessingTargetRva = 0;
        std::uint64_t preUiTargetRva = 0;
        std::uint64_t postProcessingEvents = 0;
        std::uint64_t preUiEvents = 0;
    };

    bool Arm() noexcept;
    Status GetStatus() noexcept;

    namespace testing
    {
        struct CallFixtureResult
        {
            bool matched = false;
            std::uint64_t targetRva = 0;
        };

        CallFixtureResult ValidateAe1170PostProcessingFixture(
            const std::uint8_t* bytes,
            std::uint64_t matchRva,
            std::uint64_t imageBaseRva = 0) noexcept;

        CallFixtureResult ValidateAe1170PreUiFixture(
            const std::uint8_t* bytes,
            std::uint64_t matchRva,
            std::uint64_t imageBaseRva = 0) noexcept;

        std::uint32_t ClassifyRuntime(
            std::uint16_t major,
            std::uint16_t minor,
            std::uint16_t patch,
            std::uint16_t build) noexcept;
    }
}
