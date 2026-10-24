#pragma once

#include <d3d11.h>

#include <cstdint>
#include <string>
#include <vector>

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

    enum class LandmarkKind : std::uint32_t
    {
        MainDepthCopyEarly = 0,
        MainDepthCopyLate,
        HdrSceneCopy,
        ShadowMask,
        ReflectionMask,
        ReflectionColor,
        ReflectionBlurIntermediate,
        ReflectionBlurred,
        SaoCameraZ,
        SaoRawAo,
        SaoBlurIntermediate,
        SaoBlurred,
        SilRawIndirect,
        LightingCompositeHdr,
        VolumetricVolume,
        VolumetricTemporal,
        VolumetricBlurIntermediate,
        VolumetricBlurred
    };

    struct Landmark
    {
        LandmarkKind kind =
            LandmarkKind::MainDepthCopyEarly;

        std::uint32_t confidence = 0;
        std::uint64_t passId = 0;
        std::uintptr_t resource = 0;
    };

    std::vector<Landmark>
    GetCaptureLandmarks() noexcept;

    const char*
    LandmarkName(LandmarkKind kind) noexcept;

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
        std::uint64_t e,
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

    void ObserveMap(
        std::uint64_t frame,
        std::uint64_t drawOrdinal,
        ID3D11Resource* resource,
        UINT subresource,
        D3D11_MAP mapType,
        UINT mapFlags,
        const D3D11_MAPPED_SUBRESOURCE* mapped) noexcept;

    void ObserveUnmap(
        std::uint64_t frame,
        std::uint64_t drawOrdinal,
        ID3D11Resource* resource,
        UINT subresource) noexcept;

    void ObserveUpdateSubresource(
        std::uint64_t frame,
        std::uint64_t drawOrdinal,
        ID3D11Resource* resource,
        UINT destinationSubresource,
        const D3D11_BOX* destinationBox,
        const void* sourceData,
        UINT sourceRowPitch,
        UINT sourceDepthPitch) noexcept;

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
