#pragma once

#include <cstdint>
#include <string>

namespace gzfx::render::stage_scheduler
{
    enum class Boundary : std::uint32_t
    {
        Unknown = 0,
        AfterShadowProduction,
        AfterDepthPrepass,
        AfterOpaqueWorldMRT,
        AfterCameraZ,
        AfterStockAO,
        BeforeAmbientScreenSpaceComposite,
        AtAmbientScreenSpaceComposite,
        AfterAmbientScreenSpaceComposite,
        BeforeVolumetricApply,
        AfterVolumetricApply,
        PublishPreWaterDepth,
        WaterDomain,
        BeforeGlobalTAA,
        AfterGlobalTAA,
        HDRReduction,
        BeforeTonemap,
        PostTonemapPreUi,
        UiPresent,
        Count
    };

    enum class Phase : std::uint32_t
    {
        Landmark = 0,
        BeforeExecution,
        AfterExecution
    };

    struct Event
    {
        Boundary boundary = Boundary::Unknown;
        Phase phase = Phase::Landmark;
        std::uint64_t frame = 0;
        std::uint64_t ordinal = 0;
        std::uint64_t sequence = 0;
        const char* reason = nullptr;
    };

    using Callback = void(*)(
        const Event& event,
        void* user) noexcept;

    bool RegisterCallback(
        Boundary boundary,
        Callback callback,
        void* user) noexcept;

    void ClearCallbacks() noexcept;

    void Signal(
        Boundary boundary,
        Phase phase,
        std::uint64_t frame,
        std::uint64_t ordinal,
        const char* reason) noexcept;

    void BeginEvidenceCapture(
        std::uint64_t startFrame,
        std::uint64_t endFrame) noexcept;

    void FinishEvidenceCapture(
        const std::wstring& suffix) noexcept;

    void Reset() noexcept;

    const char* BoundaryName(Boundary boundary) noexcept;
    const char* PhaseName(Phase phase) noexcept;
}
