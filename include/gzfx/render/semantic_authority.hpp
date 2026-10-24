#pragma once

#include "gzfx/render/stage_scheduler.hpp"

#include <d3d11.h>

#include <array>
#include <cstdint>
#include <string>

namespace gzfx::render::semantic_authority
{
    enum class Confidence : std::uint32_t
    {
        Unknown = 0,
        Provisional,
        Strong,
        Verified
    };

    enum class EnvironmentClass : std::uint32_t
    {
        UnknownTransition = 0,
        Exterior,
        Interior
    };

    enum class Semantic : std::uint32_t
    {
        SceneColorHDR = 0,
        MainDepthCurrent,
        MotionVectors,
        NativeNormalMask,
        AuxiliaryRT3,
        DepthEarlyCopy,
        StockCameraZ,
        RawAOCurrent,
        RawAOPrevious,
        AOFilterTemp,
        AOFiltered,
        RawIndirect,
        ShadowMask,
        VolumetricDensityA,
        VolumetricDensityB,
        VolumetricPreviousDepth,
        PreWaterDepthCurrent,
        ReducedReflectionLighting,
        ReducedTemporalLightingHistory,
        Count
    };

    enum class QueryMode : std::uint32_t
    {
        CurrentFrame = 0,
        PreviousFrameHistory,
        PersistentIdentity,
        DiagnosticAnyKnown
    };

    enum class ContentEpochKind : std::uint32_t
    {
        Unknown = 0,
        CurrentFrame,
        PreviousFrameHistory,
        PreWaterCurrent
    };

    enum class ShaderStage : std::uint32_t
    {
        Vertex = 0,
        Hull,
        Domain,
        Geometry,
        Pixel,
        Compute,
        Count
    };

    struct ResourceInfo
    {
        Semantic semantic = Semantic::SceneColorHDR;
        Confidence confidence = Confidence::Unknown;
        std::uintptr_t identity = 0;
        std::uint64_t generation = 0;
        std::uint64_t firstSeenFrame = 0;
        std::uint64_t lastSeenFrame = 0;

        D3D11_RESOURCE_DIMENSION dimension =
            D3D11_RESOURCE_DIMENSION_UNKNOWN;

        UINT width = 0;
        UINT height = 0;
        UINT depth = 0;
        UINT mipLevels = 0;
        UINT arraySize = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        UINT bindFlags = 0;
    };

    struct AvailabilityInfo
    {
        bool observedThisFrame = false;
        bool producedThisFrame = false;
        bool consumedThisFrame = false;
        bool currentFrameAvailable = false;
        bool previousFrameAvailable = false;

        std::uint64_t frame = 0;
        std::uint64_t lastObservedFrame = 0;
        std::uint64_t lastProducedFrame = 0;
        std::uint64_t lastConsumedFrame = 0;
        std::uint64_t contentEpoch = 0;

        ContentEpochKind epochKind =
            ContentEpochKind::Unknown;

        EnvironmentClass environmentScope =
            EnvironmentClass::UnknownTransition;

        stage_scheduler::Boundary validFromStage =
            stage_scheduler::Boundary::Unknown;

        stage_scheduler::Boundary validUntilStage =
            stage_scheduler::Boundary::Unknown;
    };

    struct SemanticView
    {
        Semantic semantic = Semantic::SceneColorHDR;
        QueryMode queryMode = QueryMode::CurrentFrame;
        ResourceInfo resource{};
        AvailabilityInfo availability{};
        bool available = false;
    };

    struct Snapshot
    {
        std::uint64_t frame = 0;
        EnvironmentClass environment =
            EnvironmentClass::UnknownTransition;
        UINT screenWidth = 0;
        UINT screenHeight = 0;

        std::array<
            ResourceInfo,
            static_cast<std::size_t>(Semantic::Count)>
                resources{};

        std::array<
            AvailabilityInfo,
            static_cast<std::size_t>(Semantic::Count)>
                availability{};
    };

    void SetShader(
        ShaderStage stage,
        ID3D11DeviceChild* shader) noexcept;

    void SetShaderResources(
        ShaderStage stage,
        UINT startSlot,
        UINT count,
        ID3D11ShaderResourceView* const* resources) noexcept;

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

    void SetViewports(
        UINT count,
        const D3D11_VIEWPORT* viewports) noexcept;

    void ClearState() noexcept;

    void BeginDraw(
        std::uint64_t frame,
        std::uint64_t ordinal) noexcept;

    void EndDraw(
        std::uint64_t frame,
        std::uint64_t ordinal) noexcept;

    void BeginDispatch(
        std::uint64_t frame,
        std::uint64_t ordinal) noexcept;

    void EndDispatch(
        std::uint64_t frame,
        std::uint64_t ordinal) noexcept;

    void AfterCopy(
        std::uint64_t frame,
        std::uint64_t ordinal,
        ID3D11Resource* destination,
        ID3D11Resource* source) noexcept;

    void OnPresent(
        std::uint64_t frame,
        std::uint64_t drawIndexedActivity) noexcept;

    void OnResize() noexcept;

    void BeginEvidenceCapture(
        std::uint64_t startFrame,
        std::uint64_t endFrame) noexcept;

    void FinishEvidenceCapture(
        const std::wstring& suffix) noexcept;

    bool QuerySemantic(
        Semantic semantic,
        QueryMode mode,
        std::uint64_t frame,
        stage_scheduler::Boundary atStage,
        SemanticView* outView) noexcept;

    Snapshot GetSnapshot() noexcept;

    const char* SemanticName(Semantic semantic) noexcept;
    const char* ConfidenceName(Confidence confidence) noexcept;
    const char* EnvironmentName(EnvironmentClass environment) noexcept;
    const char* QueryModeName(QueryMode mode) noexcept;
    const char* ContentEpochName(ContentEpochKind epoch) noexcept;
}
