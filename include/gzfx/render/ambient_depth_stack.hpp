#pragma once

#include "gzfx/diagnostics/resource_intelligence.hpp"

#include <d3d11.h>

#include <cstdint>

namespace gzfx::render::ambient_depth
{
    bool Apply(
        std::uint64_t frame,
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const gzfx::diagnostics::resource_intelligence::LiveInputs& inputs) noexcept;
}
