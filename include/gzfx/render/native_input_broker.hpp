#pragma once

#include "gzfx/render/renderer_execution_service.hpp"
#include "gzfx/render/semantic_authority.hpp"

#include <d3d11.h>

#include <cstdint>

namespace gzfx::render::native_input
{
    enum class Failure : std::uint32_t
    {
        None = 0,
        MissingExecutionService,
        MissingSwapChain,
        MissingSemantic,
        ResourceUnavailable,
        ResourceIdentityMismatch,
        ResourceShapeInvalid,
        ResourceFormatInvalid,
        ViewCreationFailed,
        CameraUnavailable,
        CameraInvalid,
        DeviceGenerationMismatch
    };

    struct SemanticInputView
    {
        semantic_authority::Semantic semantic =
            semantic_authority::Semantic::SceneColorHDR;

        std::uint64_t frame = 0;
        std::uint64_t semanticGeneration = 0;
        std::uint64_t contractToken = 0;
        std::uint64_t deviceGeneration = 0;
        std::uint64_t surfaceGeneration = 0;

        stage_scheduler::Boundary stage =
            stage_scheduler::Boundary::Unknown;

        semantic_authority::EnvironmentClass environment =
            semantic_authority::EnvironmentClass::UnknownTransition;

        semantic_authority::ContentEpochKind contentEpoch =
            semantic_authority::ContentEpochKind::Unknown;

        ID3D11Texture2D* texture = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;

        D3D11_TEXTURE2D_DESC desc{};
        DXGI_FORMAT viewFormat = DXGI_FORMAT_UNKNOWN;
        std::uintptr_t canonicalIdentity = 0;
        bool available = false;
    };

    struct GeometryInputs
    {
        std::uint64_t frame = 0;
        std::uint64_t deviceGeneration = 0;
        std::uint64_t surfaceGeneration = 0;

        SemanticInputView mainDepth{};
        SemanticInputView motion{};
        SemanticInputView nativeNormal{};

        ID3D11Buffer* cameraBuffer = nullptr;
        D3D11_BUFFER_DESC cameraDesc{};
        std::uintptr_t cameraIdentity = 0;
        std::uint64_t cameraGeneration = 0;

        UINT width = 0;
        UINT height = 0;

        semantic_authority::EnvironmentClass
            previousCompletedEnvironment =
                semantic_authority::EnvironmentClass::UnknownTransition;

        bool valid = false;
    };

    bool AcquireGeometryInputs(
        std::uint64_t frame,
        stage_scheduler::Boundary stage,
        const execution::ExecutionLease& executionLease,
        GeometryInputs* outInputs,
        Failure* outFailure) noexcept;

    void ReleaseGeometryInputs(
        GeometryInputs& inputs) noexcept;

    void Reset() noexcept;

    const char* FailureName(Failure failure) noexcept;
}