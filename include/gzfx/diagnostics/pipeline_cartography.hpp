#pragma once

#include <d3d11.h>

#include <cstdint>
#include <string>

namespace gzfx::diagnostics::pipeline_cartography
{
    enum class ShaderStage : std::uint32_t
    {
        Vertex = 0,
        Hull,
        Domain,
        Geometry,
        Pixel,
        Compute
    };

    enum class DrawKind : std::uint32_t
    {
        Draw = 0,
        DrawIndexed,
        DrawInstanced,
        DrawIndexedInstanced,
        DrawInstancedIndirect,
        DrawIndexedInstancedIndirect
    };

    bool AttachDevice(ID3D11Device* device) noexcept;

    void BeginCapture(
        std::uint64_t startFrame,
        std::uint64_t endFrame,
        ID3D11DeviceContext* context,
        std::uintptr_t knownSceneColor,
        std::uintptr_t knownDepth,
        std::uintptr_t knownMotion,
        std::uintptr_t knownNormal,
        std::uintptr_t knownAux) noexcept;

    void FinishCapture(const std::wstring& suffix) noexcept;

    void SetShader(
        ShaderStage stage,
        ID3D11DeviceChild* shader) noexcept;

    void SetShaderResources(
        ShaderStage stage,
        UINT startSlot,
        UINT count,
        ID3D11ShaderResourceView* const* resources) noexcept;

    void SetConstantBuffers(
        ShaderStage stage,
        UINT startSlot,
        UINT count,
        ID3D11Buffer* const* buffers) noexcept;

    void SetSamplers(
        ShaderStage stage,
        UINT startSlot,
        UINT count,
        ID3D11SamplerState* const* samplers) noexcept;

    void SetRenderTargets(
        UINT count,
        ID3D11RenderTargetView* const* renderTargets,
        ID3D11DepthStencilView* depthTarget) noexcept;

    void SetRenderTargetsAndUavs(
        UINT renderTargetCount,
        ID3D11RenderTargetView* const* renderTargets,
        ID3D11DepthStencilView* depthTarget,
        UINT uavStartSlot,
        UINT uavCount,
        ID3D11UnorderedAccessView* const* uavs) noexcept;

    void SetComputeUavs(
        UINT startSlot,
        UINT count,
        ID3D11UnorderedAccessView* const* uavs) noexcept;

    void SetBlendState(
        ID3D11BlendState* state,
        const FLOAT blendFactor[4],
        UINT sampleMask) noexcept;

    void SetDepthStencilState(
        ID3D11DepthStencilState* state,
        UINT stencilRef) noexcept;

    void SetRasterizerState(
        ID3D11RasterizerState* state) noexcept;

    void SetPrimitiveTopology(
        D3D11_PRIMITIVE_TOPOLOGY topology) noexcept;

    void SetViewports(
        UINT count,
        const D3D11_VIEWPORT* viewports) noexcept;

    void ClearState() noexcept;

    void ExecuteDraw(
        std::uint64_t frame,
        std::uint64_t drawOrdinal,
        DrawKind kind,
        std::uint64_t a,
        std::uint64_t b,
        std::uint64_t c,
        std::uint64_t d,
        ID3D11Buffer* indirectArgs,
        UINT indirectOffset) noexcept;

    void ExecuteDispatch(
        std::uint64_t frame,
        std::uint64_t drawOrdinal,
        UINT x,
        UINT y,
        UINT z,
        ID3D11Buffer* indirectArgs,
        UINT indirectOffset) noexcept;

    void CopyResource(
        std::uint64_t frame,
        std::uint64_t drawOrdinal,
        ID3D11Resource* destination,
        ID3D11Resource* source) noexcept;

    void CopySubresourceRegion(
        std::uint64_t frame,
        std::uint64_t drawOrdinal,
        ID3D11Resource* destination,
        UINT destinationSubresource,
        UINT destinationX,
        UINT destinationY,
        UINT destinationZ,
        ID3D11Resource* source,
        UINT sourceSubresource,
        const D3D11_BOX* sourceBox) noexcept;

    void ResolveSubresource(
        std::uint64_t frame,
        std::uint64_t drawOrdinal,
        ID3D11Resource* destination,
        UINT destinationSubresource,
        ID3D11Resource* source,
        UINT sourceSubresource,
        DXGI_FORMAT format) noexcept;

    void ClearRenderTarget(
        std::uint64_t frame,
        std::uint64_t drawOrdinal,
        ID3D11RenderTargetView* view,
        const FLOAT color[4]) noexcept;

    void ClearDepthStencil(
        std::uint64_t frame,
        std::uint64_t drawOrdinal,
        ID3D11DepthStencilView* view,
        UINT flags,
        FLOAT depth,
        UINT8 stencil) noexcept;

    void ClearUavUint(
        std::uint64_t frame,
        std::uint64_t drawOrdinal,
        ID3D11UnorderedAccessView* view,
        const UINT values[4]) noexcept;

    void ClearUavFloat(
        std::uint64_t frame,
        std::uint64_t drawOrdinal,
        ID3D11UnorderedAccessView* view,
        const FLOAT values[4]) noexcept;

    void GenerateMips(
        std::uint64_t frame,
        std::uint64_t drawOrdinal,
        ID3D11ShaderResourceView* view) noexcept;

    void ExecuteCommandList(
        std::uint64_t frame,
        std::uint64_t drawOrdinal,
        ID3D11CommandList* commandList,
        BOOL restoreContextState) noexcept;
}
