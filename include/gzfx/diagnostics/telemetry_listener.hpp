#pragma once

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

    void OnSkyrimCommandList(
        std::uint64_t frame,
        std::uint64_t ordinal,
        ID3D11CommandList* commandList,
        bool restoreState) noexcept;
}
