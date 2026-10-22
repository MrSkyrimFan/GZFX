#pragma once

#include <d3d11.h>
#include <dxgi.h>

#include <cstdint>

namespace gzfx::diagnostics::resource_intelligence
{
    void ObserveRenderTargets(
        std::uint64_t frame,
        UINT count,
        ID3D11RenderTargetView* const* renderTargets,
        ID3D11DepthStencilView* depthTarget) noexcept;

    void ObserveConstantBuffers(
        std::uint64_t frame,
        char stage,
        UINT startSlot,
        UINT count,
        ID3D11Buffer* const* buffers) noexcept;

    void OnPresent(
        std::uint64_t frame,
        IDXGISwapChain* swapChain,
        ID3D11Device* device,
        ID3D11DeviceContext* context) noexcept;
}
