#pragma once

#include <d3d11.h>
#include <dxgi.h>

#include <cstdint>

namespace gzfx::diagnostics::resource_intelligence
{
    struct LiveInputs
    {
        ID3D11Texture2D* depthTexture = nullptr;
        ID3D11Texture2D* sceneColorTexture = nullptr;
        ID3D11Buffer* cameraBuffer = nullptr;
        D3D11_TEXTURE2D_DESC depthDesc{};
        D3D11_TEXTURE2D_DESC sceneColorDesc{};
    };

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

    bool AcquireLiveInputs(
        IDXGISwapChain* swapChain,
        LiveInputs& inputs) noexcept;

    void ReleaseLiveInputs(LiveInputs& inputs) noexcept;
}
