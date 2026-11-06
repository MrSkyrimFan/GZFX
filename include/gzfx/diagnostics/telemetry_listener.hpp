#pragma once

#include "gzfx/diagnostics/pipeline_cartography.hpp"
#include "gzfx/render/renderer_execution_service.hpp"

#include <d3d11.h>

#include <cstdint>

namespace gzfx::diagnostics::telemetry_listener
{
    enum class PresentPhase : std::uint32_t
    {
        HookEnter = 0,
        CallbacksBegin,
        CallbacksEnd,
        BeforeOriginalPresent,
        AfterOriginalPresent
    };

    enum class EngineAnchor : std::uint32_t
    {
        PostProcessing = 0,
        PreUi,
        MainDepth,
        MainShadowMasks,
        MainWorld,
        MainWaterEffects,
        Effects,
        ShadowDirectional,
        ShadowSpot,
        ShadowOmni,
        ShadowUnknown,
        WaterTaa,
        WaterBlend,
        ApplyReflections,
        UnderwaterMask,
        VolumetricLighting,
        ApplyVolumetricLighting,
        VolumetricBlurH,
        VolumetricBlurV,
        CompositeVolumetricLighting,
        WaterFlow,
        WaterDisplacement,
        Count
    };

    enum class EnginePhase : std::uint32_t
    {
        Enter = 0,
        Exit
    };

    struct FrameCounters
    {
        std::uint64_t frame = 0;
        std::uint64_t drawOrdinal = 0;
        std::uint64_t draws = 0;
        std::uint64_t drawIndexedCalls = 0;
        std::uint64_t drawInstancedCalls = 0;
        std::uint64_t drawIndexedInstancedCalls = 0;
        std::uint64_t dispatchCalls = 0;
        std::uint64_t renderTargetSets = 0;
        std::uint64_t renderTargetClears = 0;
        std::uint64_t depthClears = 0;
        std::uint64_t copies = 0;
        std::uint64_t resolves = 0;
        std::uint64_t viewportSets = 0;
    };

    struct DeferredListInfo
    {
        std::uint64_t frame = 0;
        std::uint64_t executeOrdinal = 0;
        std::uintptr_t commandListIdentity = 0;
        std::uint64_t commandListGeneration = 0;
        std::uintptr_t contextIdentity = 0;
        std::uint64_t contextGeneration = 0;
        std::uint64_t recordingGeneration = 0;
        std::uint64_t originalPassCount = 0;
        std::uint64_t retainedRunCount = 0;
        bool finishRestoreState = false;
        bool executeRestoreState = false;
        bool complete = false;
        bool overflow = false;
    };

    void OnDeferredList(
        const DeferredListInfo& info) noexcept;

    void OnDeferredRun(
        std::uint64_t frame,
        std::uint64_t executeOrdinal,
        std::uint64_t commandListGeneration,
        std::uint64_t contextGeneration,
        std::uint64_t recordingGeneration,
        std::uint64_t runIndex,
        std::uint64_t firstRecordingOrdinal,
        std::uint64_t lastRecordingOrdinal,
        std::uint64_t repeatCount,
        const pipeline_cartography::PassTelemetry& pass) noexcept;

    bool Initialize() noexcept;
    bool IsInitialized() noexcept;
    void Shutdown() noexcept;

    void OnPresent(
        const FrameCounters& counters) noexcept;

    void OnResize(
        std::uint64_t frame,
        UINT width,
        UINT height,
        DXGI_FORMAT format) noexcept;

    void OnOwnedCommandList(
        std::uint64_t frame,
        std::uint64_t ordinal,
        gzfx::render::execution::OwnedPass pass,
        ID3D11CommandList* commandList,
        bool restoreState) noexcept;

    void OnExternalCommandList(
        std::uint64_t frame,
        std::uint64_t ordinal,
        ID3D11CommandList* commandList,
        bool restoreState) noexcept;

    void OnPresentTimeline(
        std::uint64_t frame,
        std::uint64_t presentEpoch,
        PresentPhase phase,
        std::uint32_t threadId,
        std::uint32_t syncInterval,
        std::uint32_t flags,
        std::int32_t hresult) noexcept;

    void OnEngineEvent(
        std::uint64_t frame,
        EngineAnchor anchor,
        EnginePhase phase,
        std::uint32_t threadId,
        std::uint64_t runtimeProfile,
        std::uint64_t targetRva) noexcept;
}
