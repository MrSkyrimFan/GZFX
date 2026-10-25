#pragma once

#include "gzfx/diagnostics/pipeline_cartography.hpp"
#include "gzfx/render/renderer_execution_service.hpp"

#include <d3d11.h>

#include <cstdint>

namespace gzfx::diagnostics::telemetry_listener
{
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
}
