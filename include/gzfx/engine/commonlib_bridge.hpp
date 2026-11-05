#pragma once

#include <cstdint>

namespace gzfx::engine::commonlib
{
    enum class Status : std::uint32_t
    {
        Disabled = 0,
        TestBypass,
        ModuleReady,
        AddressLibraryMissing,
        WaitingForDataLoaded,
        Bound,
        BindFailed
    };

    struct Snapshot
    {
        std::uint64_t frame = 0;

        Status status = Status::Disabled;

        bool moduleReady = false;
        bool addressLibraryPresent = false;
        bool dataLoaded = false;
        bool rendererBound = false;
        bool rendererCorrelated = false;
        bool fallbackAttachAttempted = false;
        bool fallbackAttachSucceeded = false;

        std::uintptr_t renderer = 0;
        std::uintptr_t sky = 0;
        std::uintptr_t dataHandler = 0;

        std::uintptr_t device = 0;
        std::uintptr_t context = 0;
        std::uintptr_t swapChain = 0;

        std::uintptr_t currentWeather = 0;
        std::uintptr_t lastWeather = 0;
        std::uintptr_t sun = 0;
        std::uintptr_t clouds = 0;
        std::uintptr_t masser = 0;
        std::uintptr_t secunda = 0;
        std::uintptr_t precipitation = 0;

        float gameHour = 0.0f;
        float weatherBlend = 0.0f;
        float windSpeed = 0.0f;
        float windAngle = 0.0f;
        float fogNear = 0.0f;
        float fogFar = 0.0f;
        float fogPower = 0.0f;
        float fogClamp = 0.0f;

        std::uint32_t skyMode = 0;
    };

    bool InitializeModule() noexcept;
    bool BindAfterDataLoaded() noexcept;
    void OnPresent(std::uint64_t frame) noexcept;

    Snapshot GetSnapshot() noexcept;
    const char* StatusName(Status status) noexcept;

    const char* EmbeddedMitNotice() noexcept;
}
