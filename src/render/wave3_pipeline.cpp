#include "gzfx/render/wave3_pipeline.hpp"

#include "gzfx/paths.hpp"
#include "gzfx/render/native_input_broker.hpp"
#include "gzfx/render/renderer_execution_service.hpp"
#include "gzfx/render/semantic_authority.hpp"
#include "gzfx/render/stage_scheduler.hpp"
#include "gzfx/render/wave3_math.hpp"
#include "gzfx/version.hpp"

#include <Windows.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace gzfx::render::wave3
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr UINT kThreadsX = 8;
        constexpr UINT kThreadsY = 8;
        constexpr std::size_t kGpuTimerSlots = 6;
        constexpr std::size_t kGpuTimestampCount = 5;
        constexpr std::size_t kCameraHistoryElements = 16;
        constexpr std::size_t kCutStateElements = 3;

        enum class ResetReason : std::uint32_t
        {
            None = 0,
            FirstValidFrame,
            FrameGap,
            DeviceGenerationChanged,
            SurfaceGenerationChanged,
            ResourceDimensionsChanged,
            SemanticGenerationChanged,
            CameraGenerationChanged,
            EnvironmentTransition,
            RuntimeRebuild,
            MissingRequiredInput,
            ExecutionFailure,
            Count
        };

        struct Parameters
        {
            std::uint32_t resourceWidth = 0;
            std::uint32_t resourceHeight = 0;
            std::uint32_t mipCount = 0;
            std::uint32_t frameLow = 0;

            float depthDisocclusion = 0.02f;
            float normalDotThreshold = 0.75f;
            float maxHistoryFrames = 16.0f;
            float motionHighPixels = 32.0f;

            std::uint32_t globalHistoryValid = 0;
            std::uint32_t historyGenerationLow = 0;
            std::uint32_t debugFlags = 0;
            std::uint32_t reductionMode = 0;

            float cameraTranslationCut = 4096.0f;
            float cameraRotationCutDegrees = 75.0f;
            float cameraMatrixContinuityCut = 0.02f;
            float cameraProjectionCut = 0.05f;
        };

        static_assert(sizeof(Parameters) == 64);

        struct TextureView
        {
            ID3D11Texture2D* texture = nullptr;
            ID3D11ShaderResourceView* srv = nullptr;
            ID3D11UnorderedAccessView* uav = nullptr;
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        };

        struct HistorySet
        {
            TextureView depth{};
            TextureView worldNormal{};
            TextureView confidence{};

            ID3D11Buffer* camera = nullptr;
            ID3D11ShaderResourceView* cameraSrv = nullptr;
            ID3D11UnorderedAccessView* cameraUav = nullptr;
        };

        struct GpuTimerSlot
        {
            ID3D11Query* disjoint = nullptr;
            std::array<
                ID3D11Query*,
                kGpuTimestampCount> timestamps{};
            bool pending = false;
            std::uint64_t frame = 0;
        };

        struct GpuTiming
        {
            bool valid = false;
            std::uint64_t frame = 0;
            double depthBaseMs = 0.0;
            double depthTailMs = 0.0;
            double temporalMs = 0.0;
            double cameraStoreMs = 0.0;
            double totalMs = 0.0;
        };

        struct Runtime
        {
            ID3D11Device* device = nullptr;
            ID3D11DeviceContext* deferred = nullptr;

            ID3D11ComputeShader* depthBaseShader = nullptr;
            ID3D11ComputeShader* depthReduceShader = nullptr;
            ID3D11ComputeShader* cameraCutShader = nullptr;
            ID3D11ComputeShader* temporalShader = nullptr;
            ID3D11ComputeShader* cameraStoreShader = nullptr;

            ID3D11SamplerState* linearSampler = nullptr;
            ID3D11Buffer* parameters = nullptr;

            ID3D11Texture2D* hierarchy = nullptr;
            ID3D11ShaderResourceView* hierarchySrv = nullptr;
            std::vector<ID3D11ShaderResourceView*> hierarchyMipSrvs;
            std::vector<ID3D11UnorderedAccessView*> hierarchyMipUavs;

            std::array<HistorySet, 2> history{};

            ID3D11Buffer* cutState = nullptr;
            ID3D11ShaderResourceView* cutStateSrv = nullptr;
            ID3D11UnorderedAccessView* cutStateUav = nullptr;

            TextureView debugMask{};

            std::array<GpuTimerSlot, kGpuTimerSlots> gpuTimers{};
            std::size_t gpuTimerCursor = 0;
            GpuTiming latestTiming{};

            UINT width = 0;
            UINT height = 0;
            UINT mipCount = 0;
            DXGI_FORMAT normalHistoryFormat = DXGI_FORMAT_UNKNOWN;
            DXGI_FORMAT confidenceFormat = DXGI_FORMAT_UNKNOWN;
            std::uint64_t deviceGeneration = 0;
        };

        struct HistoryState
        {
            bool valid = false;
            std::uint64_t generation = 0;
            std::uint64_t lastFrame = 0;
            std::uint64_t deviceGeneration = 0;
            std::uint64_t surfaceGeneration = 0;
            UINT width = 0;
            UINT height = 0;
            std::uint64_t depthGeneration = 0;
            std::uint64_t motionGeneration = 0;
            std::uint64_t normalGeneration = 0;
            std::uint64_t cameraGeneration = 0;
            semantic_authority::EnvironmentClass environment =
                semantic_authority::EnvironmentClass::UnknownTransition;
            std::size_t readIndex = 0;
            ResetReason lastReset = ResetReason::FirstValidFrame;
        };

        struct PendingCapture
        {
            bool active = false;
            std::uint64_t frame = 0;
            std::uint32_t index = 0;
            std::vector<float> gzfxDepth;
            UINT width = 0;
            UINT height = 0;
            fs::path directory;
        };

        std::mutex g_mutex;
        std::once_flag g_initializeOnce;
        bool g_initialized = false;
        Runtime g_runtime{};
        HistoryState g_history{};
        PendingCapture g_capture{};
        bool g_captureKeyDown = false;
        std::uint32_t g_captureIndex = 0;
        std::uint64_t g_executeCount = 0;
        std::uint64_t g_skipCount = 0;
        std::uint64_t g_lastStatusFrame = 0;

        std::string HResultText(const HRESULT value)
        {
            std::ostringstream stream;
            stream
                << "0x"
                << std::hex
                << std::uppercase
                << static_cast<std::uint32_t>(value);
            return stream.str();
        }

        std::string PointerText(const void* value)
        {
            std::ostringstream stream;
            stream
                << "0x"
                << std::hex
                << std::uppercase
                << reinterpret_cast<std::uintptr_t>(value);
            return stream.str();
        }

        std::uintptr_t CanonicalId(IUnknown* value) noexcept
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

        const char* ResetReasonName(
            const ResetReason reason) noexcept
        {
            switch (reason)
            {
            case ResetReason::None:
                return "none";
            case ResetReason::FirstValidFrame:
                return "first_valid_frame";
            case ResetReason::FrameGap:
                return "frame_gap";
            case ResetReason::DeviceGenerationChanged:
                return "device_generation_changed";
            case ResetReason::SurfaceGenerationChanged:
                return "surface_generation_changed";
            case ResetReason::ResourceDimensionsChanged:
                return "resource_dimensions_changed";
            case ResetReason::SemanticGenerationChanged:
                return "semantic_generation_changed";
            case ResetReason::CameraGenerationChanged:
                return "camera_generation_changed";
            case ResetReason::EnvironmentTransition:
                return "environment_transition";
            case ResetReason::RuntimeRebuild:
                return "runtime_rebuild";
            case ResetReason::MissingRequiredInput:
                return "missing_required_input";
            case ResetReason::ExecutionFailure:
                return "execution_failure";
            case ResetReason::Count:
            default:
                return "unknown";
            }
        }

        void WriteText(
            const fs::path& path,
            const std::string& text)
        {
            std::error_code error;
            fs::create_directories(path.parent_path(), error);

            if (error)
            {
                return;
            }

            std::ofstream out(
                path,
                std::ios::out | std::ios::trunc);

            if (out)
            {
                out << text;
            }
        }

        fs::path DiagnosticsRoot()
        {
            const fs::path root = paths::DiagnosticsRoot();

            if (root.empty())
            {
                return {};
            }

            return root / L"Wave3";
        }

        void ReleaseTextureView(TextureView& value) noexcept
        {
            if (value.uav != nullptr)
            {
                value.uav->Release();
            }

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

        void ReleaseHistorySet(HistorySet& value) noexcept
        {
            ReleaseTextureView(value.confidence);
            ReleaseTextureView(value.worldNormal);
            ReleaseTextureView(value.depth);

            if (value.cameraUav != nullptr)
            {
                value.cameraUav->Release();
            }

            if (value.cameraSrv != nullptr)
            {
                value.cameraSrv->Release();
            }

            if (value.camera != nullptr)
            {
                value.camera->Release();
            }

            value = {};
        }

        void ReleaseRuntime() noexcept
        {
            for (auto& timer : g_runtime.gpuTimers)
            {
                for (auto*& timestamp : timer.timestamps)
                {
                    if (timestamp != nullptr)
                    {
                        timestamp->Release();
                        timestamp = nullptr;
                    }
                }

                if (timer.disjoint != nullptr)
                {
                    timer.disjoint->Release();
                    timer.disjoint = nullptr;
                }

                timer.pending = false;
            }

            ReleaseTextureView(g_runtime.debugMask);

            if (g_runtime.cutStateUav != nullptr)
            {
                g_runtime.cutStateUav->Release();
            }

            if (g_runtime.cutStateSrv != nullptr)
            {
                g_runtime.cutStateSrv->Release();
            }

            if (g_runtime.cutState != nullptr)
            {
                g_runtime.cutState->Release();
            }

            for (auto& history : g_runtime.history)
            {
                ReleaseHistorySet(history);
            }

            for (auto*& view : g_runtime.hierarchyMipUavs)
            {
                if (view != nullptr)
                {
                    view->Release();
                }
            }

            for (auto*& view : g_runtime.hierarchyMipSrvs)
            {
                if (view != nullptr)
                {
                    view->Release();
                }
            }

            g_runtime.hierarchyMipUavs.clear();
            g_runtime.hierarchyMipSrvs.clear();

            if (g_runtime.hierarchySrv != nullptr)
            {
                g_runtime.hierarchySrv->Release();
            }

            if (g_runtime.hierarchy != nullptr)
            {
                g_runtime.hierarchy->Release();
            }

            if (g_runtime.parameters != nullptr)
            {
                g_runtime.parameters->Release();
            }

            if (g_runtime.linearSampler != nullptr)
            {
                g_runtime.linearSampler->Release();
            }

            if (g_runtime.cameraStoreShader != nullptr)
            {
                g_runtime.cameraStoreShader->Release();
            }

            if (g_runtime.temporalShader != nullptr)
            {
                g_runtime.temporalShader->Release();
            }

            if (g_runtime.cameraCutShader != nullptr)
            {
                g_runtime.cameraCutShader->Release();
            }

            if (g_runtime.depthReduceShader != nullptr)
            {
                g_runtime.depthReduceShader->Release();
            }

            if (g_runtime.depthBaseShader != nullptr)
            {
                g_runtime.depthBaseShader->Release();
            }

            if (g_runtime.deferred != nullptr)
            {
                g_runtime.deferred->Release();
            }

            if (g_runtime.device != nullptr)
            {
                g_runtime.device->Release();
            }

            g_runtime = {};
        }

        bool CompileShader(
            const char* source,
            const char* entry,
            ID3DBlob** blob,
            std::string& error)
        {
            ID3DBlob* errors = nullptr;

            const HRESULT result =
                D3DCompile(
                    source,
                    std::strlen(source),
                    "GZFXWave3",
                    nullptr,
                    nullptr,
                    entry,
                    "cs_5_0",
                    D3DCOMPILE_OPTIMIZATION_LEVEL3,
                    0,
                    blob,
                    &errors);

            if (FAILED(result))
            {
                if (errors != nullptr)
                {
                    error.assign(
                        static_cast<const char*>(
                            errors->GetBufferPointer()),
                        errors->GetBufferSize());
                }
                else
                {
                    error = "D3DCompile failed without diagnostics";
                }
            }

            if (errors != nullptr)
            {
                errors->Release();
            }

            return SUCCEEDED(result);
        }

        const char* ShaderSource()
        {
            return R"GZFX(
cbuffer Wave3Parameters : register(b0)
{
    uint2 ResourceSize;
    uint MipCount;
    uint FrameLow;

    float DepthDisocclusion;
    float NormalDotThreshold;
    float MaxHistoryFrames;
    float MotionHighPixels;

    uint GlobalHistoryValid;
    uint HistoryGenerationLow;
    uint DebugFlags;
    uint ReductionMode;

    float CameraTranslationCut;
    float CameraRotationCutDegrees;
    float CameraMatrixContinuityCut;
    float CameraProjectionCut;
};

cbuffer SkyrimPerFrame : register(b12)
{
    row_major float4x4 CameraView : packoffset(c0);
    row_major float4x4 CameraProj : packoffset(c4);
    row_major float4x4 CameraViewProj : packoffset(c8);
    row_major float4x4 CameraViewProjUnjittered : packoffset(c12);
    row_major float4x4 CameraPreviousViewProjUnjittered : packoffset(c16);
    row_major float4x4 CameraProjUnjittered : packoffset(c20);
    row_major float4x4 CameraProjUnjitteredInverse : packoffset(c24);
    row_major float4x4 CameraViewInverse : packoffset(c28);
    row_major float4x4 CameraViewProjInverse : packoffset(c32);
    row_major float4x4 CameraProjInverse : packoffset(c36);
    float4 CameraPosAdjust : packoffset(c40);
    float4 CameraPreviousPosAdjust : packoffset(c41);
    float4 FrameParams : packoffset(c42);
    float4 DynamicResolutionParams1 : packoffset(c43);
    float4 DynamicResolutionParams2 : packoffset(c44);
};

SamplerState LinearSampler : register(s0);

float2 CurrentRatio()
{
    return DynamicResolutionParams1.xy;
}

float2 PreviousRatio()
{
    return DynamicResolutionParams1.zw;
}

uint2 ActiveSize(float2 ratio)
{
    float2 safeRatio = max(ratio, float2(0.000001, 0.000001));
    return max(
        uint2(1, 1),
        uint2(
            floor(
                float2(ResourceSize) *
                safeRatio +
                0.5)));
}

bool RatioValid(float2 ratio)
{
    return
        all(isfinite(ratio)) &&
        all(ratio > float2(0.0, 0.0)) &&
        all(ratio <= float2(1.5, 1.5));
}

float2 LogicalUv(uint2 pixel, uint2 activeSize)
{
    return
        (float2(pixel) + 0.5) /
        max(float2(activeSize), 1.0);
}

float3 ReconstructViewRay(
    float2 logicalUv,
    row_major float4x4 projectionInverse)
{
    float4 clip =
        float4(
            2.0 * float2(
                logicalUv.x,
                1.0 - logicalUv.y) - 1.0,
            1.0,
            1.0);

    float4 viewH =
        mul(
            projectionInverse,
            clip);

    if (abs(viewH.w) < 0.000001)
    {
        return 0.0;
    }

    return viewH.xyz / viewH.w;
}

float3 ReconstructViewFromLinearDepth(
    float2 logicalUv,
    float positiveViewDepth,
    row_major float4x4 projectionInverse)
{
    float3 ray =
        ReconstructViewRay(
            logicalUv,
            projectionInverse);

    if (abs(ray.z) < 0.000001)
    {
        return 0.0;
    }

    return ray *
        (positiveViewDepth / ray.z);
}

float3 ReconstructWorldFromLinearDepth(
    float2 logicalUv,
    float positiveViewDepth,
    row_major float4x4 projectionInverse,
    row_major float4x4 viewInverse,
    float3 positionAdjust)
{
    float3 view =
        ReconstructViewFromLinearDepth(
            logicalUv,
            positiveViewDepth,
            projectionInverse);

    float3 world =
        mul(
            viewInverse,
            float4(view, 1.0)).xyz;

    return world + positionAdjust;
}

float3 DecodeNativeNormal(float2 encoded)
{
    float2 e = encoded - 0.5;

    float nz =
        clamp(
            8.0 * dot(e, e) - 1.0,
            -1.0,
            1.0);

    float q =
        sqrt(
            max(
                8.0 - 8.0 * nz,
                0.0));

    return normalize(
        float3(
            e * q,
            nz));
}

float2 EncodeOct(float3 n)
{
    n /=
        max(
            abs(n.x) +
            abs(n.y) +
            abs(n.z),
            0.000001);

    float2 e = n.xy;

    if (n.z < 0.0)
    {
        e =
            (1.0 - abs(e.yx)) *
            float2(
                e.x >= 0.0 ? 1.0 : -1.0,
                e.y >= 0.0 ? 1.0 : -1.0);
    }

    return e * 0.5 + 0.5;
}

float3 DecodeOct(float2 e)
{
    float2 f = e * 2.0 - 1.0;
    float3 n =
        float3(
            f.x,
            f.y,
            1.0 - abs(f.x) - abs(f.y));

    if (n.z < 0.0)
    {
        float2 folded =
            (1.0 - abs(n.yx)) *
            float2(
                n.x >= 0.0 ? 1.0 : -1.0,
                n.y >= 0.0 ? 1.0 : -1.0);

        n.xy = folded;
    }

    return normalize(n);
}

Texture2D<float> RawDepth : register(t0);
RWTexture2D<float> DepthMip0 : register(u0);

[numthreads(8, 8, 1)]
void DepthBaseCS(uint3 dispatchId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchId.xy;

    if (any(pixel >= ResourceSize))
    {
        return;
    }

    float2 ratio = CurrentRatio();
    uint2 activeSize = ActiveSize(ratio);

    if (!RatioValid(ratio) ||
        any(pixel >= activeSize))
    {
        DepthMip0[pixel] = 0.0;
        return;
    }

    float rawDepth =
        RawDepth.Load(
            int3(pixel, 0));

    if (!isfinite(rawDepth) ||
        rawDepth >= 0.999999)
    {
        DepthMip0[pixel] = 0.0;
        return;
    }

    float2 uv =
        LogicalUv(
            pixel,
            activeSize);

    float4 clip =
        float4(
            2.0 * float2(
                uv.x,
                1.0 - uv.y) - 1.0,
            rawDepth,
            1.0);

    float4 viewH =
        mul(
            CameraProjInverse,
            clip);

    if (abs(viewH.w) < 0.000001)
    {
        DepthMip0[pixel] = 0.0;
        return;
    }

    float viewDepth =
        viewH.z / viewH.w;

    DepthMip0[pixel] =
        isfinite(viewDepth) &&
        viewDepth > 0.0
            ? viewDepth
            : 0.0;
}

Texture2D<float> SourceDepth : register(t0);
RWTexture2D<float> ReducedDepth : register(u0);

float ReduceDepth4(float values[4], uint validCount)
{
    if (validCount == 0)
    {
        return 0.0;
    }

    if (ReductionMode == 1)
    {
        float sum = 0.0;

        [unroll]
        for (uint i = 0; i < 4; ++i)
        {
            if (values[i] > 0.0)
            {
                sum += values[i];
            }
        }

        return sum / validCount;
    }

    if (ReductionMode == 2)
    {
        float value = 3.402823466e+38F;

        [unroll]
        for (uint i = 0; i < 4; ++i)
        {
            if (values[i] > 0.0)
            {
                value = min(value, values[i]);
            }
        }

        return value;
    }

    if (ReductionMode == 3)
    {
        float value = 0.0;

        [unroll]
        for (uint i = 0; i < 4; ++i)
        {
            value = max(value, values[i]);
        }

        return value;
    }

    float farDepth = 0.0;

    [unroll]
    for (uint i = 0; i < 4; ++i)
    {
        farDepth = max(farDepth, values[i]);
    }

    float scale =
        max(
            farDepth * 0.02,
            0.001);

    float weighted = 0.0;
    float weightTotal = 0.0;

    [unroll]
    for (uint i = 0; i < 4; ++i)
    {
        float depth = values[i];

        if (depth <= 0.0)
        {
            continue;
        }

        float weight =
            exp2(
                -abs(farDepth - depth) /
                scale);

        weighted += depth * weight;
        weightTotal += weight;
    }

    return
        weightTotal > 0.0
            ? weighted / weightTotal
            : farDepth;
}

[numthreads(8, 8, 1)]
void DepthReduceCS(uint3 dispatchId : SV_DispatchThreadID)
{
    uint2 destination = dispatchId.xy;
    uint destinationWidth = 0;
    uint destinationHeight = 0;
    ReducedDepth.GetDimensions(
        destinationWidth,
        destinationHeight);

    if (destination.x >= destinationWidth ||
        destination.y >= destinationHeight)
    {
        return;
    }

    uint sourceWidth = 0;
    uint sourceHeight = 0;
    SourceDepth.GetDimensions(
        sourceWidth,
        sourceHeight);

    uint2 base = destination * 2;
    float values[4] = {0.0, 0.0, 0.0, 0.0};
    uint validCount = 0;

    [unroll]
    for (uint y = 0; y < 2; ++y)
    {
        [unroll]
        for (uint x = 0; x < 2; ++x)
        {
            uint2 source = base + uint2(x, y);
            uint index = y * 2 + x;

            if (source.x < sourceWidth &&
                source.y < sourceHeight)
            {
                float depth =
                    SourceDepth.Load(
                        int3(source, 0));

                if (isfinite(depth) &&
                    depth > 0.0)
                {
                    values[index] = depth;
                    ++validCount;
                }
            }
        }
    }

    ReducedDepth[destination] =
        ReduceDepth4(
            values,
            validCount);
}

StructuredBuffer<float4> PreviousCamera : register(t0);
RWStructuredBuffer<float4> CutStateOut : register(u0);

row_major float4x4 PreviousViewInverse()
{
    row_major float4x4 m =
        float4x4(
            PreviousCamera[0],
            PreviousCamera[1],
            PreviousCamera[2],
            PreviousCamera[3]);
    return m;
}

row_major float4x4 PreviousProjectionInverse()
{
    row_major float4x4 m =
        float4x4(
            PreviousCamera[4],
            PreviousCamera[5],
            PreviousCamera[6],
            PreviousCamera[7]);
    return m;
}

row_major float4x4 PreviousViewProjUnjittered()
{
    row_major float4x4 m =
        float4x4(
            PreviousCamera[8],
            PreviousCamera[9],
            PreviousCamera[10],            PreviousCamera[11]);
    return m;
}

float MatrixMaxError(
    row_major float4x4 a,
    row_major float4x4 b)
{
    float error = 0.0;

    [unroll]
    for (uint row = 0; row < 4; ++row)
    {
        [unroll]
        for (uint column = 0; column < 4; ++column)
        {
            error =
                max(
                    error,
                    abs(a[row][column] - b[row][column]));
        }
    }

    return error;
}

[numthreads(1, 1, 1)]
void CameraCutCS(uint3 dispatchId : SV_DispatchThreadID)
{
    if (any(dispatchId != uint3(0, 0, 0)))
    {
        return;
    }

    uint reasons = 0;

    if (GlobalHistoryValid == 0)
    {
        reasons |= 1u;
    }

    float3 previousPosition =
        PreviousCamera[12].xyz;

    float translationDelta =
        length(
            CameraPosAdjust.xyz -
            previousPosition);

    if (GlobalHistoryValid != 0 &&
        translationDelta >
            CameraTranslationCut)
    {
        reasons |= 2u;
    }

    row_major float4x4 previousViewInverse =
        PreviousViewInverse();

    float3 currentForward =
        normalize(
            mul(
                CameraViewInverse,
                float4(0.0, 0.0, 1.0, 0.0)).xyz);

    float3 previousForward =
        normalize(
            mul(
                previousViewInverse,
                float4(0.0, 0.0, 1.0, 0.0)).xyz);

    float forwardDot =
        clamp(
            dot(
                currentForward,
                previousForward),
            -1.0,
            1.0);

    float rotationDegrees =
        degrees(acos(forwardDot));

    if (GlobalHistoryValid != 0 &&
        rotationDegrees >
            CameraRotationCutDegrees)
    {
        reasons |= 4u;
    }

    float continuityError =
        MatrixMaxError(
            CameraPreviousViewProjUnjittered,
            PreviousViewProjUnjittered());

    if (GlobalHistoryValid != 0 &&
        continuityError >
            CameraMatrixContinuityCut)
    {
        reasons |= 8u;
    }

    float projectionDelta = 0.0;

    [unroll]
    for (uint column = 0; column < 4; ++column)
    {
        projectionDelta =
            max(
                projectionDelta,
                abs(
                    CameraProj[0][column] -
                    PreviousCamera[14][column]));

        projectionDelta =
            max(
                projectionDelta,
                abs(
                    CameraProj[1][column] -
                    PreviousCamera[15][column]));
    }

    if (GlobalHistoryValid != 0 &&
        projectionDelta >
            CameraProjectionCut)
    {
        reasons |= 16u;
    }

    float2 currentRatio = CurrentRatio();
    float2 previousRatio = PreviousRatio();

    if (!RatioValid(currentRatio) ||
        !RatioValid(previousRatio))
    {
        reasons |= 32u;
    }

    uint2 active = ActiveSize(currentRatio);

    CutStateOut[0] =
        float4(
            asfloat(reasons),
            translationDelta,
            rotationDegrees,
            continuityError);

    CutStateOut[1] =
        float4(
            currentRatio,
            previousRatio);

    CutStateOut[2] =
        float4(
            projectionDelta,
            active.x,
            active.y,
            0.0);
}

Texture2D<float> CurrentLinearDepth : register(t0);
Texture2D<float2> NativeMotion : register(t1);
Texture2D<float4> NativeNormal : register(t2);
Texture2D<float> PreviousLinearDepth : register(t3);
Texture2D<float2> PreviousWorldNormal : register(t4);
Texture2D<float> PreviousConfidence : register(t5);
StructuredBuffer<float4> TemporalPreviousCamera : register(t6);
StructuredBuffer<float4> CutState : register(t7);

RWTexture2D<float> CurrentDepthHistory : register(u0);
RWTexture2D<float2> CurrentNormalHistory : register(u1);
RWTexture2D<float> CurrentConfidence : register(u2);
RWTexture2D<float4> DebugMask : register(u3);

row_major float4x4 TemporalPreviousViewInverse()
{
    row_major float4x4 m =
        float4x4(
            TemporalPreviousCamera[0],
            TemporalPreviousCamera[1],
            TemporalPreviousCamera[2],
            TemporalPreviousCamera[3]);
    return m;
}

row_major float4x4 TemporalPreviousProjectionInverse()
{
    row_major float4x4 m =
        float4x4(
            TemporalPreviousCamera[4],
            TemporalPreviousCamera[5],
            TemporalPreviousCamera[6],
            TemporalPreviousCamera[7]);
    return m;
}

[numthreads(8, 8, 1)]
void TemporalGeometryCS(uint3 dispatchId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchId.xy;

    if (any(pixel >= ResourceSize))
    {
        return;
    }

    float2 currentRatio = CurrentRatio();
    float2 previousRatio = PreviousRatio();
    uint2 currentActive = ActiveSize(currentRatio);

    if (!RatioValid(currentRatio) ||
        any(pixel >= currentActive))
    {
        CurrentDepthHistory[pixel] = 0.0;
        CurrentNormalHistory[pixel] = float2(0.5, 0.5);
        CurrentConfidence[pixel] = 0.0;
        DebugMask[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float currentDepth =
        CurrentLinearDepth.Load(
            int3(pixel, 0));

    if (!isfinite(currentDepth) ||
        currentDepth <= 0.0)
    {
        CurrentDepthHistory[pixel] = 0.0;
        CurrentNormalHistory[pixel] = float2(0.5, 0.5);
        CurrentConfidence[pixel] = 0.0;
        DebugMask[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float3 currentViewNormal =
        DecodeNativeNormal(
            NativeNormal.Load(
                int3(pixel, 0)).rg);

    float3 currentWorldNormal =
        normalize(
            mul(
                CameraViewInverse,
                float4(
                    currentViewNormal,
                    0.0)).xyz);

    CurrentDepthHistory[pixel] =
        currentDepth;

    CurrentNormalHistory[pixel] =
        EncodeOct(
            currentWorldNormal);

    uint cutReasons =
        asuint(
            CutState[0].x);

    if (GlobalHistoryValid == 0 ||
        cutReasons != 0 ||
        !RatioValid(previousRatio))
    {
        CurrentConfidence[pixel] = 0.0;
        DebugMask[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float2 currentUv =
        LogicalUv(
            pixel,
            currentActive);

    float2 motion =
        NativeMotion.Load(
            int3(pixel, 0)).xy;

    float2 previousUv =
        currentUv + motion;

    if (any(previousUv < 0.0) ||
        any(previousUv > 1.0))
    {
        CurrentConfidence[pixel] = 0.0;
        DebugMask[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float2 previousDrUv =
        previousRatio *
        previousUv;

    float previousDepth =
        PreviousLinearDepth.SampleLevel(
            LinearSampler,
            previousDrUv,
            0);

    if (!isfinite(previousDepth) ||
        previousDepth <= 0.0)
    {
        CurrentConfidence[pixel] = 0.0;
        DebugMask[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float cheapThreshold =
        max(
            currentDepth *
                DepthDisocclusion *
                3.0,
            0.001);

    if (abs(currentDepth - previousDepth) >
        cheapThreshold)
    {
        CurrentConfidence[pixel] = 0.0;
        DebugMask[pixel] = float4(0.0, 1.0, 0.0, 0.0);
        return;
    }

    row_major float4x4 previousProjectionInverse =
        TemporalPreviousProjectionInverse();

    row_major float4x4 previousViewInverse =
        TemporalPreviousViewInverse();

    float3 currentWorld =
        ReconstructWorldFromLinearDepth(
            currentUv,
            currentDepth,
            CameraProjInverse,
            CameraViewInverse,
            CameraPosAdjust.xyz);

    float3 previousWorld =
        ReconstructWorldFromLinearDepth(
            previousUv,
            previousDepth,
            previousProjectionInverse,
            previousViewInverse,
            TemporalPreviousCamera[12].xyz);

    float worldThreshold =
        max(
            currentDepth *
                DepthDisocclusion,
            0.001);

    if (distance(
            currentWorld,
            previousWorld) >
        worldThreshold)
    {
        CurrentConfidence[pixel] = 0.0;
        DebugMask[pixel] = float4(0.0, 1.0, 0.0, 0.0);
        return;
    }

    float2 previousEncodedNormal =
        PreviousWorldNormal.SampleLevel(
            LinearSampler,
            previousDrUv,
            0);

    float3 previousWorldNormal =
        DecodeOct(
            previousEncodedNormal);

    if (dot(
            currentWorldNormal,
            previousWorldNormal) <
        NormalDotThreshold)
    {
        CurrentConfidence[pixel] = 0.0;
        DebugMask[pixel] = float4(0.0, 0.0, 1.0, 0.0);
        return;
    }

    float previousConfidence =
        saturate(
            PreviousConfidence.SampleLevel(
                LinearSampler,
                previousDrUv,
                0));

    float previousFrames =
        previousConfidence *
        MaxHistoryFrames;

    float2 motionPixels =
        motion *
        float2(currentActive);

    float motionMagnitude =
        length(motionPixels);

    float capT =
        saturate(
            (motionMagnitude - 0.5) /
            max(
                MotionHighPixels - 0.5,
                0.001));

    float frameCap =
        lerp(
            MaxHistoryFrames,
            4.0,
            capT);

    float currentFrames =
        min(
            previousFrames + 1.0,
            frameCap);

    CurrentConfidence[pixel] =
        saturate(
            currentFrames /
            max(
                MaxHistoryFrames,
                1.0));

    DebugMask[pixel] = float4(1.0, 0.0, 0.0, 0.0);
}

RWStructuredBuffer<float4> CurrentCameraOut : register(u0);

[numthreads(1, 1, 1)]
void CameraStoreCS(uint3 dispatchId : SV_DispatchThreadID)
{
    if (any(dispatchId != uint3(0, 0, 0)))
    {
        return;
    }

    CurrentCameraOut[0] = CameraViewInverse[0];
    CurrentCameraOut[1] = CameraViewInverse[1];
    CurrentCameraOut[2] = CameraViewInverse[2];
    CurrentCameraOut[3] = CameraViewInverse[3];

    CurrentCameraOut[4] = CameraProjInverse[0];
    CurrentCameraOut[5] = CameraProjInverse[1];
    CurrentCameraOut[6] = CameraProjInverse[2];
    CurrentCameraOut[7] = CameraProjInverse[3];

    CurrentCameraOut[8] = CameraViewProjUnjittered[0];
    CurrentCameraOut[9] = CameraViewProjUnjittered[1];
    CurrentCameraOut[10] = CameraViewProjUnjittered[2];
    CurrentCameraOut[11] = CameraViewProjUnjittered[3];

    CurrentCameraOut[12] = CameraPosAdjust;
    CurrentCameraOut[13] = DynamicResolutionParams1;
    CurrentCameraOut[14] = CameraProj[0];
    CurrentCameraOut[15] = CameraProj[1];
}
)GZFX";
        }

        bool CreateTextureView(
            ID3D11Device* const device,
            const UINT width,
            const UINT height,
            const DXGI_FORMAT format,
            TextureView& out,
            std::string& error)
        {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = width;
            desc.Height = height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = format;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags =
                D3D11_BIND_SHADER_RESOURCE |
                D3D11_BIND_UNORDERED_ACCESS;

            HRESULT result =
                device->CreateTexture2D(
                    &desc,
                    nullptr,
                    &out.texture);

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateShaderResourceView(
                        out.texture,
                        nullptr,
                        &out.srv);
            }

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateUnorderedAccessView(
                        out.texture,
                        nullptr,
                        &out.uav);
            }

            if (FAILED(result))
            {
                error =
                    "CreateTextureView=" +
                    HResultText(result);
                ReleaseTextureView(out);
                return false;
            }

            out.format = format;
            return true;
        }

        bool CreateStructuredBuffer(
            ID3D11Device* const device,
            const UINT elementCount,
            const UINT elementBytes,
            ID3D11Buffer** const buffer,
            ID3D11ShaderResourceView** const srv,
            ID3D11UnorderedAccessView** const uav,
            std::string& error)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth =
                elementCount * elementBytes;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags =
                D3D11_BIND_SHADER_RESOURCE |
                D3D11_BIND_UNORDERED_ACCESS;
            desc.MiscFlags =
                D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            desc.StructureByteStride =
                elementBytes;

            HRESULT result =
                device->CreateBuffer(
                    &desc,
                    nullptr,
                    buffer);

            if (SUCCEEDED(result))
            {
                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
                srvDesc.Format = DXGI_FORMAT_UNKNOWN;
                srvDesc.ViewDimension =
                    D3D11_SRV_DIMENSION_BUFFER;
                srvDesc.Buffer.FirstElement = 0;
                srvDesc.Buffer.NumElements =
                    elementCount;

                result =
                    device->CreateShaderResourceView(
                        *buffer,
                        &srvDesc,
                        srv);
            }

            if (SUCCEEDED(result))
            {
                D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
                uavDesc.Format = DXGI_FORMAT_UNKNOWN;
                uavDesc.ViewDimension =
                    D3D11_UAV_DIMENSION_BUFFER;
                uavDesc.Buffer.FirstElement = 0;
                uavDesc.Buffer.NumElements =
                    elementCount;

                result =
                    device->CreateUnorderedAccessView(
                        *buffer,
                        &uavDesc,
                        uav);
            }

            if (FAILED(result))
            {
                error =
                    "CreateStructuredBuffer=" +
                    HResultText(result);

                if (*uav != nullptr)
                {
                    (*uav)->Release();
                    *uav = nullptr;
                }

                if (*srv != nullptr)
                {
                    (*srv)->Release();
                    *srv = nullptr;
                }

                if (*buffer != nullptr)
                {
                    (*buffer)->Release();
                    *buffer = nullptr;
                }

                return false;
            }

            return true;
        }

        bool TypedUavSupported(
            ID3D11Device* const device,
            const DXGI_FORMAT format)
        {
            UINT support = 0;

            if (FAILED(
                    device->CheckFormatSupport(
                        format,
                        &support)))
            {
                return false;
            }

            return
                (support &
                    D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW) != 0;
        }

        bool CreateGpuTimers(
            ID3D11Device* const device)
        {
            D3D11_QUERY_DESC disjoint{};
            disjoint.Query =
                D3D11_QUERY_TIMESTAMP_DISJOINT;

            D3D11_QUERY_DESC timestamp{};
            timestamp.Query =
                D3D11_QUERY_TIMESTAMP;

            for (auto& slot : g_runtime.gpuTimers)
            {
                if (FAILED(
                        device->CreateQuery(
                            &disjoint,
                            &slot.disjoint)))
                {
                    return false;
                }

                for (auto*& query : slot.timestamps)
                {
                    if (FAILED(
                            device->CreateQuery(
                                &timestamp,
                                &query)))
                    {
                        return false;
                    }
                }
            }

            return true;
        }

        void ResolveGpuTimers(
            ID3D11DeviceContext* const immediate)
        {
            if (immediate == nullptr)
            {
                return;
            }

            for (auto& slot : g_runtime.gpuTimers)
            {
                if (!slot.pending ||
                    slot.disjoint == nullptr)
                {
                    continue;
                }

                D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};

                if (immediate->GetData(
                        slot.disjoint,
                        &disjoint,
                        sizeof(disjoint),
                        D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
                {
                    continue;
                }

                std::array<UINT64, kGpuTimestampCount> values{};
                bool ready = true;

                for (std::size_t i = 0;
                     i < values.size();
                     ++i)
                {
                    if (immediate->GetData(
                            slot.timestamps[i],
                            &values[i],
                            sizeof(values[i]),
                            D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
                    {
                        ready = false;
                        break;
                    }
                }

                if (!ready)
                {
                    continue;
                }

                slot.pending = false;

                if (disjoint.Disjoint ||
                    disjoint.Frequency == 0)
                {
                    continue;
                }

                const double scale =
                    1000.0 /
                    static_cast<double>(
                        disjoint.Frequency);

                const auto delta =
                    [&](const std::size_t from,
                        const std::size_t to)
                    {
                        return
                            static_cast<double>(
                                values[to] -
                                values[from]) *
                            scale;
                    };

                g_runtime.latestTiming.valid = true;
                g_runtime.latestTiming.frame = slot.frame;
                g_runtime.latestTiming.depthBaseMs = delta(0, 1);
                g_runtime.latestTiming.depthTailMs = delta(1, 2);
                g_runtime.latestTiming.temporalMs = delta(2, 3);
                g_runtime.latestTiming.cameraStoreMs = delta(3, 4);
                g_runtime.latestTiming.totalMs = delta(0, 4);
            }
        }

        GpuTimerSlot* BeginGpuTimer(
            ID3D11DeviceContext* const deferred,
            const std::uint64_t frame)
        {
            auto& slot =
                g_runtime.gpuTimers[
                    g_runtime.gpuTimerCursor];

            g_runtime.gpuTimerCursor =
                (g_runtime.gpuTimerCursor + 1) %
                kGpuTimerSlots;

            if (slot.pending ||
                slot.disjoint == nullptr ||
                slot.timestamps[0] == nullptr)
            {
                return nullptr;
            }

            slot.frame = frame;
            deferred->Begin(slot.disjoint);
            deferred->End(slot.timestamps[0]);
            return &slot;
        }

        void StampGpuTimer(
            ID3D11DeviceContext* const deferred,
            GpuTimerSlot* const slot,
            const std::size_t index)
        {
            if (deferred != nullptr &&
                slot != nullptr &&
                index < slot->timestamps.size() &&
                slot->timestamps[index] != nullptr)
            {
                deferred->End(
                    slot->timestamps[index]);
            }
        }

        void FinishGpuTimer(
            ID3D11DeviceContext* const deferred,
            GpuTimerSlot* const slot)
        {
            if (deferred == nullptr ||
                slot == nullptr ||
                slot->disjoint == nullptr)
            {
                return;
            }

            deferred->End(slot->disjoint);
            slot->pending = true;
        }

        bool CreateHierarchy(
            ID3D11Device* const device,
            const UINT width,
            const UINT height,
            const UINT mipCount,
            std::string& error)
        {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = width;
            desc.Height = height;
            desc.MipLevels = mipCount;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R32_FLOAT;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags =
                D3D11_BIND_SHADER_RESOURCE |
                D3D11_BIND_UNORDERED_ACCESS;

            HRESULT result =
                device->CreateTexture2D(
                    &desc,
                    nullptr,
                    &g_runtime.hierarchy);

            if (SUCCEEDED(result))
            {
                D3D11_SHADER_RESOURCE_VIEW_DESC full{};
                full.Format = DXGI_FORMAT_R32_FLOAT;
                full.ViewDimension =
                    D3D11_SRV_DIMENSION_TEXTURE2D;
                full.Texture2D.MostDetailedMip = 0;
                full.Texture2D.MipLevels = mipCount;

                result =
                    device->CreateShaderResourceView(
                        g_runtime.hierarchy,
                        &full,
                        &g_runtime.hierarchySrv);
            }

            if (FAILED(result))
            {
                error =
                    "CreateHierarchy=" +
                    HResultText(result);
                return false;
            }

            g_runtime.hierarchyMipSrvs.resize(
                mipCount,
                nullptr);

            g_runtime.hierarchyMipUavs.resize(
                mipCount,
                nullptr);

            for (UINT mip = 0;
                 mip < mipCount;
                 ++mip)
            {
                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
                srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
                srvDesc.ViewDimension =
                    D3D11_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MostDetailedMip = mip;
                srvDesc.Texture2D.MipLevels = 1;

                result =
                    device->CreateShaderResourceView(
                        g_runtime.hierarchy,
                        &srvDesc,
                        &g_runtime.hierarchyMipSrvs[mip]);

                if (FAILED(result))
                {
                    break;
                }

                D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
                uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
                uavDesc.ViewDimension =
                    D3D11_UAV_DIMENSION_TEXTURE2D;
                uavDesc.Texture2D.MipSlice = mip;

                result =
                    device->CreateUnorderedAccessView(
                        g_runtime.hierarchy,
                        &uavDesc,
                        &g_runtime.hierarchyMipUavs[mip]);

                if (FAILED(result))
                {
                    break;
                }
            }

            if (FAILED(result))
            {
                error =
                    "CreateHierarchyViews=" +
                    HResultText(result);
                return false;
            }

            return true;
        }

        bool CreateHistorySet(
            ID3D11Device* const device,
            const UINT width,
            const UINT height,
            HistorySet& history,
            std::string& error)
        {
            if (!CreateTextureView(
                    device,
                    width,
                    height,
                    DXGI_FORMAT_R32_FLOAT,
                    history.depth,
                    error))
            {
                return false;
            }

            if (!CreateTextureView(
                    device,
                    width,
                    height,
                    g_runtime.normalHistoryFormat,
                    history.worldNormal,
                    error))
            {
                return false;
            }

            if (!CreateTextureView(
                    device,
                    width,
                    height,
                    g_runtime.confidenceFormat,
                    history.confidence,
                    error))
            {
                return false;
            }

            return CreateStructuredBuffer(
                device,
                static_cast<UINT>(
                    kCameraHistoryElements),
                sizeof(float) * 4,
                &history.camera,
                &history.cameraSrv,
                &history.cameraUav,
                error);
        }

        bool EnsureRuntime(
            const execution::ExecutionLease& lease,
            const native_input::GeometryInputs& inputs,
            bool* const rebuilt,
            std::string& error)
        {
            if (rebuilt != nullptr)
            {
                *rebuilt = false;
            }

            if (lease.device == nullptr ||
                inputs.width == 0 ||
                inputs.height == 0)
            {
                error = "missing device or dimensions";
                return false;
            }

            if (g_runtime.device == lease.device &&
                g_runtime.deviceGeneration ==
                    lease.deviceGeneration &&
                g_runtime.width == inputs.width &&
                g_runtime.height == inputs.height &&
                g_runtime.deferred != nullptr)
            {
                return true;
            }

            ReleaseRuntime();

            g_runtime.device = lease.device;
            g_runtime.device->AddRef();
            g_runtime.deviceGeneration =
                lease.deviceGeneration;
            g_runtime.width = inputs.width;
            g_runtime.height = inputs.height;
            g_runtime.mipCount =
                wave3_math::MipCount(
                    inputs.width,
                    inputs.height);

            HRESULT result =                lease.device->CreateDeferredContext(
                    0,
                    &g_runtime.deferred);

            ID3DBlob* depthBase = nullptr;
            ID3DBlob* depthReduce = nullptr;
            ID3DBlob* cameraCut = nullptr;
            ID3DBlob* temporal = nullptr;
            ID3DBlob* cameraStore = nullptr;

            const char* source = ShaderSource();

            if (SUCCEEDED(result) &&
                !CompileShader(
                    source,
                    "DepthBaseCS",
                    &depthBase,
                    error))
            {
                result = E_FAIL;
            }

            if (SUCCEEDED(result) &&
                !CompileShader(
                    source,
                    "DepthReduceCS",
                    &depthReduce,
                    error))
            {
                result = E_FAIL;
            }

            if (SUCCEEDED(result) &&
                !CompileShader(
                    source,
                    "CameraCutCS",
                    &cameraCut,
                    error))
            {
                result = E_FAIL;
            }

            if (SUCCEEDED(result) &&
                !CompileShader(
                    source,
                    "TemporalGeometryCS",
                    &temporal,
                    error))
            {
                result = E_FAIL;
            }

            if (SUCCEEDED(result) &&
                !CompileShader(
                    source,
                    "CameraStoreCS",
                    &cameraStore,
                    error))
            {
                result = E_FAIL;
            }

            if (SUCCEEDED(result))
            {
                result =
                    lease.device->CreateComputeShader(
                        depthBase->GetBufferPointer(),
                        depthBase->GetBufferSize(),
                        nullptr,
                        &g_runtime.depthBaseShader);
            }

            if (SUCCEEDED(result))
            {
                result =
                    lease.device->CreateComputeShader(
                        depthReduce->GetBufferPointer(),
                        depthReduce->GetBufferSize(),
                        nullptr,
                        &g_runtime.depthReduceShader);
            }

            if (SUCCEEDED(result))
            {
                result =
                    lease.device->CreateComputeShader(
                        cameraCut->GetBufferPointer(),
                        cameraCut->GetBufferSize(),
                        nullptr,
                        &g_runtime.cameraCutShader);
            }

            if (SUCCEEDED(result))
            {
                result =
                    lease.device->CreateComputeShader(
                        temporal->GetBufferPointer(),
                        temporal->GetBufferSize(),
                        nullptr,
                        &g_runtime.temporalShader);
            }

            if (SUCCEEDED(result))
            {
                result =
                    lease.device->CreateComputeShader(
                        cameraStore->GetBufferPointer(),
                        cameraStore->GetBufferSize(),
                        nullptr,
                        &g_runtime.cameraStoreShader);
            }

            if (depthBase != nullptr) depthBase->Release();
            if (depthReduce != nullptr) depthReduce->Release();
            if (cameraCut != nullptr) cameraCut->Release();
            if (temporal != nullptr) temporal->Release();
            if (cameraStore != nullptr) cameraStore->Release();

            D3D11_BUFFER_DESC parameterDesc{};
            parameterDesc.ByteWidth = sizeof(Parameters);
            parameterDesc.Usage = D3D11_USAGE_DEFAULT;
            parameterDesc.BindFlags =
                D3D11_BIND_CONSTANT_BUFFER;

            if (SUCCEEDED(result))
            {
                result =
                    lease.device->CreateBuffer(
                        &parameterDesc,
                        nullptr,
                        &g_runtime.parameters);
            }

            D3D11_SAMPLER_DESC sampler{};
            sampler.Filter =
                D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU =
                D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.AddressV =
                D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.AddressW =
                D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.MaxLOD = D3D11_FLOAT32_MAX;

            if (SUCCEEDED(result))
            {
                result =
                    lease.device->CreateSamplerState(
                        &sampler,
                        &g_runtime.linearSampler);
            }

            if (FAILED(result))
            {
                if (error.empty())
                {
                    error =
                        "Wave3 shader/runtime creation=" +
                        HResultText(result);
                }

                ReleaseRuntime();
                return false;
            }

            if (!CreateHierarchy(
                    lease.device,
                    inputs.width,
                    inputs.height,
                    g_runtime.mipCount,
                    error))
            {
                ReleaseRuntime();
                return false;
            }

            g_runtime.normalHistoryFormat =
                TypedUavSupported(
                    lease.device,
                    DXGI_FORMAT_R8G8_UNORM)
                    ? DXGI_FORMAT_R8G8_UNORM
                    : DXGI_FORMAT_R16G16_FLOAT;

            g_runtime.confidenceFormat =
                TypedUavSupported(
                    lease.device,
                    DXGI_FORMAT_R8_UNORM)
                    ? DXGI_FORMAT_R8_UNORM
                    : DXGI_FORMAT_R16_FLOAT;

            for (auto& history : g_runtime.history)
            {
                if (!CreateHistorySet(
                        lease.device,
                        inputs.width,
                        inputs.height,
                        history,
                        error))
                {
                    ReleaseRuntime();
                    return false;
                }
            }

            if (!CreateStructuredBuffer(
                    lease.device,
                    static_cast<UINT>(
                        kCutStateElements),
                    sizeof(float) * 4,
                    &g_runtime.cutState,
                    &g_runtime.cutStateSrv,
                    &g_runtime.cutStateUav,
                    error))
            {
                ReleaseRuntime();
                return false;
            }

            if (!CreateTextureView(
                    lease.device,
                    inputs.width,
                    inputs.height,
                    DXGI_FORMAT_R8G8B8A8_UNORM,
                    g_runtime.debugMask,
                    error))
            {
                ReleaseRuntime();
                return false;
            }

            if (!CreateGpuTimers(lease.device))
            {
                error = "gpu timer creation failed";
                ReleaseRuntime();
                return false;
            }

            if (rebuilt != nullptr)
            {
                *rebuilt = true;
            }

            return true;
        }

        ResetReason EvaluateReset(
            const native_input::GeometryInputs& inputs,
            const execution::ExecutionLease& lease,
            const bool rebuilt)
        {
            if (rebuilt)
            {
                return ResetReason::RuntimeRebuild;
            }

            if (!g_history.valid)
            {
                return ResetReason::FirstValidFrame;
            }

            if (inputs.frame !=
                g_history.lastFrame + 1)
            {
                return ResetReason::FrameGap;
            }

            if (lease.deviceGeneration !=
                g_history.deviceGeneration)
            {
                return ResetReason::DeviceGenerationChanged;
            }

            if (lease.surfaceGeneration !=
                g_history.surfaceGeneration)
            {
                return ResetReason::SurfaceGenerationChanged;
            }

            if (inputs.width != g_history.width ||
                inputs.height != g_history.height)
            {
                return ResetReason::ResourceDimensionsChanged;
            }

            if (inputs.mainDepth.semanticGeneration !=
                    g_history.depthGeneration ||
                inputs.motion.semanticGeneration !=
                    g_history.motionGeneration ||
                inputs.nativeNormal.semanticGeneration !=
                    g_history.normalGeneration)
            {
                return ResetReason::SemanticGenerationChanged;
            }

            if (inputs.cameraGeneration !=
                g_history.cameraGeneration)
            {
                return ResetReason::CameraGenerationChanged;
            }

            if (g_history.environment !=
                    semantic_authority::EnvironmentClass::UnknownTransition &&
                inputs.previousCompletedEnvironment !=
                    semantic_authority::EnvironmentClass::UnknownTransition &&
                g_history.environment !=
                    inputs.previousCompletedEnvironment)
            {
                return ResetReason::EnvironmentTransition;
            }

            return ResetReason::None;
        }

        void CommitHistoryState(
            const native_input::GeometryInputs& inputs,
            const execution::ExecutionLease& lease,
            const std::size_t writeIndex,
            const ResetReason resetReason)
        {
            if (resetReason != ResetReason::None)
            {
                g_history.lastReset = resetReason;
            }

            g_history.valid = true;
            g_history.lastFrame = inputs.frame;
            g_history.deviceGeneration =
                lease.deviceGeneration;
            g_history.surfaceGeneration =
                lease.surfaceGeneration;
            g_history.width = inputs.width;
            g_history.height = inputs.height;
            g_history.depthGeneration =
                inputs.mainDepth.semanticGeneration;
            g_history.motionGeneration =
                inputs.motion.semanticGeneration;
            g_history.normalGeneration =
                inputs.nativeNormal.semanticGeneration;
            g_history.cameraGeneration =
                inputs.cameraGeneration;
            g_history.environment =
                inputs.previousCompletedEnvironment;
            g_history.readIndex = writeIndex;
        }

        void WriteStatus(
            const std::uint64_t frame,
            const char* status,
            const native_input::GeometryInputs* inputs,
            const ResetReason resetReason,
            const native_input::Failure inputFailure,
            const std::string& detail = {})
        {
            const fs::path root = DiagnosticsRoot();

            if (root.empty())
            {
                return;
            }

            std::ostringstream out;
            out
                << "GZFX Wave 3 runtime\n"
                << "version=" << gzfx::kVersion << "\n"
                << "status=" << status << "\n"
                << "frame=" << frame << "\n"
                << "executions=" << g_executeCount << "\n"
                << "skips=" << g_skipCount << "\n"
                << "history_valid="
                << (g_history.valid ? "true" : "false")
                << "\n"
                << "history_generation="
                << g_history.generation
                << "\n"
                << "history_reset_reason="
                << ResetReasonName(resetReason)
                << "\n"
                << "last_reset_reason="
                << ResetReasonName(g_history.lastReset)
                << "\n"
                << "input_failure="
                << native_input::FailureName(inputFailure)
                << "\n"
                << "hierarchy_valid="
                << (g_runtime.hierarchy != nullptr ? "true" : "false")
                << "\n"
                << "temporal_valid="
                << (g_runtime.history[0].depth.texture != nullptr
                    ? "true"
                    : "false")
                << "\n"
                << "history_reused_candidate="
                << (
                    resetReason == ResetReason::None &&
                    g_history.valid
                        ? "true"
                        : "false")
                << "\n"
                << "normal_history_format="
                << static_cast<unsigned>(
                    g_runtime.normalHistoryFormat)
                << "\n"
                << "confidence_format="
                << static_cast<unsigned>(
                    g_runtime.confidenceFormat)
                << "\n";

            if (inputs != nullptr)
            {
                out
                    << "size="
                    << inputs->width
                    << "x"
                    << inputs->height
                    << "\n"
                    << "mips="
                    << g_runtime.mipCount
                    << "\n"
                    << "main_depth="
                    << PointerText(
                        inputs->mainDepth.texture)
                    << " generation="
                    << inputs->mainDepth.semanticGeneration
                    << "\n"
                    << "motion="
                    << PointerText(
                        inputs->motion.texture)
                    << " generation="
                    << inputs->motion.semanticGeneration
                    << "\n"
                    << "native_normal="
                    << PointerText(
                        inputs->nativeNormal.texture)
                    << " generation="
                    << inputs->nativeNormal.semanticGeneration
                    << "\n"
                    << "camera="
                    << PointerText(
                        inputs->cameraBuffer)
                    << " generation="
                    << inputs->cameraGeneration
                    << "\n"
                    << "device_generation="
                    << inputs->deviceGeneration
                    << "\n"
                    << "surface_generation="
                    << inputs->surfaceGeneration
                    << "\n"
                    << "previous_completed_environment="
                    << semantic_authority::EnvironmentName(
                        inputs->previousCompletedEnvironment)
                    << "\n";
            }

            if (g_runtime.latestTiming.valid)
            {
                out
                    << std::fixed
                    << std::setprecision(4)
                    << "gpu_timing_frame="
                    << g_runtime.latestTiming.frame
                    << "\n"
                    << "gpu_depth_base_ms="
                    << g_runtime.latestTiming.depthBaseMs
                    << "\n"
                    << "gpu_depth_tail_ms="
                    << g_runtime.latestTiming.depthTailMs
                    << "\n"
                    << "gpu_temporal_ms="
                    << g_runtime.latestTiming.temporalMs
                    << "\n"
                    << "gpu_camera_store_ms="
                    << g_runtime.latestTiming.cameraStoreMs
                    << "\n"
                    << "gpu_total_ms="
                    << g_runtime.latestTiming.totalMs
                    << "\n";
            }

            if (!detail.empty())
            {
                out << "detail=" << detail << "\n";
            }

            WriteText(
                root / L"wave3_status.txt",
                out.str());

            if (g_runtime.latestTiming.valid)
            {
                std::ostringstream timing;
                timing
                    << "frame,depth_base_ms,depth_tail_ms,temporal_ms,camera_store_ms,total_ms\n"
                    << g_runtime.latestTiming.frame
                    << ","
                    << g_runtime.latestTiming.depthBaseMs
                    << ","
                    << g_runtime.latestTiming.depthTailMs
                    << ","
                    << g_runtime.latestTiming.temporalMs
                    << ","
                    << g_runtime.latestTiming.cameraStoreMs
                    << ","
                    << g_runtime.latestTiming.totalMs
                    << "\n";

                WriteText(
                    root / L"wave3_gpu_timing.csv",
                    timing.str());
            }
        }

        void ClearComputeBindings(
            ID3D11DeviceContext* const deferred)
        {
            std::array<ID3D11ShaderResourceView*, 8> nullSrvs{};
            std::array<ID3D11UnorderedAccessView*, 4> nullUavs{};
            std::array<ID3D11Buffer*, 2> nullBuffers{};
            std::array<ID3D11SamplerState*, 1> nullSamplers{};

            deferred->CSSetShaderResources(
                0,
                static_cast<UINT>(nullSrvs.size()),
                nullSrvs.data());

            deferred->CSSetUnorderedAccessViews(
                0,
                static_cast<UINT>(nullUavs.size()),
                nullUavs.data(),
                nullptr);

            deferred->CSSetConstantBuffers(
                0,
                static_cast<UINT>(nullBuffers.size()),
                nullBuffers.data());

            deferred->CSSetSamplers(
                0,
                static_cast<UINT>(nullSamplers.size()),
                nullSamplers.data());

            deferred->CSSetShader(
                nullptr,
                nullptr,
                0);
        }

        bool RecordWave3Commands(
            const native_input::GeometryInputs& inputs,
            const std::size_t previousIndex,
            const std::size_t writeIndex,
            const bool globalHistoryValid,
            ID3D11CommandList** const outCommands,
            std::string& error)
        {
            if (outCommands == nullptr)
            {
                return false;
            }

            *outCommands = nullptr;

            auto* const deferred =
                g_runtime.deferred;

            if (deferred == nullptr)
            {
                error = "deferred context missing";
                return false;
            }

            Parameters parameters{};
            parameters.resourceWidth = inputs.width;
            parameters.resourceHeight = inputs.height;
            parameters.mipCount = g_runtime.mipCount;
            parameters.frameLow =
                static_cast<std::uint32_t>(inputs.frame);
            parameters.globalHistoryValid =
                globalHistoryValid ? 1u : 0u;
            parameters.historyGenerationLow =
                static_cast<std::uint32_t>(
                    g_history.generation);
            parameters.debugFlags =
                g_capture.active &&
                g_capture.frame == inputs.frame
                    ? 1u
                    : 0u;
            parameters.reductionMode = 0;

            deferred->ClearState();

            deferred->UpdateSubresource(
                g_runtime.parameters,
                0,
                nullptr,
                &parameters,
                0,
                0);

            ID3D11Buffer* constants[2] =
            {
                g_runtime.parameters,
                inputs.cameraBuffer
            };

            deferred->CSSetConstantBuffers(
                0,
                1,
                &constants[0]);

            deferred->CSSetConstantBuffers(
                12,
                1,
                &constants[1]);

            GpuTimerSlot* timer =
                BeginGpuTimer(
                    deferred,
                    inputs.frame);

            deferred->CSSetShader(
                g_runtime.depthBaseShader,
                nullptr,
                0);

            ID3D11ShaderResourceView* baseSrv =
                inputs.mainDepth.srv;

            ID3D11UnorderedAccessView* baseUav =
                g_runtime.hierarchyMipUavs[0];

            deferred->CSSetShaderResources(
                0,
                1,
                &baseSrv);

            deferred->CSSetUnorderedAccessViews(
                0,
                1,
                &baseUav,
                nullptr);

            deferred->Dispatch(
                (inputs.width + kThreadsX - 1) /
                    kThreadsX,
                (inputs.height + kThreadsY - 1) /
                    kThreadsY,
                1);

            ClearComputeBindings(deferred);
            StampGpuTimer(deferred, timer, 1);

            deferred->CSSetConstantBuffers(
                0,
                1,
                &constants[0]);

            for (UINT mip = 1;
                 mip < g_runtime.mipCount;
                 ++mip)
            {
                deferred->CSSetShader(
                    g_runtime.depthReduceShader,
                    nullptr,
                    0);

                ID3D11ShaderResourceView* source =
                    g_runtime.hierarchyMipSrvs[mip - 1];

                ID3D11UnorderedAccessView* destination =
                    g_runtime.hierarchyMipUavs[mip];

                deferred->CSSetShaderResources(
                    0,
                    1,
                    &source);

                deferred->CSSetUnorderedAccessViews(
                    0,
                    1,
                    &destination,
                    nullptr);

                const UINT mipWidth =
                    std::max<UINT>(
                        1,
                        inputs.width >> mip);

                const UINT mipHeight =
                    std::max<UINT>(
                        1,
                        inputs.height >> mip);

                deferred->Dispatch(
                    (mipWidth + kThreadsX - 1) /
                        kThreadsX,
                    (mipHeight + kThreadsY - 1) /
                        kThreadsY,
                    1);

                ClearComputeBindings(deferred);

                deferred->CSSetConstantBuffers(
                    0,
                    1,
                    &constants[0]);
            }

            StampGpuTimer(deferred, timer, 2);

            deferred->CSSetConstantBuffers(
                0,
                1,
                &constants[0]);

            deferred->CSSetConstantBuffers(
                12,
                1,
                &constants[1]);

            deferred->CSSetShader(
                g_runtime.cameraCutShader,
                nullptr,
                0);

            ID3D11ShaderResourceView* previousCamera =
                g_runtime.history[previousIndex].cameraSrv;

            deferred->CSSetShaderResources(
                0,
                1,
                &previousCamera);

            ID3D11UnorderedAccessView* cutUav =
                g_runtime.cutStateUav;

            deferred->CSSetUnorderedAccessViews(
                0,
                1,
                &cutUav,
                nullptr);

            deferred->Dispatch(1, 1, 1);
            ClearComputeBindings(deferred);

            deferred->CSSetConstantBuffers(
                0,
                1,
                &constants[0]);

            deferred->CSSetConstantBuffers(
                12,
                1,
                &constants[1]);

            deferred->CSSetSamplers(
                0,
                1,
                &g_runtime.linearSampler);

            deferred->CSSetShader(
                g_runtime.temporalShader,
                nullptr,
                0);

            std::array<ID3D11ShaderResourceView*, 8> temporalSrvs =
            {
                g_runtime.hierarchyMipSrvs[0],
                inputs.motion.srv,
                inputs.nativeNormal.srv,
                g_runtime.history[previousIndex].depth.srv,
                g_runtime.history[previousIndex].worldNormal.srv,
                g_runtime.history[previousIndex].confidence.srv,
                g_runtime.history[previousIndex].cameraSrv,
                g_runtime.cutStateSrv
            };

            deferred->CSSetShaderResources(
                0,
                static_cast<UINT>(temporalSrvs.size()),
                temporalSrvs.data());

            std::array<ID3D11UnorderedAccessView*, 4> temporalUavs =
            {
                g_runtime.history[writeIndex].depth.uav,
                g_runtime.history[writeIndex].worldNormal.uav,
                g_runtime.history[writeIndex].confidence.uav,
                g_runtime.debugMask.uav
            };

            deferred->CSSetUnorderedAccessViews(
                0,
                static_cast<UINT>(temporalUavs.size()),
                temporalUavs.data(),
                nullptr);

            deferred->Dispatch(
                (inputs.width + kThreadsX - 1) /
                    kThreadsX,
                (inputs.height + kThreadsY - 1) /
                    kThreadsY,
                1);

            ClearComputeBindings(deferred);
            StampGpuTimer(deferred, timer, 3);

            deferred->CSSetConstantBuffers(
                12,
                1,
                &constants[1]);

            deferred->CSSetShader(
                g_runtime.cameraStoreShader,
                nullptr,
                0);

            ID3D11UnorderedAccessView* cameraOut =
                g_runtime.history[writeIndex].cameraUav;

            deferred->CSSetUnorderedAccessViews(
                0,
                1,
                &cameraOut,
                nullptr);

            deferred->Dispatch(1, 1, 1);
            ClearComputeBindings(deferred);
            StampGpuTimer(deferred, timer, 4);
            FinishGpuTimer(deferred, timer);

            const HRESULT result =
                deferred->FinishCommandList(
                    FALSE,
                    outCommands);

            if (FAILED(result) ||
                *outCommands == nullptr)
            {
                error =
                    "FinishCommandList=" +
                    HResultText(result);
                return false;
            }

            return true;
        }

        float HalfToFloat(const std::uint16_t half) noexcept
        {
            const std::uint32_t sign =
                (half >> 15u) & 0x1u;
            const std::uint32_t exponent =
                (half >> 10u) & 0x1Fu;
            const std::uint32_t mantissa =
                half & 0x3FFu;

            std::uint32_t bits = 0;

            if (exponent == 0)
            {
                if (mantissa == 0)
                {
                    bits = sign << 31u;
                }
                else
                {
                    std::uint32_t m = mantissa;
                    int e = -14;

                    while ((m & 0x400u) == 0)
                    {
                        m <<= 1u;
                        --e;
                    }

                    m &= 0x3FFu;
                    bits =
                        (sign << 31u) |
                        (static_cast<std::uint32_t>(
                            e + 127) << 23u) |
                        (m << 13u);
                }
            }
            else if (exponent == 31)
            {
                bits =
                    (sign << 31u) |
                    0x7F800000u |
                    (mantissa << 13u);
            }
            else
            {
                bits =
                    (sign << 31u) |
                    ((exponent - 15u + 127u) << 23u) |
                    (mantissa << 13u);
            }

            float result = 0.0f;
            std::memcpy(
                &result,
                &bits,
                sizeof(result));
            return result;
        }

        bool WriteBmp(
            const fs::path& path,
            const UINT width,
            const UINT height,
            const std::vector<std::uint8_t>& gray)
        {
            if (width == 0 ||
                height == 0 ||
                gray.size() <
                    static_cast<std::size_t>(width) *
                    height)
            {
                return false;
            }

            BITMAPFILEHEADER fileHeader{};
            BITMAPINFOHEADER infoHeader{};

            const DWORD imageBytes =
                width * height * 4;

            fileHeader.bfType = 0x4D42;
            fileHeader.bfOffBits =
                sizeof(BITMAPFILEHEADER) +
                sizeof(BITMAPINFOHEADER);
            fileHeader.bfSize =
                fileHeader.bfOffBits +
                imageBytes;

            infoHeader.biSize =
                sizeof(BITMAPINFOHEADER);
            infoHeader.biWidth =
                static_cast<LONG>(width);
            infoHeader.biHeight =
                static_cast<LONG>(height);
            infoHeader.biPlanes = 1;
            infoHeader.biBitCount = 32;
            infoHeader.biCompression = BI_RGB;
            infoHeader.biSizeImage = imageBytes;

            std::ofstream out(
                path,
                std::ios::binary |
                    std::ios::trunc);

            if (!out)
            {
                return false;
            }

            out.write(
                reinterpret_cast<const char*>(
                    &fileHeader),
                sizeof(fileHeader));

            out.write(
                reinterpret_cast<const char*>(
                    &infoHeader),
                sizeof(infoHeader));

            std::vector<std::uint8_t> row(
                static_cast<std::size_t>(width) * 4);

            for (UINT y = 0;
                 y < height;
                 ++y)
            {
                const UINT sourceY =
                    height - 1 - y;

                for (UINT x = 0;
                     x < width;
                     ++x)
                {
                    const auto value =
                        gray[
                            static_cast<std::size_t>(sourceY) *
                                width +
                            x];

                    row[x * 4 + 0] = value;
                    row[x * 4 + 1] = value;
                    row[x * 4 + 2] = value;
                    row[x * 4 + 3] = 255;
                }

                out.write(
                    reinterpret_cast<const char*>(
                        row.data()),
                    static_cast<std::streamsize>(
                        row.size()));
            }

            return static_cast<bool>(out);        }

        bool ReadR32Subresource(
            ID3D11Device* const device,
            ID3D11DeviceContext* const immediate,
            ID3D11Texture2D* const source,
            const UINT sourceMip,
            const UINT width,
            const UINT height,
            std::vector<float>& values)
        {
            if (device == nullptr ||
                immediate == nullptr ||
                source == nullptr ||
                width == 0 ||
                height == 0)
            {
                return false;
            }

            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = width;
            desc.Height = height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R32_FLOAT;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags =
                D3D11_CPU_ACCESS_READ;

            ID3D11Texture2D* staging = nullptr;

            if (FAILED(
                    device->CreateTexture2D(
                        &desc,
                        nullptr,
                        &staging)) ||
                staging == nullptr)
            {
                return false;
            }

            immediate->CopySubresourceRegion(
                staging,
                0,
                0,
                0,
                0,
                source,
                sourceMip,
                nullptr);

            D3D11_MAPPED_SUBRESOURCE mapped{};

            if (FAILED(
                    immediate->Map(
                        staging,
                        0,
                        D3D11_MAP_READ,
                        0,
                        &mapped)))
            {
                staging->Release();
                return false;
            }

            values.resize(
                static_cast<std::size_t>(width) *
                height);

            for (UINT y = 0;
                 y < height;
                 ++y)
            {
                const auto* row =
                    reinterpret_cast<const float*>(
                        static_cast<const std::uint8_t*>(
                            mapped.pData) +
                        static_cast<std::size_t>(y) *
                            mapped.RowPitch);

                std::copy(
                    row,
                    row + width,
                    values.begin() +
                        static_cast<std::size_t>(y) *
                            width);
            }

            immediate->Unmap(staging, 0);
            staging->Release();
            return true;
        }

        std::vector<std::uint8_t> DepthPreview(
            const std::vector<float>& values,
            float* const maxDepthOut = nullptr)
        {
            float maxDepth = 0.0f;

            for (const float value : values)
            {
                if (std::isfinite(value) &&
                    value > maxDepth)
                {
                    maxDepth = value;
                }
            }

            if (maxDepthOut != nullptr)
            {
                *maxDepthOut = maxDepth;
            }

            const float denominator =
                std::log2(1.0f +
                    std::max(maxDepth, 1.0f));

            std::vector<std::uint8_t> result(
                values.size(),
                0);

            for (std::size_t i = 0;
                 i < values.size();
                 ++i)
            {
                const float value = values[i];

                if (!std::isfinite(value) ||
                    value <= 0.0f)
                {
                    result[i] = 0;
                    continue;
                }

                const float normalized =
                    denominator > 0.0f
                        ? std::log2(1.0f + value) /
                            denominator
                        : 0.0f;

                result[i] =
                    static_cast<std::uint8_t>(
                        std::clamp(
                            normalized,
                            0.0f,
                            1.0f) *
                        255.0f +
                        0.5f);
            }

            return result;
        }

        bool CaptureDepthMip(
            const execution::ExecutionLease& lease,
            const UINT mip,
            const fs::path& path,
            std::vector<float>* const valuesOut = nullptr)
        {
            if (mip >= g_runtime.mipCount)
            {
                return false;
            }

            const UINT width =
                std::max<UINT>(
                    1,
                    g_runtime.width >> mip);

            const UINT height =
                std::max<UINT>(
                    1,
                    g_runtime.height >> mip);

            std::vector<float> values;

            if (!ReadR32Subresource(
                    lease.device,
                    lease.immediate,
                    g_runtime.hierarchy,
                    mip,
                    width,
                    height,
                    values))
            {
                return false;
            }

            const auto gray =
                DepthPreview(values);

            const bool written =
                WriteBmp(
                    path,
                    width,
                    height,
                    gray);

            if (valuesOut != nullptr)
            {
                *valuesOut =
                    std::move(values);
            }

            return written;
        }

        bool ReadRgba8(
            ID3D11Device* const device,
            ID3D11DeviceContext* const immediate,
            ID3D11Texture2D* const source,
            const UINT width,
            const UINT height,
            std::vector<std::array<std::uint8_t, 4>>& values)
        {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = width;
            desc.Height = height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags =
                D3D11_CPU_ACCESS_READ;

            ID3D11Texture2D* staging = nullptr;

            if (FAILED(
                    device->CreateTexture2D(
                        &desc,
                        nullptr,
                        &staging)) ||
                staging == nullptr)
            {
                return false;
            }

            immediate->CopyResource(
                staging,
                source);

            D3D11_MAPPED_SUBRESOURCE mapped{};

            if (FAILED(
                    immediate->Map(
                        staging,
                        0,
                        D3D11_MAP_READ,
                        0,
                        &mapped)))
            {
                staging->Release();
                return false;
            }

            values.resize(
                static_cast<std::size_t>(width) *
                height);

            for (UINT y = 0;
                 y < height;
                 ++y)
            {
                const auto* row =
                    static_cast<const std::uint8_t*>(
                        mapped.pData) +
                    static_cast<std::size_t>(y) *
                        mapped.RowPitch;

                for (UINT x = 0;
                     x < width;
                     ++x)
                {
                    values[
                        static_cast<std::size_t>(y) *
                            width + x] =
                        {
                            row[x * 4 + 0],
                            row[x * 4 + 1],
                            row[x * 4 + 2],
                            row[x * 4 + 3]
                        };
                }
            }

            immediate->Unmap(staging, 0);
            staging->Release();
            return true;
        }

        bool CaptureDebugMasks(
            const execution::ExecutionLease& lease,
            const fs::path& directory)
        {
            std::vector<std::array<std::uint8_t, 4>> values;

            if (!ReadRgba8(
                    lease.device,
                    lease.immediate,
                    g_runtime.debugMask.texture,
                    g_runtime.width,
                    g_runtime.height,
                    values))
            {
                return false;
            }

            std::array<std::vector<std::uint8_t>, 4> channels;

            for (auto& channel : channels)
            {
                channel.resize(values.size());
            }

            for (std::size_t i = 0;
                 i < values.size();
                 ++i)
            {
                for (std::size_t c = 0;
                     c < channels.size();
                     ++c)
                {
                    channels[c][i] = values[i][c];
                }
            }

            bool ok = true;
            ok = WriteBmp(
                directory / L"history_accepted.bmp",
                g_runtime.width,
                g_runtime.height,
                channels[0]) && ok;
            ok = WriteBmp(
                directory / L"reject_depth_or_world_position.bmp",
                g_runtime.width,
                g_runtime.height,
                channels[1]) && ok;
            ok = WriteBmp(
                directory / L"reject_world_normal.bmp",
                g_runtime.width,
                g_runtime.height,
                channels[2]) && ok;
            ok = WriteBmp(
                directory / L"reject_uv_or_invalid.bmp",
                g_runtime.width,
                g_runtime.height,
                channels[3]) && ok;
            return ok;
        }

        bool ReadScalarTexture(
            ID3D11Device* device,
            ID3D11DeviceContext* immediate,
            ID3D11Texture2D* source,
            const DXGI_FORMAT format,
            const UINT width,
            const UINT height,
            std::vector<float>& values)
        {
            if (device == nullptr ||
                immediate == nullptr ||
                source == nullptr ||
                width == 0 ||
                height == 0)
            {
                return false;
            }

            D3D11_TEXTURE2D_DESC stagingDesc{};
            stagingDesc.Width = width;
            stagingDesc.Height = height;
            stagingDesc.MipLevels = 1;
            stagingDesc.ArraySize = 1;
            stagingDesc.Format = format;
            stagingDesc.SampleDesc.Count = 1;
            stagingDesc.Usage = D3D11_USAGE_STAGING;
            stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

            ID3D11Texture2D* staging = nullptr;

            if (FAILED(
                    device->CreateTexture2D(
                        &stagingDesc,
                        nullptr,
                        &staging)) ||
                staging == nullptr)
            {
                return false;
            }

            immediate->CopyResource(staging, source);

            D3D11_MAPPED_SUBRESOURCE mapped{};
            const HRESULT mapResult =
                immediate->Map(
                    staging,
                    0,
                    D3D11_MAP_READ,
                    0,
                    &mapped);

            if (FAILED(mapResult))
            {
                staging->Release();
                return false;
            }

            values.assign(
                static_cast<std::size_t>(width) * height,
                0.0f);

            for (UINT y = 0; y < height; ++y)
            {
                const auto* row =
                    static_cast<const std::uint8_t*>(mapped.pData) +
                    static_cast<std::size_t>(y) * mapped.RowPitch;

                for (UINT x = 0; x < width; ++x)
                {
                    float value = 0.0f;

                    if (format == DXGI_FORMAT_R8_UNORM)
                    {
                        value =
                            static_cast<float>(row[x]) /
                            255.0f;
                    }
                    else if (format == DXGI_FORMAT_R16_FLOAT)
                    {
                        const auto* halves =
                            reinterpret_cast<const std::uint16_t*>(row);
                        value = HalfToFloat(halves[x]);
                    }
                    else if (format == DXGI_FORMAT_R32_FLOAT)
                    {
                        const auto* floats =
                            reinterpret_cast<const float*>(row);
                        value = floats[x];
                    }
                    else
                    {
                        immediate->Unmap(staging, 0);
                        staging->Release();
                        return false;
                    }

                    values[
                        static_cast<std::size_t>(y) * width + x] =
                        value;
                }
            }

            immediate->Unmap(staging, 0);
            staging->Release();
            return true;
        }

        bool CaptureConfidence(
            const execution::ExecutionLease& lease,
            const fs::path& directory,
            const HistorySet& history)
        {
            std::vector<float> confidence;

            if (!ReadScalarTexture(
                    lease.device,
                    lease.immediate,
                    history.confidence.texture,
                    g_runtime.confidenceFormat,
                    g_runtime.width,
                    g_runtime.height,
                    confidence))
            {
                return false;
            }

            std::vector<std::uint8_t> visible(confidence.size(), 0);

            for (std::size_t i = 0;
                 i < confidence.size();
                 ++i)
            {
                visible[i] =
                    static_cast<std::uint8_t>(
                        std::clamp(
                            confidence[i],
                            0.0f,
                            1.0f) * 255.0f + 0.5f);
            }

            return WriteBmp(
                directory / L"history_confidence.bmp",
                g_runtime.width,
                g_runtime.height,
                visible);
        }

        bool CaptureMotion(
            const execution::ExecutionLease& lease,
            const native_input::GeometryInputs& inputs,
            const fs::path& directory)
        {
            if (inputs.motion.texture == nullptr ||
                inputs.motion.desc.Format != DXGI_FORMAT_R16G16_FLOAT)
            {
                return false;
            }

            D3D11_TEXTURE2D_DESC stagingDesc =
                inputs.motion.desc;
            stagingDesc.Usage = D3D11_USAGE_STAGING;
            stagingDesc.BindFlags = 0;
            stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            stagingDesc.MiscFlags = 0;
            stagingDesc.MipLevels = 1;
            stagingDesc.ArraySize = 1;

            ID3D11Texture2D* staging = nullptr;

            if (FAILED(
                    lease.device->CreateTexture2D(
                        &stagingDesc,
                        nullptr,
                        &staging)) ||
                staging == nullptr)
            {
                return false;
            }

            lease.immediate->CopyResource(
                staging,
                inputs.motion.texture);

            D3D11_MAPPED_SUBRESOURCE mapped{};

            if (FAILED(
                    lease.immediate->Map(
                        staging,
                        0,
                        D3D11_MAP_READ,
                        0,
                        &mapped)))
            {
                staging->Release();
                return false;
            }

            const std::size_t count =
                static_cast<std::size_t>(inputs.width) *
                inputs.height;

            std::vector<std::uint8_t> xView(count, 128);
            std::vector<std::uint8_t> yView(count, 128);
            std::vector<std::uint8_t> magnitude(count, 0);

            double sumPixels = 0.0;
            float maxPixels = 0.0f;
            std::uint64_t finiteCount = 0;

            for (UINT y = 0;
                 y < inputs.height;
                 ++y)
            {
                const auto* row =
                    reinterpret_cast<const std::uint16_t*>(
                        static_cast<const std::uint8_t*>(mapped.pData) +
                        static_cast<std::size_t>(y) * mapped.RowPitch);

                for (UINT x = 0;
                     x < inputs.width;
                     ++x)
                {
                    const float mx =
                        HalfToFloat(row[x * 2 + 0]);
                    const float my =
                        HalfToFloat(row[x * 2 + 1]);

                    if (!std::isfinite(mx) ||
                        !std::isfinite(my))
                    {
                        continue;
                    }

                    const auto index =
                        static_cast<std::size_t>(y) *
                            inputs.width + x;

                    const float signedScale = 32.0f;
                    xView[index] =
                        static_cast<std::uint8_t>(
                            std::clamp(
                                0.5f + mx * signedScale,
                                0.0f,
                                1.0f) *
                            255.0f + 0.5f);
                    yView[index] =
                        static_cast<std::uint8_t>(
                            std::clamp(
                                0.5f + my * signedScale,
                                0.0f,
                                1.0f) *
                            255.0f + 0.5f);

                    const float pixels =
                        std::sqrt(
                            (mx * inputs.width) *
                                (mx * inputs.width) +
                            (my * inputs.height) *
                                (my * inputs.height));

                    magnitude[index] =
                        static_cast<std::uint8_t>(
                            std::clamp(
                                pixels / 32.0f,
                                0.0f,
                                1.0f) *
                            255.0f + 0.5f);

                    sumPixels += pixels;
                    maxPixels = std::max(maxPixels, pixels);
                    ++finiteCount;
                }
            }

            lease.immediate->Unmap(staging, 0);
            staging->Release();

            const bool images =
                WriteBmp(
                    directory / L"motion_x.bmp",
                    inputs.width,
                    inputs.height,
                    xView) &&
                WriteBmp(
                    directory / L"motion_y.bmp",
                    inputs.width,
                    inputs.height,
                    yView) &&
                WriteBmp(
                    directory / L"motion_magnitude.bmp",
                    inputs.width,
                    inputs.height,
                    magnitude);

            std::ostringstream stats;
            stats
                << "motion_vector_convention=previous_uv-current_uv\n"
                << "reprojection=previousUV=currentUV+motion.xy\n"
                << "coordinate_space=normalized_screen_uv\n"
                << "mean_motion_pixels="
                << (finiteCount != 0
                    ? sumPixels /
                        static_cast<double>(finiteCount)
                    : 0.0)
                << "\n"
                << "max_motion_pixels="
                << maxPixels
                << "\n";

            WriteText(
                directory / L"motion_stats.txt",
                stats.str());

            return images;
        }

        bool ReadCutState(
            const execution::ExecutionLease& lease,
            std::array<float, kCutStateElements * 4>& values)
        {
            if (g_runtime.cutState == nullptr)
            {
                return false;
            }

            D3D11_BUFFER_DESC sourceDesc{};
            g_runtime.cutState->GetDesc(&sourceDesc);

            D3D11_BUFFER_DESC stagingDesc = sourceDesc;
            stagingDesc.Usage = D3D11_USAGE_STAGING;
            stagingDesc.BindFlags = 0;
            stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            stagingDesc.MiscFlags = 0;

            ID3D11Buffer* staging = nullptr;

            if (FAILED(
                    lease.device->CreateBuffer(
                        &stagingDesc,
                        nullptr,
                        &staging)) ||
                staging == nullptr)
            {
                return false;
            }

            lease.immediate->CopyResource(
                staging,
                g_runtime.cutState);

            D3D11_MAPPED_SUBRESOURCE mapped{};

            if (FAILED(
                    lease.immediate->Map(
                        staging,
                        0,
                        D3D11_MAP_READ,
                        0,
                        &mapped)))
            {
                staging->Release();
                return false;
            }

            std::memcpy(
                values.data(),
                mapped.pData,
                sizeof(values));

            lease.immediate->Unmap(staging, 0);
            staging->Release();
            return true;
        }

        void CaptureCameraContinuity(
            const execution::ExecutionLease& lease,
            const fs::path& directory,
            const ResetReason resetReason)
        {
            std::array<float, kCutStateElements * 4> values{};

            std::ostringstream out;
            out
                << "reset_reason="
                << ResetReasonName(resetReason)
                << "\n";

            if (ReadCutState(lease, values))
            {
                std::uint32_t flags = 0;
                std::memcpy(
                    &flags,
                    &values[0],
                    sizeof(flags));

                out
                    << "cut_flags=" << flags << "\n"
                    << "translation_delta="
                    << values[1] << "\n"
                    << "rotation_delta_degrees="
                    << values[2] << "\n"
                    << "matrix_continuity_error="
                    << values[3] << "\n"
                    << "current_dr_ratio="
                    << values[4] << "," << values[5] << "\n"
                    << "previous_dr_ratio="
                    << values[6] << "," << values[7] << "\n"
                    << "projection_delta="
                    << values[8] << "\n"
                    << "current_active_size="
                    << values[9] << "x" << values[10] << "\n";
            }
            else
            {
                out << "cut_state_readback=failed\n";
            }

            WriteText(
                directory / L"wave3_camera_continuity.txt",
                out.str());
        }

        bool CaptureAfterOpaque(
            const execution::ExecutionLease& lease,
            const native_input::GeometryInputs& inputs,
            const std::size_t writeIndex,
            const ResetReason resetReason)
        {
            if (!g_capture.active ||
                g_capture.frame != inputs.frame)
            {
                return true;
            }

            std::error_code ec;
            fs::create_directories(
                g_capture.directory,
                ec);

            if (ec)
            {
                return false;
            }

            bool ok = true;

            const std::array<UINT, 5> mips =
            {
                0,
                std::min<UINT>(1, g_runtime.mipCount - 1),
                std::min<UINT>(2, g_runtime.mipCount - 1),
                std::min<UINT>(4, g_runtime.mipCount - 1),
                g_runtime.mipCount - 1
            };

            const std::array<const wchar_t*, 5> names =
            {
                L"gzfx_depth_mip0.bmp",
                L"gzfx_depth_mip1.bmp",
                L"gzfx_depth_mip2.bmp",
                L"gzfx_depth_mip4.bmp",
                L"gzfx_depth_tail.bmp"
            };

            for (std::size_t i = 0;
                 i < mips.size();
                 ++i)
            {
                ok = CaptureDepthMip(
                    lease.device,
                    lease.immediate,
                    mips[i],
                    g_capture.directory / names[i],
                    i == 0 ? &g_capture.gzfxDepth : nullptr) &&
                    ok;
            }

            g_capture.width = inputs.width;
            g_capture.height = inputs.height;

            ok = CaptureDebugMasks(
                lease,
                g_capture.directory) && ok;

            ok = CaptureConfidence(
                lease,
                g_capture.directory,
                g_runtime.history[writeIndex]) && ok;

            ok = CaptureMotion(
                lease,
                inputs,
                g_capture.directory) && ok;

            CaptureCameraContinuity(
                lease,
                g_capture.directory,
                resetReason);

            std::ostringstream summary;
            summary
                << "GZFX Wave 3 manual capture\n"
                << "version=" << gzfx::kVersion << "\n"
                << "capture_index=" << g_capture.index << "\n"
                << "frame=" << inputs.frame << "\n"
                << "size=" << inputs.width << "x" << inputs.height << "\n"
                << "mips=" << g_runtime.mipCount << "\n"
                << "reset_reason="
                << ResetReasonName(resetReason) << "\n"
                << "history_generation="
                << g_history.generation << "\n"
                << "normal_history_format="
                << static_cast<unsigned>(g_runtime.normalHistoryFormat)
                << "\n"
                << "confidence_format="
                << static_cast<unsigned>(g_runtime.confidenceFormat)
                << "\n"
                << "depth_source=MAIN_DEPTH_CURRENT\n"
                << "build_stage=AfterOpaqueWorldMRT\n"
                << "motion_rule=previousUV=currentUV+motion.xy\n"
                << "capture_after_opaque="
                << (ok ? "complete" : "partial")
                << "\n";

            WriteText(
                g_capture.directory / L"wave3_summary.txt",
                summary.str());

            return ok;
        }

        std::uintptr_t CanonicalId(IUnknown* value)
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

        ID3D11Texture2D* FindBoundTexture(
            ID3D11DeviceContext* immediate,
            const std::uintptr_t identity)
        {
            if (immediate == nullptr ||
                identity == 0)
            {
                return nullptr;
            }

            std::array<
                ID3D11RenderTargetView*,
                D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> rtvs{};
            ID3D11DepthStencilView* dsv = nullptr;

            immediate->OMGetRenderTargets(
                static_cast<UINT>(rtvs.size()),
                rtvs.data(),
                &dsv);

            auto inspectView =
                [&](ID3D11View* view) -> ID3D11Texture2D*
                {
                    if (view == nullptr)
                    {
                        return nullptr;
                    }

                    ID3D11Resource* resource = nullptr;
                    view->GetResource(&resource);

                    if (resource == nullptr)
                    {
                        return nullptr;
                    }

                    ID3D11Texture2D* texture = nullptr;

                    if (CanonicalId(resource) == identity)
                    {
                        resource->QueryInterface(
                            __uuidof(ID3D11Texture2D),
                            reinterpret_cast<void**>(&texture));
                    }

                    resource->Release();
                    return texture;
                };

            ID3D11Texture2D* result = nullptr;

            for (auto*& rtv : rtvs)
            {
                if (result == nullptr)
                {
                    result = inspectView(rtv);
                }

                if (rtv != nullptr)
                {
                    rtv->Release();
                }
            }

            if (result == nullptr)
            {
                result = inspectView(dsv);
            }

            if (dsv != nullptr)
            {
                dsv->Release();
            }

            if (result != nullptr)
            {
                return result;
            }

            std::array<ID3D11UnorderedAccessView*, 8> uavs{};
            immediate->CSGetUnorderedAccessViews(
                0,
                static_cast<UINT>(uavs.size()),
                uavs.data());

            for (auto*& uav : uavs)
            {
                if (result == nullptr)
                {
                    result = inspectView(uav);
                }
                if (uav != nullptr)
                {
                    uav->Release();
                }
            }

            return result;
        }

        double Percentile(
            std::vector<double>& values,
            const double percentile)
        {
            if (values.empty())
            {
                return 0.0;
            }

            const double clamped =
                std::clamp(percentile, 0.0, 1.0);

            const std::size_t index =
                static_cast<std::size_t>(
                    std::llround(
                        clamped *
                        static_cast<double>(
                            values.size() - 1)));

            std::nth_element(
                values.begin(),
                values.begin() +
                    static_cast<std::ptrdiff_t>(index),
                values.end());

            return values[index];
        }

        bool CaptureStockCameraZ(
            const execution::ExecutionLease& lease,
            const std::uint64_t frame)
        {
            if (!g_capture.active ||
                g_capture.frame != frame ||
                g_capture.gzfxDepth.empty())
            {
                return false;
            }

            semantic_authority::SemanticView view{};

            if (!semantic_authority::QuerySemantic(
                    semantic_authority::Semantic::StockCameraZ,
                    semantic_authority::QueryMode::CurrentFrame,
                    frame,
                    stage_scheduler::Boundary::AfterCameraZ,
                    &view))
            {
                WriteText(
                    g_capture.directory / L"cameraz_comparison.txt",
                    "status=stock_cameraz_semantic_unavailable\n");
                return false;
            }

            ID3D11Texture2D* stock =
                FindBoundTexture(
                    lease.immediate,
                    view.resource.identity);

            if (stock == nullptr)
            {
                WriteText(
                    g_capture.directory / L"cameraz_comparison.txt",
                    "status=stock_cameraz_resource_not_bound\n");
                return false;
            }

            D3D11_TEXTURE2D_DESC desc{};
            stock->GetDesc(&desc);

            std::vector<float> stockValues;

            const bool read =
                desc.Format == DXGI_FORMAT_R32_FLOAT &&
                desc.Width == g_capture.width &&
                desc.Height == g_capture.height &&
                ReadR32Subresource(
                    lease.device,
                    lease.immediate,
                    stock,
                    0,
                    desc.Width,
                    desc.Height,
                    stockValues);

            stock->Release();

            if (!read ||
                stockValues.size() !=
                    g_capture.gzfxDepth.size())
            {
                WriteText(
                    g_capture.directory / L"cameraz_comparison.txt",
                    "status=stock_cameraz_readback_failed\n");
                return false;
            }

            std::vector<std::uint8_t> stockPreview;
            DepthPreview(stockValues, stockPreview);
            WriteBmp(
                g_capture.directory / L"stock_cameraz.bmp",
                g_capture.width,
                g_capture.height,
                stockPreview);

            std::vector<std::uint8_t> gzfxOnly(stockValues.size(), 0);
            std::vector<std::uint8_t> stockOnly(stockValues.size(), 0);
            std::vector<std::uint8_t> commonError(stockValues.size(), 0);
            std::vector<double> absoluteErrors;
            std::vector<double> relativeErrors;

            std::uint64_t common = 0;
            std::uint64_t gzfxOnlyCount = 0;
            std::uint64_t stockOnlyCount = 0;
            std::uint64_t bothBackground = 0;

            for (std::size_t i = 0;
                 i < stockValues.size();
                 ++i)
            {
                const float gzfx = g_capture.gzfxDepth[i];
                const float native = stockValues[i];

                const bool gzfxValid =
                    std::isfinite(gzfx) && gzfx > 0.0f;
                const bool stockValid =
                    std::isfinite(native) && native > 0.0f;

                if (gzfxValid && stockValid)
                {
                    ++common;
                    const double absolute =
                        std::abs(
                            static_cast<double>(gzfx) -
                            static_cast<double>(native));
                    const double relative =
                        absolute /
                        std::max(
                            std::abs(
                                static_cast<double>(native)),
                            1.0e-6);
                    absoluteErrors.push_back(absolute);
                    relativeErrors.push_back(relative);
                    commonError[i] =
                        static_cast<std::uint8_t>(
                            std::clamp(relative / 0.1, 0.0, 1.0) *
                                255.0 +
                            0.5);
                }
                else if (gzfxValid)
                {
                    ++gzfxOnlyCount;
                    gzfxOnly[i] = 255;
                }
                else if (stockValid)
                {
                    ++stockOnlyCount;
                    stockOnly[i] = 255;
                }
                else
                {
                    ++bothBackground;
                }
            }

            const double medianAbs =
                Percentile(absoluteErrors, 0.5);
            const double medianRel =
                Percentile(relativeErrors, 0.5);
            const double p95Rel =
                Percentile(relativeErrors, 0.95);
            const double p99Rel =
                Percentile(relativeErrors, 0.99);

            WriteBmp(
                g_capture.directory / L"depth_gzfx_only_geometry.bmp",
                g_capture.width,
                g_capture.height,
                gzfxOnly);
            WriteBmp(
                g_capture.directory / L"depth_stock_only_geometry.bmp",
                g_capture.width,
                g_capture.height,
                stockOnly);
            WriteBmp(
                g_capture.directory / L"depth_common_error.bmp",
                g_capture.width,
                g_capture.height,
                commonError);

            std::ostringstream out;
            out
                << "status=complete\n"
                << "frame=" << frame << "\n"
                << "common_geometry=" << common << "\n"
                << "gzfx_only_geometry=" << gzfxOnlyCount << "\n"
                << "stock_only_geometry=" << stockOnlyCount << "\n"
                << "both_background=" << bothBackground << "\n"
                << "median_absolute_error=" << medianAbs << "\n"
                << "median_relative_error=" << medianRel << "\n"
                << "p95_relative_error=" << p95Rel << "\n"
                << "p99_relative_error=" << p99Rel << "\n";

            WriteText(
                g_capture.directory / L"cameraz_comparison.txt",
                out.str());

            return true;
        }

        void PollCaptureRequest(const std::uint64_t frame)
        {
            const bool shift =
                (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            const bool numpad5 =
                (GetAsyncKeyState(VK_NUMPAD5) & 0x8000) != 0 ||
                (GetAsyncKeyState(VK_CLEAR) & 0x8000) != 0;
            const bool down = shift && numpad5;

            if (down && !g_captureKeyDown &&
                !g_capture.active)
            {
                ++g_captureIndex;
                g_capture = {};
                g_capture.active = true;
                g_capture.frame = frame;
                g_capture.index = g_captureIndex;

                std::wostringstream name;
                name
                    << L"manual"
                    << std::setw(4)
                    << std::setfill(L'0')
                    << g_captureIndex;

                g_capture.directory =
                    DiagnosticsRoot() /
                    name.str();
            }

            g_captureKeyDown = down;
        }

        void InvalidateHistory(
            const ResetReason reason)
        {
            g_history.valid = false;
            ++g_history.generation;
            g_history.lastReset = reason;
        }

        void OnAfterOpaque(
            const stage_scheduler::Event& event,
            void*) noexcept
        {
            try
            {
                if (event.boundary !=
                        stage_scheduler::Boundary::AfterOpaqueWorldMRT)
                {
                    return;
                }

                std::scoped_lock lock(g_mutex);
                PollCaptureRequest(event.frame);

                execution::ExecutionLease lease{};

                if (!execution::Acquire(&lease))
                {
                    ++g_skipCount;
                    InvalidateHistory(
                        ResetReason::MissingRequiredInput);
                    WriteStatus(
                        event.frame,
                        "skipped_execution_service",
                        nullptr,
                        ResetReason::MissingRequiredInput,
                        native_input::Failure::MissingExecutionService);
                    return;
                }

                struct LeaseGuard
                {
                    execution::ExecutionLease* value;
                    ~LeaseGuard()
                    {
                        execution::Release(*value);
                    }
                } leaseGuard{ &lease };

                ResolveGpuTimers(lease.immediate);

                native_input::GeometryInputs inputs{};
                native_input::Failure inputFailure =
                    native_input::Failure::None;

                if (!native_input::AcquireGeometryInputs(
                        event.frame,
                        event.boundary,
                        lease,
                        &inputs,
                        &inputFailure))
                {
                    ++g_skipCount;
                    InvalidateHistory(
                        ResetReason::MissingRequiredInput);
                    WriteStatus(
                        event.frame,
                        "skipped_missing_inputs",
                        nullptr,
                        ResetReason::MissingRequiredInput,
                        inputFailure);
                    return;
                }

                struct InputGuard
                {
                    native_input::GeometryInputs* value;
                    ~InputGuard()
                    {
                        native_input::ReleaseGeometryInputs(*value);
                    }
                } inputGuard{ &inputs };

                bool rebuilt = false;
                std::string error;

                if (!EnsureRuntime(
                        lease,
                        inputs,
                        &rebuilt,
                        error))
                {
                    ++g_skipCount;
                    InvalidateHistory(
                        ResetReason::ExecutionFailure);
                    WriteStatus(
                        event.frame,
                        "skipped_runtime",
                        &inputs,
                        ResetReason::ExecutionFailure,
                        native_input::Failure::None,
                        error);
                    return;
                }

                const ResetReason resetReason =
                    EvaluateReset(
                        inputs,
                        lease,
                        rebuilt);

                if (resetReason != ResetReason::None)
                {
                    ++g_history.generation;
                    g_history.lastReset = resetReason;
                }

                const bool globalHistoryValid =
                    resetReason == ResetReason::None &&
                    g_history.valid;

                const std::size_t previousIndex =
                    g_history.readIndex;
                const std::size_t writeIndex =
                    g_history.valid
                        ? 1u - previousIndex
                        : 0u;

                ID3D11CommandList* commandList = nullptr;

                if (!RecordWave3Commands(
                        inputs,
                        previousIndex,
                        writeIndex,
                        globalHistoryValid,
                        &commandList,
                        error))
                {
                    if (commandList != nullptr)
                    {
                        commandList->Release();
                    }

                    ++g_skipCount;
                    InvalidateHistory(
                        ResetReason::ExecutionFailure);
                    WriteStatus(
                        event.frame,
                        "skipped_record",
                        &inputs,
                        ResetReason::ExecutionFailure,
                        native_input::Failure::None,
                        error);
                    return;
                }

                const bool executed =
                    execution::ExecuteOwned(
                        lease,
                        commandList,
                        execution::OwnedPass::Wave3TemporalGeometry,
                        event.frame,
                        true);

                commandList->Release();

                if (!executed)
                {
                    ++g_skipCount;
                    InvalidateHistory(
                        ResetReason::ExecutionFailure);
                    WriteStatus(
                        event.frame,
                        "skipped_execute",
                        &inputs,
                        ResetReason::ExecutionFailure,
                        native_input::Failure::None);
                    return;
                }

                ++g_executeCount;
                CommitHistoryState(
                    inputs,
                    lease,
                    writeIndex,
                    ResetReason::None);

                if (g_capture.active &&
                    g_capture.frame == event.frame)
                {
                    CaptureAfterOpaque(
                        lease,
                        inputs,
                        writeIndex,
                        resetReason);
                }

                if (resetReason != ResetReason::None ||
                    g_capture.active ||
                    event.frame <= 3 ||
                    event.frame - g_lastStatusFrame >= 120)
                {
                    WriteStatus(
                        event.frame,
                        "executed",
                        &inputs,
                        resetReason,
                        native_input::Failure::None);
                    g_lastStatusFrame = event.frame;
                }
            }
            catch (...)
            {
            }
        }

        void OnAfterCameraZ(
            const stage_scheduler::Event& event,
            void*) noexcept
        {
            try
            {
                if (event.boundary !=
                        stage_scheduler::Boundary::AfterCameraZ)
                {
                    return;
                }

                std::scoped_lock lock(g_mutex);

                if (!g_capture.active ||
                    g_capture.frame != event.frame)
                {
                    return;
                }

                execution::ExecutionLease lease{};

                if (execution::Acquire(&lease))
                {
                    CaptureStockCameraZ(
                        lease,
                        event.frame);
                    execution::Release(lease);
                }

                g_capture.active = false;
                g_capture.gzfxDepth.clear();
            }
            catch (...)
            {
            }
        }
    }

    bool Initialize() noexcept
    {
        try
        {
            std::call_once(
                g_initializeOnce,
                []()
                {
                    const bool opaque =
                        stage_scheduler::RegisterCallback(
                            stage_scheduler::Boundary::AfterOpaqueWorldMRT,
                            &OnAfterOpaque,
                            nullptr);

                    const bool camera =
                        stage_scheduler::RegisterCallback(
                            stage_scheduler::Boundary::AfterCameraZ,
                            &OnAfterCameraZ,
                            nullptr);

                    g_initialized = opaque && camera;
                });

            return g_initialized;
        }
        catch (...)
        {
            return false;
        }
    }

    bool IsInitialized() noexcept
    {
        try
        {
            return g_initialized;
        }
        catch (...)
        {
            return false;
        }
    }
}