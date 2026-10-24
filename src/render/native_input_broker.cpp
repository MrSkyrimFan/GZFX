#include "gzfx/render/native_input_broker.hpp"

#include "gzfx/diagnostics/resource_intelligence.hpp"

#include <array>
#include <mutex>

namespace gzfx::render::native_input
{
    namespace
    {
        struct CachedView
        {
            std::uintptr_t identity = 0;
            std::uint64_t semanticGeneration = 0;
            std::uint64_t deviceGeneration = 0;
            DXGI_FORMAT viewFormat = DXGI_FORMAT_UNKNOWN;
            ID3D11Texture2D* texture = nullptr;
            ID3D11ShaderResourceView* srv = nullptr;
            D3D11_TEXTURE2D_DESC desc{};
        };

        std::mutex g_mutex;
        std::array<
            CachedView,
            static_cast<std::size_t>(
                semantic_authority::Semantic::Count)>
                    g_cache{};

        std::uintptr_t g_cameraIdentity = 0;
        std::uint64_t g_cameraGeneration = 0;
        std::uint64_t g_deviceGeneration = 0;

        std::size_t Index(
            const semantic_authority::Semantic semantic) noexcept
        {
            return static_cast<std::size_t>(semantic);
        }

        std::uintptr_t CanonicalId(
            IUnknown* const value) noexcept
        {
            if (value == nullptr)
            {
                return 0;
            }

            IUnknown* identity = nullptr;

            if (FAILED(
                    value->QueryInterface(
                        __uuidof(IUnknown),
                        reinterpret_cast<void**>(&identity))) ||
                identity == nullptr)
            {
                return reinterpret_cast<std::uintptr_t>(value);
            }

            const auto result =
                reinterpret_cast<std::uintptr_t>(identity);

            identity->Release();
            return result;
        }

        void ReleaseCached(
            CachedView& value) noexcept
        {
            if (value.srv != nullptr)
            {
                value.srv->Release();
            }

            if (value.texture != nullptr)
            {
                value.texture->Release();
            }

            value = {};
        }

        void ResetLocked() noexcept
        {
            for (auto& item : g_cache)
            {
                ReleaseCached(item);
            }

            g_cameraIdentity = 0;
            g_cameraGeneration = 0;
        }

        bool ValidateTextureShape(
            const semantic_authority::Semantic semantic,
            const D3D11_TEXTURE2D_DESC& desc,
            const UINT width,
            const UINT height,
            DXGI_FORMAT* const viewFormat) noexcept
        {
            if (viewFormat == nullptr ||
                desc.Width != width ||
                desc.Height != height ||
                desc.SampleDesc.Count != 1)
            {
                return false;
            }

            switch (semantic)
            {
            case semantic_authority::Semantic::MainDepthCurrent:
                if (desc.Format != DXGI_FORMAT_R24G8_TYPELESS)
                {
                    return false;
                }

                *viewFormat =
                    DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
                return true;

            case semantic_authority::Semantic::MotionVectors:
                if (desc.Format != DXGI_FORMAT_R16G16_FLOAT)
                {
                    return false;
                }

                *viewFormat = desc.Format;
                return true;

            case semantic_authority::Semantic::NativeNormalMask:
                if (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM)
                {
                    return false;
                }

                *viewFormat = desc.Format;
                return true;

            default:
                return false;
            }
        }

        bool BuildInput(
            ID3D11Device* const device,
            const std::uint64_t frame,
            const std::uint64_t deviceGeneration,
            const std::uint64_t surfaceGeneration,
            const stage_scheduler::Boundary stage,
            const semantic_authority::Semantic semantic,
            ID3D11Texture2D* const liveTexture,
            const D3D11_TEXTURE2D_DESC& liveDesc,
            const UINT width,
            const UINT height,
            SemanticInputView* const out,
            Failure* const failure) noexcept
        {
            semantic_authority::SemanticView authority{};

            if (!semantic_authority::QuerySemantic(
                    semantic,
                    semantic_authority::QueryMode::CurrentFrame,
                    frame,
                    stage,
                    &authority))
            {
                *failure = Failure::MissingSemantic;
                return false;
            }

            if (liveTexture == nullptr)
            {
                *failure = Failure::ResourceUnavailable;
                return false;
            }

            const auto identity =
                CanonicalId(liveTexture);

            if (identity == 0 ||
                identity != authority.resource.identity)
            {
                *failure = Failure::ResourceIdentityMismatch;
                return false;
            }

            DXGI_FORMAT viewFormat = DXGI_FORMAT_UNKNOWN;

            if (!ValidateTextureShape(
                    semantic,
                    liveDesc,
                    width,
                    height,
                    &viewFormat))
            {
                *failure = Failure::ResourceFormatInvalid;
                return false;
            }

            auto& cached = g_cache[Index(semantic)];

            const bool rebuild =
                cached.texture == nullptr ||
                cached.srv == nullptr ||
                cached.identity != identity ||
                cached.semanticGeneration !=
                    authority.resource.generation ||
                cached.deviceGeneration != deviceGeneration ||
                cached.viewFormat != viewFormat;

            if (rebuild)
            {
                ReleaseCached(cached);

                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
                srvDesc.Format = viewFormat;
                srvDesc.ViewDimension =
                    D3D11_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MostDetailedMip = 0;
                srvDesc.Texture2D.MipLevels = 1;

                ID3D11ShaderResourceView* srv = nullptr;

                if (FAILED(
                        device->CreateShaderResourceView(
                            liveTexture,
                            &srvDesc,
                            &srv)) ||
                    srv == nullptr)
                {
                    *failure = Failure::ViewCreationFailed;
                    return false;
                }

                cached.identity = identity;
                cached.semanticGeneration =
                    authority.resource.generation;
                cached.deviceGeneration = deviceGeneration;
                cached.viewFormat = viewFormat;
                cached.texture = liveTexture;
                cached.texture->AddRef();
                cached.srv = srv;
                cached.desc = liveDesc;
            }

            out->semantic = semantic;
            out->frame = frame;
            out->semanticGeneration =
                authority.resource.generation;
            out->deviceGeneration = deviceGeneration;
            out->surfaceGeneration = surfaceGeneration;
            out->stage = stage;
            out->environment =
                authority.availability.environmentScope;
            out->contentEpoch =
                authority.availability.epochKind;
            out->texture = cached.texture;
            out->texture->AddRef();
            out->srv = cached.srv;
            out->srv->AddRef();
            out->desc = cached.desc;
            out->viewFormat = cached.viewFormat;
            out->canonicalIdentity = identity;
            out->available = true;

            return true;
        }
    }

    bool AcquireGeometryInputs(
        const std::uint64_t frame,
        const stage_scheduler::Boundary stage,
        const execution::ExecutionLease& executionLease,
        GeometryInputs* const outInputs,
        Failure* const outFailure) noexcept
    {
        if (outInputs == nullptr)
        {
            return false;
        }

        *outInputs = {};

        Failure failure = Failure::None;

        auto fail =
            [&](const Failure value)
            {
                failure = value;

                if (outFailure != nullptr)
                {
                    *outFailure = failure;
                }

                ReleaseGeometryInputs(*outInputs);
                return false;
            };

        try
        {
            if (!executionLease.valid ||
                executionLease.device == nullptr ||
                executionLease.immediate == nullptr)
            {
                return fail(
                    Failure::MissingExecutionService);
            }

            if (executionLease.swapChain == nullptr)
            {
                return fail(Failure::MissingSwapChain);
            }

            diagnostics::resource_intelligence::LiveInputs live{};

            if (!diagnostics::resource_intelligence::AcquireLiveInputs(
                    executionLease.swapChain,
                    live))
            {
                return fail(Failure::ResourceUnavailable);
            }

            struct LiveGuard
            {
                diagnostics::resource_intelligence::LiveInputs* value;
                ~LiveGuard()
                {
                    diagnostics::resource_intelligence::ReleaseLiveInputs(
                        *value);
                }
            } guard{ &live };

            const UINT width = live.depthDesc.Width;
            const UINT height = live.depthDesc.Height;

            if (width == 0 || height == 0)
            {
                return fail(Failure::ResourceShapeInvalid);
            }

            std::scoped_lock lock(g_mutex);

            if (g_deviceGeneration !=
                executionLease.deviceGeneration)
            {
                ResetLocked();
                g_deviceGeneration =
                    executionLease.deviceGeneration;
            }

            if (!BuildInput(
                    executionLease.device,
                    frame,
                    executionLease.deviceGeneration,
                    executionLease.surfaceGeneration,
                    stage,
                    semantic_authority::Semantic::MainDepthCurrent,
                    live.depthTexture,
                    live.depthDesc,
                    width,
                    height,
                    &outInputs->mainDepth,
                    &failure))
            {
                return fail(failure);
            }

            if (!BuildInput(
                    executionLease.device,
                    frame,
                    executionLease.deviceGeneration,
                    executionLease.surfaceGeneration,
                    stage,
                    semantic_authority::Semantic::MotionVectors,
                    live.motionVectorTexture,
                    live.motionVectorDesc,
                    width,
                    height,
                    &outInputs->motion,
                    &failure))
            {
                return fail(failure);
            }

            if (!BuildInput(
                    executionLease.device,
                    frame,
                    executionLease.deviceGeneration,
                    executionLease.surfaceGeneration,
                    stage,
                    semantic_authority::Semantic::NativeNormalMask,
                    live.nativeNormalTexture,
                    live.nativeNormalDesc,
                    width,
                    height,
                    &outInputs->nativeNormal,
                    &failure))
            {
                return fail(failure);
            }

            if (live.cameraBuffer == nullptr)
            {
                return fail(Failure::CameraUnavailable);
            }

            D3D11_BUFFER_DESC cameraDesc{};
            live.cameraBuffer->GetDesc(&cameraDesc);

            if (cameraDesc.ByteWidth != 720 ||
                (cameraDesc.BindFlags &
                    D3D11_BIND_CONSTANT_BUFFER) == 0)
            {
                return fail(Failure::CameraInvalid);
            }

            const auto cameraIdentity =
                CanonicalId(live.cameraBuffer);

            if (cameraIdentity == 0)
            {
                return fail(Failure::CameraInvalid);
            }

            if (cameraIdentity != g_cameraIdentity)
            {
                g_cameraIdentity = cameraIdentity;
                ++g_cameraGeneration;
            }

            outInputs->cameraBuffer = live.cameraBuffer;
            outInputs->cameraBuffer->AddRef();
            outInputs->cameraDesc = cameraDesc;
            outInputs->cameraIdentity = cameraIdentity;
            outInputs->cameraGeneration = g_cameraGeneration;
            outInputs->frame = frame;
            outInputs->deviceGeneration =
                executionLease.deviceGeneration;
            outInputs->surfaceGeneration =
                executionLease.surfaceGeneration;
            outInputs->width = width;
            outInputs->height = height;
            outInputs->previousCompletedEnvironment =
                semantic_authority::GetSnapshot().environment;
            outInputs->valid = true;

            if (outFailure != nullptr)
            {
                *outFailure = Failure::None;
            }

            return true;
        }
        catch (...)
        {
            return fail(Failure::ResourceUnavailable);
        }
    }

    void ReleaseGeometryInputs(
        GeometryInputs& inputs) noexcept
    {
        auto releaseView =
            [](SemanticInputView& value)
            {
                if (value.srv != nullptr)
                {
                    value.srv->Release();
                }

                if (value.texture != nullptr)
                {
                    value.texture->Release();
                }

                value = {};
            };

        releaseView(inputs.mainDepth);
        releaseView(inputs.motion);
        releaseView(inputs.nativeNormal);

        if (inputs.cameraBuffer != nullptr)
        {
            inputs.cameraBuffer->Release();
        }

        inputs = {};
    }

    void Reset() noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);
            ResetLocked();
            g_deviceGeneration = 0;
        }
        catch (...)
        {
        }
    }

    const char* FailureName(
        const Failure failure) noexcept
    {
        switch (failure)
        {
        case Failure::None:
            return "none";
        case Failure::MissingExecutionService:
            return "missing_execution_service";
        case Failure::MissingSwapChain:
            return "missing_swap_chain";
        case Failure::MissingSemantic:
            return "missing_semantic";
        case Failure::ResourceUnavailable:
            return "resource_unavailable";
        case Failure::ResourceIdentityMismatch:
            return "resource_identity_mismatch";
        case Failure::ResourceShapeInvalid:
            return "resource_shape_invalid";
        case Failure::ResourceFormatInvalid:
            return "resource_format_invalid";
        case Failure::ViewCreationFailed:
            return "view_creation_failed";
        case Failure::CameraUnavailable:
            return "camera_unavailable";
        case Failure::CameraInvalid:
            return "camera_invalid";
        case Failure::DeviceGenerationMismatch:
            return "device_generation_mismatch";
        default:
            return "unknown";
        }
    }
}