#pragma once

#include "gzfx/diagnostics/resource_intelligence.hpp"

#include <d3d11.h>

#include <cstdint>

namespace gzfx::render::scene_effects
{
    bool ApplyPreUi(
        std::uint64_t frame,
        std::uint64_t drawOrdinal,
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const gzfx::diagnostics::resource_intelligence::LiveInputs& inputs) noexcept;
}
