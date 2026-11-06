#pragma once

#include "gzfx/diagnostics/resource_intelligence.hpp"

#include <d3d11.h>

#include <cstdint>

namespace gzfx::render::unified_product_renderer
{
    struct ApplyInputs
    {
        std::uint64_t frame = 0;
        std::uint64_t drawOrdinal = 0;
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
        const gzfx::diagnostics::resource_intelligence::LiveInputs*
            live = nullptr;
        bool ambientDepthApplied = false;
        bool ambientVisibilityApplied = false;
    };

    struct Status
    {
        std::uint64_t applications = 0;
        std::uint64_t skips = 0;
        std::uint64_t failures = 0;
        std::uint64_t lastFrame = 0;
        std::uint64_t settingsRevision = 0;
        std::uint64_t activeEffectMask = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        float exposure = 1.0f;
        bool historyValid = false;
        bool sourceCopyCountOne = false;
        bool sceneWritebackCountOne = false;
    };

    bool Apply(
        const ApplyInputs& inputs) noexcept;

    bool WasAppliedFrame(
        std::uint64_t frame) noexcept;

    Status GetStatus() noexcept;

    void RequestManualCapture() noexcept;
    void Reset() noexcept;
}
