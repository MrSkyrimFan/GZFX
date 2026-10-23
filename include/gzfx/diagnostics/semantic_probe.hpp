#pragma once

#include <d3d11.h>
#include <dxgi.h>

#include <cstdint>

namespace gzfx::diagnostics::semantic_probe
{
    void SetPixelShader(ID3D11PixelShader* shader) noexcept;
    void SetComputeShader(ID3D11ComputeShader* shader) noexcept;
    void SetViewport(UINT count, const D3D11_VIEWPORT* viewports) noexcept;

    void ObserveRenderTargets(
        std::uint64_t frame,
        std::uint64_t ordinal,
        UINT count,
        ID3D11RenderTargetView* const* renderTargets,
        ID3D11DepthStencilView* depthTarget) noexcept;

    void ObservePixelResources(
        std::uint64_t frame,
        std::uint64_t ordinal,
        UINT startSlot,
        UINT count,
        ID3D11ShaderResourceView* const* resources) noexcept;

    void ObserveComputeResources(
        std::uint64_t frame,
        std::uint64_t ordinal,
        UINT startSlot,
        UINT count,
        ID3D11ShaderResourceView* const* resources) noexcept;

    void ObserveComputeUavs(
        std::uint64_t frame,
        std::uint64_t ordinal,
        UINT startSlot,
        UINT count,
        ID3D11UnorderedAccessView* const* resources) noexcept;

    void OnPresent(
        std::uint64_t frame,
        std::uint64_t drawIndexedActivity,
        IDXGISwapChain* swapChain,
        ID3D11Device* device,
        ID3D11DeviceContext* context) noexcept;

    bool SuppressSceneEffects() noexcept;
}
