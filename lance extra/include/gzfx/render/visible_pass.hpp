#pragma once

#include <d3d11.h>
#include <dxgi.h>

#include <cstdint>

namespace gzfx::render
{
    bool RenderVisiblePass(
        std::uint64_t frame,
        std::uint64_t drawIndexedThisFrame,
        IDXGISwapChain* swapChain,
        ID3D11Device* device,
        ID3D11DeviceContext* context) noexcept;
}
