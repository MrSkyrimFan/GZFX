#include "gzfx/render/ambient_visibility_v2.hpp"

#include "gzfx/paths.hpp"
#include "gzfx/render/native_input_broker.hpp"
#include "gzfx/render/renderer_execution_service.hpp"
#include "gzfx/render/runtime_state.hpp"
#include "gzfx/render/semantic_authority.hpp"
#include "gzfx/render/stage_scheduler.hpp"
#include "gzfx/render/wave3_pipeline.hpp"
#include "gzfx/version.hpp"

#include <Windows.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
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

namespace gzfx::render::ambient_visibility_v2
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr UINT kThreadsX = 8;
        constexpr UINT kThreadsY = 8;
        constexpr std::size_t kTimerSlots = 6;
        constexpr std::size_t kComputeTimestampCount = 5;
        constexpr std::size_t kTimingHistoryLimit = 256;
        constexpr std::uint64_t kCaptureDelayFrames = 30;

        struct Parameters
        {
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            std::uint32_t mipCount = 0;
            std::uint32_t frameIndex = 0;

            float radiusWorld = 240.0f;
            float falloffFraction = 0.35f;
            float distributionPower = 2.0f;
            float visibilityPower = 1.0f;

            std::uint32_t sliceCount = 3;
            std::uint32_t stepsPerSlice = 6;
            std::uint32_t temporalEnabled = 0;
            std::uint32_t mode = 0;

            float depthSigma = 30.0f;
            float normalPower = 8.0f;
            float temporalBaseWeight = 0.70f;
            float temporalMaxWeight = 0.95f;
        };

        static_assert(sizeof(Parameters) == 64);

        struct TextureView
        {
            ID3D11Texture2D* texture = nullptr;
            ID3D11ShaderResourceView* srv = nullptr;
            ID3D11UnorderedAccessView* uav = nullptr;
        };

        struct ComputeTimerSlot
        {
            ID3D11Query* disjoint = nullptr;
            std::array<
                ID3D11Query*,
                kComputeTimestampCount> timestamps{};
            bool pending = false;
            std::uint64_t frame = 0;
        };

        struct PackTimerSlot
        {
            ID3D11Query* disjoint = nullptr;
            ID3D11Query* start = nullptr;
            ID3D11Query* end = nullptr;
            bool pending = false;
            std::uint64_t frame = 0;
        };

        struct GpuTiming
        {
            std::uint64_t frame = 0;
            double rawMs = 0.0;
            double spatialHorizontalMs = 0.0;
            double spatialVerticalMs = 0.0;
            double temporalMs = 0.0;
            double packMs = 0.0;
            double totalMs = 0.0;
        };

        struct Runtime
        {
            ID3D11Device* device = nullptr;
            ID3D11DeviceContext* deferred = nullptr;

            ID3D11ComputeShader* rawShader = nullptr;
            ID3D11ComputeShader* horizontalShader = nullptr;
            ID3D11ComputeShader* verticalShader = nullptr;
            ID3D11ComputeShader* temporalShader = nullptr;
            ID3D11ComputeShader* packShader = nullptr;

            ID3D11SamplerState* pointSampler = nullptr;
            ID3D11SamplerState* linearSampler = nullptr;
            ID3D11Buffer* parameters = nullptr;

            TextureView raw{};
            TextureView spatialTemp{};
            TextureView spatial{};
            std::array<TextureView, 2> history{};

            ID3D11Texture2D* stockCopy = nullptr;
            ID3D11ShaderResourceView* stockCopySrv = nullptr;
            ID3D11Texture2D* finalCopy = nullptr;

            ID3D11UnorderedAccessView* stockUav = nullptr;
            std::uintptr_t stockIdentity = 0;

            std::array<ComputeTimerSlot, kTimerSlots> computeTimers{};
            std::array<PackTimerSlot, kTimerSlots> packTimers{};
            std::size_t computeTimerCursor = 0;
            std::size_t packTimerCursor = 0;

            UINT width = 0;
            UINT height = 0;
            std::uint64_t deviceGeneration = 0;
        };

        struct SignalHistory
        {
            bool valid = false;
            std::uint64_t lastFrame = 0;
            std::uint64_t wave3HistoryGeneration = 0;
            std::size_t readIndex = 0;
        };

        struct PendingCapture
        {
            bool active = false;
            std::uint32_t index = 0;
            std::uint64_t targetFrame = 0;
            std::uint64_t selectedFrame = 0;
            fs::path directory{};
        };

        std::mutex g_mutex;
        std::once_flag g_initializeOnce;
        bool g_initialized = false;

        Runtime g_runtime{};
        SignalHistory g_signalHistory{};

        std::uint64_t g_finalVisibilityFrame = 0;
        std::uint64_t g_lastAppliedFrame = 0;
        ID3D11Buffer* g_currentCamera = nullptr;
        std::uint64_t g_currentCameraFrame = 0;
        std::uint64_t g_computeCount = 0;
        std::uint64_t g_applyCount = 0;
        std::uint64_t g_skipCount = 0;
        std::uint64_t g_lastStatusFrame = 0;

        std::atomic<bool> g_captureRequested{false};
        std::uint32_t g_captureIndex = 0;
        PendingCapture g_capture{};

        std::vector<GpuTiming> g_timingHistory{};

        std::uintptr_t CanonicalId(
            IUnknown* value) noexcept
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

        std::string HResultText(
            const HRESULT value)
        {
            std::ostringstream out;
            out
                << "0x"
                << std::hex
                << std::uppercase
                << static_cast<std::uint32_t>(value);
            return out.str();
        }

        bool DisabledForLegacyHarness()
        {
            wchar_t outputRoot[4]{};
            wchar_t wave4[4]{};

            const bool testHarness =
                GetEnvironmentVariableW(
                    L"GZFX_TEST_OUTPUT_ROOT",
                    outputRoot,
                    static_cast<DWORD>(
                        std::size(outputRoot))) > 0;

            const bool wave4Enabled =
                GetEnvironmentVariableW(
                    L"GZFX_TEST_WAVE4_ACTIVE",
                    wave4,
                    static_cast<DWORD>(
                        std::size(wave4))) > 0;

            return
                testHarness &&
                !wave4Enabled;
        }

        fs::path DiagnosticsRoot()
        {
            const fs::path root =
                paths::DiagnosticsRoot();

            return root.empty()
                ? fs::path{}
                : root / L"AmbientVisibilityV2";
        }

        void WriteText(
            const fs::path& path,
            const std::string& text)
        {
            std::error_code error;
            fs::create_directories(
                path.parent_path(),
                error);

            if (error)
            {
                return;
            }

            std::ofstream out(
                path,
                std::ios::out |
                    std::ios::trunc);

            if (out)
            {
                out << text;
            }
        }

        void ReleaseTexture(
            TextureView& value) noexcept
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

        void ReleaseStockViews() noexcept
        {
            if (g_runtime.stockUav != nullptr)
            {
                g_runtime.stockUav->Release();
                g_runtime.stockUav = nullptr;
            }

            g_runtime.stockIdentity = 0;
        }

        void ReleaseRuntime() noexcept
        {
            ReleaseStockViews();

            for (auto& slot : g_runtime.computeTimers)
            {
                for (auto*& timestamp : slot.timestamps)
                {
                    if (timestamp != nullptr)
                    {
                        timestamp->Release();
                        timestamp = nullptr;
                    }
                }

                if (slot.disjoint != nullptr)
                {
                    slot.disjoint->Release();
                    slot.disjoint = nullptr;
                }
            }

            for (auto& slot : g_runtime.packTimers)
            {
                if (slot.end != nullptr)
                {
                    slot.end->Release();
                    slot.end = nullptr;
                }

                if (slot.start != nullptr)
                {
                    slot.start->Release();
                    slot.start = nullptr;
                }

                if (slot.disjoint != nullptr)
                {
                    slot.disjoint->Release();
                    slot.disjoint = nullptr;
                }
            }

            if (g_runtime.finalCopy != nullptr)
            {
                g_runtime.finalCopy->Release();
            }

            if (g_runtime.stockCopySrv != nullptr)
            {
                g_runtime.stockCopySrv->Release();
            }

            if (g_runtime.stockCopy != nullptr)
            {
                g_runtime.stockCopy->Release();
            }

            for (auto& history : g_runtime.history)
            {
                ReleaseTexture(history);
            }

            ReleaseTexture(g_runtime.spatial);
            ReleaseTexture(g_runtime.spatialTemp);
            ReleaseTexture(g_runtime.raw);

            if (g_runtime.parameters != nullptr)
            {
                g_runtime.parameters->Release();
            }

            if (g_runtime.linearSampler != nullptr)
            {
                g_runtime.linearSampler->Release();
            }

            if (g_runtime.pointSampler != nullptr)
            {
                g_runtime.pointSampler->Release();
            }

            if (g_runtime.packShader != nullptr)
            {
                g_runtime.packShader->Release();
            }

            if (g_runtime.temporalShader != nullptr)
            {
                g_runtime.temporalShader->Release();
            }

            if (g_runtime.verticalShader != nullptr)
            {
                g_runtime.verticalShader->Release();
            }

            if (g_runtime.horizontalShader != nullptr)
            {
                g_runtime.horizontalShader->Release();
            }

            if (g_runtime.rawShader != nullptr)
            {
                g_runtime.rawShader->Release();
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
            g_signalHistory = {};
            g_finalVisibilityFrame = 0;

            if (g_currentCamera != nullptr)
            {
                g_currentCamera->Release();
                g_currentCamera = nullptr;
            }

            g_currentCameraFrame = 0;
        }

        const std::string& ShaderSource()
        {
            static const std::string source =
                std::string(R"GZFX(
cbuffer AVParameters : register(b0)
{
    uint2 ResourceSize;
    uint MipCount;
    uint FrameIndex;

    float RadiusWorld;
    float FalloffFraction;
    float DistributionPower;
    float VisibilityPower;

    uint SliceCount;
    uint StepsPerSlice;
    uint TemporalEnabled;
    uint Mode;

    float DepthSigma;
    float NormalPower;
    float TemporalBaseWeight;
    float TemporalMaxWeight;
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

Texture2D<float4> T0 : register(t0);
Texture2D<float4> T1 : register(t1);
Texture2D<float4> T2 : register(t2);
Texture2D<float4> T3 : register(t3);
Texture2D<float4> T4 : register(t4);

SamplerState PointSampler : register(s0);
SamplerState LinearSampler : register(s1);

#ifdef PACK_SHADER
RWTexture2D<float4> OutColor : register(u0);
#else
RWTexture2D<float> OutScalar : register(u0);
#endif

static const float PI = 3.14159265359;

float2 CurrentRatio()
{
    return DynamicResolutionParams1.xy;
}

float2 PreviousRatio()
{
    return DynamicResolutionParams1.zw;
}

bool RatioValid(float2 ratio)
{
    return
        all(isfinite(ratio)) &&
        all(ratio > float2(0.0, 0.0)) &&
        all(ratio <= float2(1.5, 1.5));
}

uint2 ActiveSize(float2 ratio)
{
    return max(
        uint2(1, 1),
        uint2(
            floor(
                float2(ResourceSize) *
                max(ratio, 0.000001) +
                0.5)));
}

float2 LogicalUv(
    uint2 pixel,
    uint2 activeSize)
{
    return
        (float2(pixel) + 0.5) /
        max(float2(activeSize), 1.0);
}

float3 ReconstructViewRay(
    float2 logicalUv)
{
    float4 clip =
        float4(
            2.0 * float2(
                logicalUv.x,
                1.0 - logicalUv.y) -
                1.0,
            1.0,
            1.0);

    float4 viewH =
        mul(
            CameraProjInverse,
            clip);

    return
        abs(viewH.w) > 0.000001
            ? viewH.xyz / viewH.w
            : 0.0;
}

float3 ReconstructView(
    float2 logicalUv,
    float viewDepth)
{
    float3 ray =
        ReconstructViewRay(
            logicalUv);

    return
        abs(ray.z) > 0.000001
            ? ray * (viewDepth / ray.z)
            : 0.0;
}

float3 DecodeNativeNormal(
    float2 encoded)
{
    float2 e =
        encoded - 0.5;

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

uint Hash32(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352d;
    value ^= value >> 15;
    value *= 0x846ca68b;
    value ^= value >> 16;
    return value;
}

float2 Noise2(
    uint2 pixel,
    uint frame)
{
    uint seed =
        Hash32(
            pixel.x * 0x9e3779b9u ^
            pixel.y * 0x85ebca6bu ^
            frame * 0xc2b2ae35u);

    uint other =
        Hash32(
            seed ^ 0x68bc21ebu);

    return float2(
        (seed & 0x00ffffffu) /
            16777216.0,
        (other & 0x00ffffffu) /
            16777216.0);
}

float SampleDepth(
    float2 logicalUv,
    float mip)
{
    if (any(logicalUv < 0.0) ||
        any(logicalUv > 1.0))
    {
        return 0.0;
    }

    return T0.SampleLevel(
        PointSampler,
        CurrentRatio() *
            logicalUv,
        mip).x;
}

#ifndef PACK_SHADER

[numthreads(8, 8, 1)]
void RawGtaoCS(
    uint3 dispatchId : SV_DispatchThreadID)
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
        OutScalar[pixel] = 1.0;
        return;
    }

    float centerDepth =
        T0.Load(
            int3(pixel, 0)).x;

    if (!isfinite(centerDepth) ||
        centerDepth <= 0.0)
    {
        OutScalar[pixel] = 1.0;
        return;
    }

    float2 currentUv =
        LogicalUv(
            pixel,
            activeSize);

    float3 centerPos =
        ReconstructView(
            currentUv,
            centerDepth);

    float centerLength =
        length(centerPos);

    if (!all(isfinite(centerPos)) ||
        centerLength <= 0.0001)
    {
        OutScalar[pixel] = 1.0;
        return;
    }

    float3 viewVec =
        -centerPos /
        centerLength;

    float3 normal =
        DecodeNativeNormal(
            T1.Load(
                int3(pixel, 0)).rg);

    if (!all(isfinite(normal)))
    {
        OutScalar[pixel] = 1.0;
        return;
    }

    float projectionPixels =
        0.5 *
        max(
            abs(CameraProj[0][0]) *
                activeSize.x,
            abs(CameraProj[1][1]) *
                activeSize.y);

    float screenRadius =
        clamp(
            RadiusWorld *
                projectionPixels /
                max(centerDepth, 1.0),
            1.5,
            512.0);

    float minS =
        min(
            1.3 /
                max(screenRadius, 1.5),
            0.35);

    float falloffRange =
        max(
            RadiusWorld *
                FalloffFraction,
            0.001);

    float falloffFrom =
        RadiusWorld -
        falloffRange;

    float2 noise =
        Noise2(
            pixel,
            FrameIndex);

    float visibility = 0.0;
    uint slices =
        clamp(
            SliceCount,
            1u,
            4u);

    uint steps =
        clamp(
            StepsPerSlice,
            1u,
            8u);

    [loop]
    for (uint slice = 0;
         slice < slices;
         ++slice)
    {
        float sliceK =
            (slice + noise.x) /
            slices;

        float phi =
            sliceK * PI;

        float cosPhi = cos(phi);
        float sinPhi = sin(phi);

        float3 directionVec =
            float3(
                cosPhi,
                sinPhi,
                0.0);

        float3 orthoDirectionVec =
            directionVec -
            dot(
                directionVec,
                viewVec) *
            viewVec;

        float orthoLength =
            length(
                orthoDirectionVec);

        if (orthoLength <= 0.00001)
        {
            visibility += 1.0;
            continue;
        }

        float3 axisVec =
            normalize(
                cross(
                    orthoDirectionVec,
                    viewVec));

        float3 projectedNormal =
            normal -
            axisVec *
                dot(
                    normal,
                    axisVec);

        float projectedLength =
            length(
                projectedNormal);

        if (projectedLength <= 0.00001)
        {
            visibility += 1.0;
            continue;
        }

        float signNorm =
            dot(
                orthoDirectionVec,
                projectedNormal) >= 0.0
                ? 1.0
                : -1.0;

        float cosNorm =
            saturate(
                dot(
                    projectedNormal,
                    viewVec) /
                projectedLength);

        float n =
            signNorm *
            acos(
                clamp(
                    cosNorm,
                    -1.0,
                    1.0));

        float lowHorizon0 =
            cos(
                n + PI * 0.5);

        float lowHorizon1 =
            cos(
                n - PI * 0.5);

        float horizon0 =
            lowHorizon0;

        float horizon1 =
            lowHorizon1;

        [loop]
        for (uint step = 0;
             step < steps;
             ++step)
        {
            float sequence =
                frac(
                    noise.y +
                    (slice +
                     step * steps) *
                        0.61803398875);

            float s =
                (step + sequence) /
                steps;

            s =
                pow(
                    max(s, 0.00001),
                    DistributionPower);

            s =
                min(
                    1.0,
                    s + minS);

            float2 offsetPixels =
                round(
                    s *
                    screenRadius *
                    float2(
                        cosPhi,
                        -sinPhi));

            float offsetLength =
                length(
                    offsetPixels);

            if (offsetLength < 0.5)
            {
                continue;
            }

            float mip =
                clamp(
                    log2(
                        max(
                            offsetLength,
                            1.0)) -
                        1.0,
                    0.0,
                    max(
                        float(MipCount) -
                            1.0,
                        0.0));

            float2 logicalOffset =
                offsetPixels /
                max(
                    float2(activeSize),
                    1.0);

            float2 uv0 =
                curren)GZFX") +
                R"GZFX(tUv +
                logicalOffset;

            float2 uv1 =
                currentUv -
                logicalOffset;

            if (all(uv0 >= 0.0) &&
                all(uv0 <= 1.0))
            {
                float depth0 =
                    SampleDepth(
                        uv0,
                        mip);

                if (depth0 > 0.0 &&
                    isfinite(depth0))
                {
                    float3 sample0 =
                        ReconstructView(
                            uv0,
                            depth0);

                    float3 delta0 =
                        sample0 -
                        centerPos;

                    float distance0 =
                        length(delta0);

                    if (distance0 > 0.0001 &&
                        distance0 <
                            RadiusWorld)
                    {
                        float weight0 =
                            distance0 <=
                                falloffFrom
                                ? 1.0
                                : saturate(
                                    (
                                        RadiusWorld -
                                        distance0
                                    ) /
                                    falloffRange);

                        float shc0 =
                            dot(
                                delta0 /
                                    distance0,
                                viewVec);

                        shc0 =
                            lerp(
                                lowHorizon0,
                                shc0,
                                weight0);

                        horizon0 =
                            max(
                                horizon0,
                                shc0);
                    }
                }
            }

            if (all(uv1 >= 0.0) &&
                all(uv1 <= 1.0))
            {
                float depth1 =
                    SampleDepth(
                        uv1,
                        mip);

                if (depth1 > 0.0 &&
                    isfinite(depth1))
                {
                    float3 sample1 =
                        ReconstructView(
                            uv1,
                            depth1);

                    float3 delta1 =
                        sample1 -
                        centerPos;

                    float distance1 =
                        length(delta1);

                    if (distance1 > 0.0001 &&
                        distance1 <
                            RadiusWorld)
                    {
                        float weight1 =
                            distance1 <=
                                falloffFrom
                                ? 1.0
                                : saturate(
                                    (
                                        RadiusWorld -
                                        distance1
                                    ) /
                                    falloffRange);

                        float shc1 =
                            dot(
                                delta1 /
                                    distance1,
                                viewVec);

                        shc1 =
                            lerp(
                                lowHorizon1,
                                shc1,
                                weight1);

                        horizon1 =
                            max(
                                horizon1,
                                shc1);
                    }
                }
            }
        }

        projectedLength =
            lerp(
                projectedLength,
                1.0,
                0.05);

        float h0 =
            -acos(
                clamp(
                    horizon1,
                    -1.0,
                    1.0));

        float h1 =
            acos(
                clamp(
                    horizon0,
                    -1.0,
                    1.0));

        float iarc0 =
            (
                cosNorm +
                2.0 * h0 * sin(n) -
                cos(2.0 * h0 - n)
            ) *
            0.25;

        float iarc1 =
            (
                cosNorm +
                2.0 * h1 * sin(n) -
                cos(2.0 * h1 - n)
            ) *
            0.25;

        visibility +=
            projectedLength *
            (iarc0 + iarc1);
    }

    visibility =
        saturate(
            visibility /
            slices);

    visibility =
        pow(
            max(
                visibility,
                0.03),
            VisibilityPower);

    OutScalar[pixel] =
        saturate(
            visibility);
}

float SpatialFilter(
    uint2 pixel,
    int2 axis)
{
    float centerDepth =
        T1.Load(
            int3(pixel, 0)).x;

    if (!isfinite(centerDepth) ||
        centerDepth <= 0.0)
    {
        return 1.0;
    }

    float3 centerNormal =
        DecodeNativeNormal(
            T2.Load(
                int3(pixel, 0)).rg);

    static const float spatial[3] =
    {
        1.0,
        0.72,
        0.32
    };

    uint2 activeSize =
        ActiveSize(
            CurrentRatio());

    float weighted = 0.0;
    float weights = 0.0;

    [unroll]
    for (int offset = -2;
         offset <= 2;
         ++offset)
    {
        int2 samplePixel =
            int2(pixel) +
            axis * offset;

        if (samplePixel.x < 0 ||
            samplePixel.y < 0 ||
            samplePixel.x >=
                int(activeSize.x) ||
            samplePixel.y >=
                int(activeSize.y))
        {
            continue;
        }

        float sampleDepth =
            T1.Load(
                int3(
                    samplePixel,
                    0)).x;

        if (!isfinite(sampleDepth) ||
            sampleDepth <= 0.0)
        {
            continue;
        }

        float relativeDepth =
            abs(
                sampleDepth -
                centerDepth) /
            max(
                centerDepth,
                1.0);

        float depthWeight =
            saturate(
                1.0 -
                relativeDepth *
                    DepthSigma);

        depthWeight *=
            depthWeight;

        float3 sampleNormal =
            DecodeNativeNormal(
                T2.Load(
                    int3(
                        samplePixel,
                        0)).rg);

        float normalWeight =
            pow(
                saturate(
                    dot(
                        centerNormal,
                        sampleNormal)),
                NormalPower);

        float w =
            spatial[abs(offset)] *
            depthWeight *
            normalWeight;

        float signal =
            T0.Load(
                int3(
                    samplePixel,
                    0)).x;

        weighted +=
            signal * w;

        weights += w;
    }

    return
        weights > 0.00001
            ? weighted / weights
            : T0.Load(
                int3(pixel, 0)).x;
}

[numthreads(8, 8, 1)]
void FilterHCS(
    uint3 dispatchId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchId.xy;

    if (any(pixel >= ResourceSize))
    {
        return;
    }

    uint2 activeSize =
        ActiveSize(
            CurrentRatio());

    OutScalar[pixel] =
        any(pixel >= activeSize)
            ? 1.0
            : SpatialFilter(
                pixel,
                int2(1, 0));
}

[numthreads(8, 8, 1)]
void FilterVCS(
    uint3 dispatchId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchId.xy;

    if (any(pixel >= ResourceSize))
    {
        return;
    }

    uint2 activeSize =
        ActiveSize(
            CurrentRatio());

    OutScalar[pixel] =
        any(pixel >= activeSize)
            ? 1.0
            : SpatialFilter(
                pixel,
                int2(0, 1));
}

[numthreads(8, 8, 1)]
void TemporalCS(
    uint3 dispatchId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchId.xy;

    if (any(pixel >= ResourceSize))
    {
        return;
    }

    float2 currentRatio =
        CurrentRatio();

    float2 previousRatio =
        PreviousRatio();

    uint2 activeSize =
        ActiveSize(
            currentRatio);

    float current =
        any(pixel >= activeSize)
            ? 1.0
            : T0.Load(
                int3(pixel, 0)).x;

    if (any(pixel >= activeSize) ||
        TemporalEnabled == 0 ||
        !RatioValid(previousRatio))
    {
        OutScalar[pixel] =
            current;
        return;
    }

    float accepted =
        T3.Load(
            int3(pixel, 0)).r;

    float confidence =
        saturate(
            T2.Load(
                int3(pixel, 0)).r);

    if (accepted < 0.5 ||
        confidence <= 0.0)
    {
        OutScalar[pixel] =
            current;
        return;
    }

    float2 currentUv =
        LogicalUv(
            pixel,
            activeSize);

    float2 motion =
        T1.Load(
            int3(pixel, 0)).rg;

    float2 previousUv =
        currentUv +
        motion;

    if (any(previousUv < 0.0) ||
        any(previousUv > 1.0))
    {
        OutScalar[pixel] =
            current;
        return;
    }

    float previous =
        T4.SampleLevel(
            LinearSampler,
            previousRatio *
                previousUv,
            0).x;

    float minimum = current;
    float maximum = current;

    [unroll]
    for (int y = -1;
         y <= 1;
         ++y)
    {
        [unroll]
        for (int x = -1;
             x <= 1;
             ++x)
        {
            int2 p =
                int2(pixel) +
                int2(x, y);

            if (p.x < 0 ||
                p.y < 0 ||
                p.x >= int(activeSize.x) ||
                p.y >= int(act)GZFX" +
                R"GZFX(iveSize.y))
            {
                continue;
            }

            float value =
                T0.Load(
                    int3(p, 0)).x;

            minimum =
                min(
                    minimum,
                    value);

            maximum =
                max(
                    maximum,
                    value);
        }
    }

    previous =
        clamp(
            previous,
            minimum,
            maximum);

    float motionPixels =
        length(
            motion *
            float2(activeSize));

    float motionFactor =
        saturate(
            1.0 -
            motionPixels /
                48.0);

    float weight =
        lerp(
            TemporalBaseWeight,
            TemporalMaxWeight,
            confidence *
                motionFactor);

    OutScalar[pixel] =
        lerp(
            current,
            previous,
            saturate(weight));
}

#else

[numthreads(8, 8, 1)]
void PackCS(
    uint3 dispatchId : SV_DispatchThreadID)
{
    uint2 pixel =
        dispatchId.xy;

    if (any(pixel >= ResourceSize))
    {
        return;
    }

    float2 ratio =
        CurrentRatio();

    uint2 activeSize =
        ActiveSize(ratio);

    bool active =
        RatioValid(ratio) &&
        all(pixel < activeSize);

    if (!active)
    {
        if (Mode == 1)
        {
            float stockOutside =
                T1.Load(
                    int3(pixel, 0)).r;

            OutColor[pixel] =
                float4(
                    stockOutside,
                    stockOutside,
                    stockOutside,
                    1.0);
        }
        else
        {
            OutColor[pixel] =
                float4(
                    1.0,
                    1.0,
                    1.0,
                    1.0);
        }

        return;
    }

    float visibility =
        saturate(
            T0.Load(
                int3(pixel, 0)).r);

    if (Mode == 1 &&
        pixel.x <
            activeSize.x / 2)
    {
        float4 stock =
            T1.Load(
                int3(pixel, 0));

        OutColor[pixel] =
            float4(
                stock.rgb,
                1.0);

        return;
    }

    OutColor[pixel] =
        float4(
            visibility,
            visibility,
            visibility,
            1.0);
}

#endif
)GZFX";

            return source;
        }

        bool Compile(
            const char* entry,
            const bool pack,
            ID3DBlob** outBlob,
            std::string& error)
        {
            if (outBlob == nullptr)
            {
                return false;
            }

            *outBlob = nullptr;

            const D3D_SHADER_MACRO packMacros[] =
            {
                {"PACK_SHADER", "1"},
                {nullptr, nullptr}
            };

            ID3DBlob* errors = nullptr;

            const auto& source =
                ShaderSource();

            const HRESULT result =
                D3DCompile(
                    source.data(),
                    source.size(),
                    "GZFXAmbientVisibilityV2",
                    pack
                        ? packMacros
                        : nullptr,
                    nullptr,
                    entry,
                    "cs_5_0",
                    D3DCOMPILE_OPTIMIZATION_LEVEL3,
                    0,
                    outBlob,
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
                    error =
                        "D3DCompile failed without diagnostic text";
                }
            }

            if (errors != nullptr)
            {
                errors->Release();
            }

            return SUCCEEDED(result);
        }

        bool CreateScalarTexture(
            ID3D11Device* device,
            const UINT width,
            const UINT height,
            TextureView& out,
            std::string& error)
        {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = width;
            desc.Height = height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format =
                DXGI_FORMAT_R16_FLOAT;
            desc.SampleDesc.Count = 1;
            desc.Usage =
                D3D11_USAGE_DEFAULT;
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
                    "CreateScalarTexture=" +
                    HResultText(result);
                ReleaseTexture(out);
                return false;
            }

            return true;
        }

        bool CreateCopyTextures(
            ID3D11Device* device,
            const UINT width,
            const UINT height,
            std::string& error)
        {
            D3D11_TEXTURE2D_DESC stock{};
            stock.Width = width;
            stock.Height = height;
            stock.MipLevels = 1;
            stock.ArraySize = 1;
            stock.Format =
                DXGI_FORMAT_R8G8B8A8_UNORM;
            stock.SampleDesc.Count = 1;
            stock.Usage =
                D3D11_USAGE_DEFAULT;
            stock.BindFlags =
                D3D11_BIND_SHADER_RESOURCE;

            HRESULT result =
                device->CreateTexture2D(
                    &stock,
                    nullptr,
                    &g_runtime.stockCopy);

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateShaderResourceView(
                        g_runtime.stockCopy,
                        nullptr,
                        &g_runtime.stockCopySrv);
            }

            D3D11_TEXTURE2D_DESC finalDesc =
                stock;

            finalDesc.BindFlags = 0;

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateTexture2D(
                        &finalDesc,
                        nullptr,
                        &g_runtime.finalCopy);
            }

            if (FAILED(result))
            {
                error =
                    "CreateCopyTextures=" +
                    HResultText(result);
                return false;
            }

            return true;
        }

        bool CreateTimers(
            ID3D11Device* device)
        {
            D3D11_QUERY_DESC disjoint{};
            disjoint.Query =
                D3D11_QUERY_TIMESTAMP_DISJOINT;

            D3D11_QUERY_DESC timestamp{};
            timestamp.Query =
                D3D11_QUERY_TIMESTAMP;

            for (auto& slot : g_runtime.computeTimers)
            {
                if (FAILED(
                        device->CreateQuery(
                            &disjoint,
                            &slot.disjoint)))
                {
                    return false;
                }

                for (auto*& value : slot.timestamps)
                {
                    if (FAILED(
                            device->CreateQuery(
                                &timestamp,
                                &value)))
                    {
                        return false;
                    }
                }
            }

            for (auto& slot : g_runtime.packTimers)
            {
                if (FAILED(
                        device->CreateQuery(
                            &disjoint,
                            &slot.disjoint)) ||
                    FAILED(
                        device->CreateQuery(
                            &timestamp,
                            &slot.start)) ||
                    FAILED(
                        device->CreateQuery(
                            &timestamp,
                            &slot.end)))
                {
                    return false;
                }
            }

            return true;
        }

        bool EnsureRuntime(
            const execution::ExecutionLease& lease,
            const UINT width,
            const UINT height,
            std::string& error,
            bool* rebuilt)
        {
            if (rebuilt != nullptr)
            {
                *rebuilt = false;
            }

            if (!lease.valid ||
                lease.device == nullptr ||
                width == 0 ||
                height == 0)
            {
                error =
                    "invalid execution lease or dimensions";
                return false;
            }

            if (g_runtime.device == lease.device &&
                g_runtime.deviceGeneration ==
                    lease.deviceGeneration &&
                g_runtime.width == width &&
                g_runtime.height == height &&
                g_runtime.deferred != nullptr)
            {
                return true;
            }

            ReleaseRuntime();

            g_runtime.device =
                lease.device;
            g_runtime.device->AddRef();

            g_runtime.deviceGeneration =
                lease.deviceGeneration;
            g_runtime.width = width;
            g_runtime.height = height;

            HRESULT result =
                lease.device->CreateDeferredContext(
                    0,
                    &g_runtime.deferred);

            ID3DBlob* raw = nullptr;
            ID3DBlob* horizontal = nullptr;
            ID3DBlob* vertical = nullptr;
            ID3DBlob* temporal = nullptr;
            ID3DBlob* pack = nullptr;

            if (SUCCEEDED(result) &&
                !Compile(
                    "RawGtaoCS",
                    false,
                    &raw,
                    error))
            {
                result = E_FAIL;
            }

            if (SUCCEEDED(result) &&
                !Compile(
                    "FilterHCS",
                    false,
                    &horizontal,
                    error))
            {
                result = E_FAIL;
            }

            if (SUCCEEDED(result) &&
                !Compile(
                    "FilterVCS",
                    false,
                    &vertical,
                    error))
            {
                result = E_FAIL;
            }

            if (SUCCEEDED(result) &&
                !Compile(
                    "TemporalCS",
                    false,
                    &temporal,
                    error))
            {
                result = E_FAIL;
            }

            if (SUCCEEDED(result) &&
                !Compile(
                    "PackCS",
                    true,
                    &pack,
                    error))
            {
                result = E_FAIL;
            }

            if (SUCCEEDED(result))
            {
                result =
                    lease.device->CreateComputeShader(
                        raw->GetBufferPointer(),
                        raw->GetBufferSize(),
                        nullptr,
                        &g_runtime.rawShader);
            }

            if (SUCCEEDED(result))
            {
                result =
                    lease.device->CreateComputeShader(
                        horizontal->GetBufferPointer(),
                        horizontal->GetBufferSize(),
                        nullptr,
                        &g_runtime.horizontalShader);
            }

            if (SUCCEEDED(result))
            {
                result =
                    lease.device->CreateComputeShader(
                        vertical->GetBufferPointer(),
                        vertical->GetBufferSize(),
                        nullptr,
                        &g_runtime.verticalShader);
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
                        pack->GetBufferPointer(),
                        pack->GetBufferSize(),
                        nullptr,
                        &g_runtime.packShader);
            }

            if (raw != nullptr) raw->Release();
            if (horizontal != nullptr) horizontal->Release();
            if (vertical != nullptr) vertical->Release();
            if (temporal != nullptr) temporal->Release();
            if (pack != nullptr) pack->Release();

            D3D11_SAMPLER_DESC point{};
            point.Filter =
                D3D11_FILTER_MIN_MAG_MIP_POINT;
            point.AddressU =
                D3D11_TEXTURE_ADDRESS_CLAMP;
            point.AddressV =
                D3D11_TEXTURE_ADDRESS_CLAMP;
            point.AddressW =
                D3D11_TEXTURE_ADDRESS_CLAMP;
            point.MaxLOD =
                D3D11_FLOAT32_MAX;

            if (SUCCEEDED(result))
            {
                result =
                    lease.device->CreateSamplerState(
                        &point,
                        &g_runtime.pointSampler);
            }

            D3D11_SAMPLER_DESC linear =
                point;

            linear.Filter =
                D3D11_FILTER_MIN_MAG_MIP_LINEAR;

            if (SUCCEEDED(result))
            {
                result =
                    lease.device->CreateSamplerState(
                        &linear,
                        &g_runtime.linearSampler);
            }

            D3D11_BUFFER_DESC cb{};
            cb.ByteWidth =
                sizeof(Parameters);
            cb.Usage =
                D3D11_USAGE_DEFAULT;
            cb.BindFlags =
                D3D11_BIND_CONSTANT_BUFFER;

            if (SUCCEEDED(result))
            {
                result =
                    lease.device->CreateBuffer(
                        &cb,
                        nullptr,
                        &g_runtime.parameters);
            }

            if (FAILED(result))
            {
                if (error.empty())
                {
                    error =
                        "Wave4 runtime shader/state creation=" +
                        HResultText(result);
                }

                ReleaseRuntime();
                return false;
            }

            if (!CreateScalarTexture(
                    lease.device,
                    width,
                    height,
                    g_runtime.raw,
                    error) ||
                !CreateScalarTexture(
                    lease.device,
                    width,
                    height,
                    g_runtime.spatialTemp,
                    error) ||
                !CreateScalarTexture(
                    lease.device,
                    width,
                    height,
                    g_runtime.spatial,
                    error))
            {
                ReleaseRuntime();
                return false;
            }

            for (auto& history :
                 g_runtime.history)
            {
                if (!CreateScalarTexture(
                        lease.device,
                        width,
                        height,
                        history,
                        error))
                {
                    ReleaseRuntime();
                    return false;
                }
            }

            if (!CreateCopyTextures(
                    lease.device,
                    width,
                    height,
                    error) ||
                !CreateTimers(
                    lease.device))
            {
                ReleaseRuntime();
                return false;
            }

            if (rebuilt != nullptr)
            {
                *rebuilt = true;
            }

            return true;
        }

        void ClearComputeBindings(
            ID3D11DeviceContext* deferred)
        {
            std::array<
                ID3D11ShaderResourceView*,
                5> nullSrvs{};

            ID3D11UnorderedAccessView*
                nullUav = nullptr;

            ID3D11Buffer*
                nullCb = nullptr;

            std::array<
                ID3D11SamplerState*,
                2> nullSamplers{};

            deferred->CSSetShaderResources(
                0,
                static_cast<UINT>(
                    nullSrvs.size()),
                nullSrvs.data());

            deferred->CSSetUnorderedAccessViews(
                0,
                1,
                &nullUav,
                nullptr);

            deferred->CSSetConstantBuffers(
                0,
                1,
                &nullCb);

            deferred->CSSetConstantBuffers(
                12,
                1,
                &nullCb);

            deferred->CSSetSamplers(
                0,
                static_cast<UINT>(
                    nullSamplers.size()),
                nullSamplers.data());

            deferred->CSSetShader(
                nullptr,
                nullptr,
                0);
        }

        void TrimTimingHistory()
        {
            if (g_timingHistory.size() <=
                kTimingHistoryLimit)
            {
                return;
            }

            g_timingHistory.erase(
                g_timingHistory.begin(),
                g_timingHistory.begin() +
                    static_cast<std::ptrdiff_t>(
                        g_timingHistory.size() -
                        kTimingHistoryLimit));
        }

        GpuTiming* FindTiming(
            const std::uint64_t frame)
        {
            for (auto& sample :
                 g_timingHistory)
            {
                if (sample.frame == frame)
                {
                    return &sample;
                }
            }

            return nullptr;
        }

        void ResolveTimers(
            ID3D11DeviceContext* immediate)
        {
            if (immediate == nullptr)
            {
                return;
            }

            for (auto& slot :
                 g_runtime.computeTimers)
            {
                if (!slot.pending)
                {
                    continue;
                }

                D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};

                if (immediate->GetData(
                        slot.disjoint,
                        &disjoint,
                        sizeof(disjoint),
                        D3D11_ASYNC_GETDATA_DONOTFLUSH) !=
                    S_OK)
                {
                    continue;
                }

                std::array<
                    UINT64,
                    kComputeTimestampCount> values{};

                bool ready = true;

                for (std::size_t i = 0;
                     i < values.size();
                     ++i)
                {
                    if (immediate->GetData(
                            slot.timestamps[i],
                            &values[i],
                            sizeof(values[i]),
                            D3D11_ASYNC_GETDATA_DONOTFLUSH) !=
                        S_OK)
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

                const double toMs =
                    1000.0 /
                    static_cast<double>(
                        disjoint.Frequency);

                const auto delta =
                    [&](const std::size_t a,
                        const std::size_t b)
                    {
                        return
                            static_cast<double>(
                                values[b] -
                                values[a]) *
                            toMs;
                    };

                GpuTiming timing{};
                timing.frame = slot.frame;
                timing.rawMs =
                    delta(0, 1);
                timing.spatialHorizontalMs =
                    delta(1, 2);
                timing.spatialVerticalMs =
                    delta(2, 3);
                timing.temporalMs =
                    delta(3, 4);
                timing.totalMs =
                    timing.rawMs +
                    timing.spatialHorizontalMs +
                    timing.spatialVerticalMs +
                    timing.temporalMs;

                if (auto* existing =
                        FindTiming(slot.frame))
                {
                    const double pack =
                        existing->packMs;
                    *existing = timing;
                    existing->packMs = pack;
                    existing->totalMs += pack;
                }
                else
                {
                    g_timingHistory.push_back(
                        timing);
                }
            }

            for (auto& slot :
                 g_runtime.packTimers)
            {
                if (!slot.pending)
                {
                    continue;
                }

                D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
                UINT64 start = 0;
                UINT64 end = 0;

                if (immediate->GetData(
                        slot.disjoint,
                        &disjoint,
                        sizeof(disjoint),
                        D3D11_ASYNC_GETDATA_DONOTFLUSH) !=
                        S_OK ||
                    immediate->GetData(
                        slot.start,
                        &start,
                        sizeof(start),
                        D3D11_ASYNC_GETDATA_DONOTFLUSH) !=
                        S_OK ||
                    immediate->GetData(
                        slot.end,
                        &end,
                        sizeof(end),
                        D3D11_ASYNC_GETDATA_DONOTFLUSH) !=
                        S_OK)
                {
                    continue;
                }

                slot.pending = false;

                if (disjoint.Disjoint ||
                    disjoint.Frequency == 0 ||
                    end < start)
                {
                    continue;
                }

                const double packMs =
                    static_cast<double>(
                        end - start) *
                    1000.0 /
                    static_cast<double>(
                        disjoint.Frequency);

                if (auto* existing =
                        FindTiming(slot.frame))
                {
                    existing->packMs =
                        packMs;

                    existing->totalMs =
                        existing->rawMs +
                        existing->spatialHorizontalMs +
                        existing->spatialVerticalMs +
                        existing->temporalMs +
                        packMs;
                }
                else
                {
                    GpuTiming timing{};
                    timing.frame =
                        slot.frame;
                    timing.packMs =
                        packMs;
                    timing.totalMs =
                        packMs;
                    g_timingHistory.push_back(
                        timing);
                }
            }

            std::sort(
                g_timingHistory.begin(),
                g_timingHistory.end(),
                [](const GpuTiming& a,
                   const GpuTiming& b)
                {
                    return a.frame < b.frame;
                });

            TrimTimingHistory();
        }

        ComputeTimerSlot* BeginComputeTimer(
            ID3D11DeviceContext* deferred,
            const std::uint64_t frame)
        {
            auto& slot =
                g_runtime.computeTimers[
                    g_runtime.computeTimerCursor];

            g_runtime.computeTimerCursor =
                (g_runtime.computeTimerCursor + 1) %
                kTimerSlots;

            if (slot.pending)
            {
                return nullptr;
            }

            slot.frame = frame;

            deferred->Begin(
                slot.disjoint);

            deferred->End(
                slot.timestamps[0]);

            return &slot;
        }

        void StampComputeTimer(
            ID3D11DeviceContext* deferred,
            ComputeTimerSlot* slot,
            const std::size_t index)
        {
            if (slot != nullptr &&
                index <
                    slot->timestamps.size())
            {
                deferred->End(
                    slot->timestamps[index]);
            }
        }

        void FinishComputeTimer(
            ID3D11DeviceContext* deferred,
            ComputeTimerSlot* slot)
        {
            if (slot == nullptr)
            {
                return;
            }

            deferred->End(
                slot->disjoint);

            slot->pending = true;
        }

        PackTimerSlot* BeginPackTimer(
            ID3D11DeviceContext* deferred,
            const std::uint64_t frame)
        {
            auto& slot =
                g_runtime.packTimers[
                    g_runtime.packTimerCursor];

            g_runtime.packTimerCursor =
                (g_runtime.packTimerCursor + 1) %
                kTimerSlots;

            if (slot.pending)
            {
                return nullptr;
            }

            slot.frame = frame;

            deferred->Begin(
                slot.disjoint);

            deferred->End(
                slot.start);

            return &slot;
        }

        void FinishPackTimer(
            ID3D11DeviceContext* deferred,
            PackTimerSlot* slot)
        {
            if (slot == nullptr)
            {
                return;
            }

            deferred->End(
                slot->end);

            deferred->End(
                slot->disjoint);

            slot->pending = true;
        }

        Parameters MakeParameters(
            const native_input::GeometryInputs& inputs,
            const wave3::FrameView& frameView,
            const bool temporal,
            const std::uint32_t mode)
        {
            Parameters p{};
            p.width = inputs.width;
            p.height = inputs.height;
            p.mipCount =
                frameView.mipCount;
            p.frameIndex =
                static_cast<std::uint32_t>(
                    inputs.frame);
            p.temporalEnabled =
                temporal ? 1u : 0u;
            p.mode = mode;
            return p;
        }

        bool RecordVisibilityCommands(
            const native_input::GeometryInputs& inputs,
            const wave3::FrameView& frameView,
            const std::size_t previousIndex,
            const std::size_t writeIndex,
            const bool temporal,
            ID3D11CommandList** outCommands,
            std::string& error)
        {
            if (outCommands == nullptr ||
                g_runtime.deferred == nullptr)
            {
                return false;
            }

            *outCommands = nullptr;

            auto* deferred =
                g_runtime.deferred;

            const Parameters parameters =
                MakeParameters(
                    inputs,
                    frameView,
                    temporal,
                    state::GetDevelopmentMode());

            deferred->ClearState();

            deferred->UpdateSubresource(
                g_runtime.parameters,
                0,
                nullptr,
                &parameters,
                0,
                0);

            ID3D11Buffer* cb =
                g_runtime.parameters;

            ID3D11Buffer* camera =
                inputs.cameraBuffer;

            ID3D11SamplerState* samplers[2] =
            {
                g_runtime.pointSampler,
                g_runtime.linearSampler
            };

            deferred->CSSetConstantBuffers(
                0,
                1,
                &cb);

            deferred->CSSetConstantBuffers(
                12,
                1,
                &camera);

            deferred->CSSetSamplers(
                0,
                2,
                samplers);

            ComputeTimerSlot* timer =
                BeginComputeTimer(
                    deferred,
                    inputs.frame);

            deferred->CSSetShader(
                g_runtime.rawShader,
                nullptr,
                0);

            ID3D11ShaderResourceView* rawSrvs[2] =
            {
                frameView.depthHierarchy,
                inputs.nativeNormal.srv
            };

            deferred->CSSetShaderResources(
                0,
                2,
                rawSrvs);

            ID3D11UnorderedAccessView* uav =
                g_runtime.raw.uav;

            deferred->CSSetUnorderedAccessViews(
                0,
                1,
                &uav,
                nullptr);

            deferred->Dispatch(
                (inputs.width +
                    kThreadsX - 1) /
                    kThreadsX,
                (inputs.height +
                    kThreadsY - 1) /
                    kThreadsY,
                1);

            ClearComputeBindings(
                deferred);

            StampComputeTimer(
                deferred,
                timer,
                1);

            deferred->CSSetConstantBuffers(
                0,
                1,
                &cb);

            deferred->CSSetConstantBuffers(
                12,
                1,
                &camera);

            deferred->CSSetSamplers(
                0,
                2,
                samplers);

            deferred->CSSetShader(
                g_runtime.horizontalShader,
                nullptr,
                0);

            ID3D11ShaderResourceView* hSrvs[3] =
            {
                g_runtime.raw.srv,
                frameView.depthHierarchy,
                inputs.nativeNormal.srv
            };

            deferred->CSSetShaderResources(
                0,
                3,
                hSrvs);

            uav =
                g_runtime.spatialTemp.uav;

            deferred->CSSetUnorderedAccessViews(
                0,
                1,
                &uav,
                nullptr);

            deferred->Dispatch(
                (inputs.width +
                    kThreadsX - 1) /
                    kThreadsX,
                (inputs.height +
                    kThreadsY - 1) /
                    kThreadsY,
                1);

            ClearComputeBindings(
                deferred);

            StampComputeTimer(
                deferred,
                timer,
                2);

            deferred->CSSetConstantBuffers(
                0,
                1,
                &cb);

            deferred->CSSetConstantBuffers(
                12,
                1,
                &camera);

            deferred->CSSetSamplers(
                0,
                2,
                samplers);

            deferred->CSSetShader(
                g_runtime.verticalShader,
                nullptr,
                0);

            ID3D11ShaderResourceView* vSrvs[3] =
            {
                g_runtime.spatialTemp.srv,
                frameView.depthHierarchy,
                inputs.nativeNormal.srv
            };

            deferred->CSSetShaderResources(
                0,
                3,
                vSrvs);

            uav =
                g_runtime.spatial.uav;

            deferred->CSSetUnorderedAccessViews(
                0,
                1,
                &uav,
                nullptr);

            deferred->Dispatch(
                (inputs.width +
                    kThreadsX - 1) /
                    kThreadsX,
                (inputs.height +
                    kThreadsY - 1) /
                    kThreadsY,
                1);

            ClearComputeBindings(
                deferred);

            StampComputeTimer(
                deferred,
                timer,
                3);

            deferred->CSSetConstantBuffers(
                0,
                1,
                &cb);

            deferred->CSSetConstantBuffers(
                12,
                1,
                &camera);

            deferred->CSSetSamplers(
                0,
                2,
                samplers);

            deferred->CSSetShader(
                g_runtime.temporalShader,
                nullptr,
                0);

            ID3D11ShaderResourceView* temporalSrvs[5] =
            {
                g_runtime.spatial.srv,
                inputs.motion.srv,
                frameView.geometryConfidence,
                frameView.reprojectionMask,
                g_runtime.history[
                    previousIndex].srv
            };

            deferred->CSSetShaderResources(
                0,
                5,
                temporalSrvs);

            uav =
                g_runtime.history[
                    writeIndex].uav;

            deferred->CSSetUnorderedAccessViews(
                0,
                1,
                &uav,
                nullptr);

            deferred->Dispatch(
                (inputs.width +
                    kThreadsX - 1) /
                    kThreadsX,
                (inputs.height +
                    kThreadsY - 1) /
                    kThreadsY,
                1);

            ClearComputeBindings(
                deferred);

            StampComputeTimer(
                deferred,
                timer,
                4);

            FinishComputeTimer(
                deferred,
                timer);

            const HRESULT result =
                deferred->FinishCommandList(
                    FALSE,
                    outCommands);

            if (FAILED(result) ||
                *outCommands == nullptr)
            {
                error =
                    "Finish visibility command list=" +
                    HResultText(result);
                return false;
            }

            return true;
        }

        bool EnsureStockUav(
            ID3D11Device* device,
            ID3D11Texture2D* texture,
            const std::uintptr_t identity,
            std::string& error)
        {
            if (g_runtime.stockUav != nullptr &&
                g_runtime.stockIdentity ==
                    identity)
            {
                return true;
            }

            ReleaseStockViews();

            D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};
            desc.Format =
                DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.ViewDimension =
                D3D11_UAV_DIMENSION_TEXTURE2D;
            desc.Texture2D.MipSlice = 0;

            const HRESULT result =
                device->CreateUnorderedAccessView(
                    texture,
                    &desc,
                    &g_runtime.stockUav);

            if (FAILED(result) ||
                g_runtime.stockUav == nullptr)
            {
                error =
                    "Create stock AO UAV=" +
                    HResultText(result);
                return false;
            }

            g_runtime.stockIdentity =
                identity;
            return true;
        }

        bool RecordPackCommands(
            const std::uint64_t frame,
            const std::uint32_t mode,
            ID3D11Buffer* camera,
            ID3D11Texture2D* stockTexture,
            const bool needStockCopy,
            const bool capture,
            ID3D11CommandList** outCommands,
            std::string& error)
        {
            if (outCommands == nullptr ||
                stockTexture == nullptr ||
                camera == nullptr)
            {
                error =
                    "missing stock texture or camera for AO pack";
                return false;
            }

            *outCommands = nullptr;

            auto* deferred =
                g_runtime.deferred;

            Parameters parameters{};
            parameters.width =
                g_runtime.width;
            parameters.height =
                g_runtime.height;
            parameters.frameIndex =
                static_cast<std::uint32_t>(
                    frame);
            parameters.mode =
                mode;

            deferred->ClearState();

            deferred->OMSetRenderTargets(
                0,
                nullptr,
                nullptr);

            if (needStockCopy)
            {
                deferred->CopyResource(
                    g_runtime.stockCopy,
                    stockTexture);
            }

            deferred->UpdateSubresource(
                g_runtime.parameters,
                0,
                nullptr,
                &parameters,
                0,
                0);

            ID3D11Buffer* cb =
                g_runtime.parameters;

            deferred->CSSetConstantBuffers(
                0,
                1,
                &cb);

            deferred->CSSetConstantBuffers(
                12,
                1,
                &camera);

            PackTimerSlot* timer =
                BeginPackTimer(
                    deferred,
                    frame);

            deferred->CSSetShader(
                g_runtime.packShader,
                nullptr,
                0);

            ID3D11ShaderResourceView* srvs[2] =
            {
                g_runtime.history[
                    g_signalHistory.readIndex].srv,
                needStockCopy
                    ? g_runtime.stockCopySrv
                    : nullptr
            };

            deferred->CSSetShaderResources(
                0,
                2,
                srvs);

            ID3D11UnorderedAccessView* uav =
                g_runtime.stockUav;

            deferred->CSSetUnorderedAccessViews(
                0,
                1,
                &uav,
                nullptr);

            deferred->Dispatch(
                (g_runtime.width +
                    kThreadsX - 1) /
                    kThreadsX,
                (g_runtime.height +
                    kThreadsY - 1) /
                    kThreadsY,
                1);

            ClearComputeBindings(
                deferred);

            FinishPackTimer(
                deferred,
                timer);

            if (capture)
            {
                deferred->CopyResource(
                    g_runtime.finalCopy,
                    stockTexture);
            }

            const HRESULT result =
                deferred->FinishCommandList(
                    FALSE,
                    outCommands);

            if (FAILED(result) ||
                *outCommands == nullptr)
            {
                error =
                    "Finish AO injection command list=" +
                    HResultText(result);
                return false;
            }

            return true;
        }

        float HalfToFloat(
            const std::uint16_t half) noexcept
        {
            const std::uint32_t sign =
                (half >> 15u) & 1u;

            const std::uint32_t exponent =
                (half >> 10u) &
                0x1Fu;

            const std::uint32_t mantissa =
                half & 0x3FFu;

            std::uint32_t bits = 0;

            if (exponent == 0)
            {
                if (mantissa == 0)
                {
                    bits =
                        sign << 31u;
                }
                else
                {
                    std::uint32_t m =
                        mantissa;

                    int e = -14;

                    while ((m & 0x400u) == 0)
                    {
                        m <<= 1u;
                        --e;
                    }

                    m &= 0x3FFu;

                    bits =
                        (sign << 31u) |
                        (
                            static_cast<std::uint32_t>(
                                e + 127) <<
                            23u
                        ) |
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
                    (
                        (exponent -
                         15u +
                         127u) <<
                        23u
                    ) |
                    (mantissa << 13u);
            }

            return std::bit_cast<float>(
                bits);
        }

        bool ReadR16(
            ID3D11Device* device,
            ID3D11DeviceContext* immediate,
            ID3D11Texture2D* source,
            std::vector<float>& values)
        {
            if (source == nullptr)
            {
                return false;
            }

            D3D11_TEXTURE2D_DESC desc{};
            source->GetDesc(&desc);

            if (desc.Format !=
                    DXGI_FORMAT_R16_FLOAT ||
                desc.Width !=
                    g_runtime.width ||
                desc.Height !=
                    g_runtime.height)
            {
                return false;
            }

            D3D11_TEXTURE2D_DESC staging =
                desc;

            staging.Usage =
                D3D11_USAGE_STAGING;
            staging.BindFlags = 0;
            staging.CPUAccessFlags =
                D3D11_CPU_ACCESS_READ;
            staging.MiscFlags = 0;

            ID3D11Texture2D* copy = nullptr;

            if (FAILED(
                    device->CreateTexture2D(
                        &staging,
                        nullptr,
                        &copy)) ||
                copy == nullptr)
            {
                return false;
            }

            immediate->CopyResource(
                copy,
                source);

            D3D11_MAPPED_SUBRESOURCE mapped{};

            if (FAILED(
                    immediate->Map(
                        copy,
                        0,
                        D3D11_MAP_READ,
                        0,
                        &mapped)))
            {
                copy->Release();
                return false;
            }

            values.resize(
                static_cast<std::size_t>(
                    desc.Width) *
                desc.Height);

            for (UINT y = 0;
                 y < desc.Height;
                 ++y)
            {
                const auto* row =
                    reinterpret_cast<
                        const std::uint16_t*>(
                            static_cast<
                                const std::uint8_t*>(
                                    mapped.pData) +
                            static_cast<std::size_t>(y) *
                                mapped.RowPitch);

                for (UINT x = 0;
                     x < desc.Width;
                     ++x)
                {
                    values[
                        static_cast<std::size_t>(y) *
                            desc.Width +
                        x] =
                        HalfToFloat(
                            row[x]);
                }
            }

            immediate->Unmap(
                copy,
                0);

            copy->Release();
            return true;
        }

        bool ReadRgba8Red(
            ID3D11Device* device,
            ID3D11DeviceContext* immediate,
            ID3D11Texture2D* source,
            std::vector<float>& values)
        {
            if (source == nullptr)
            {
                return false;
            }

            D3D11_TEXTURE2D_DESC desc{};
            source->GetDesc(&desc);

            if (desc.Format !=
                    DXGI_FORMAT_R8G8B8A8_UNORM ||
                desc.Width !=
                    g_runtime.width ||
                desc.Height !=
                    g_runtime.height)
            {
                return false;
            }

            D3D11_TEXTURE2D_DESC staging =
                desc;

            staging.Usage =
                D3D11_USAGE_STAGING;
            staging.BindFlags = 0;
            staging.CPUAccessFlags =
                D3D11_CPU_ACCESS_READ;
            staging.MiscFlags = 0;

            ID3D11Texture2D* copy = nullptr;

            if (FAILED(
                    device->CreateTexture2D(
                        &staging,
                        nullptr,
                        &copy)) ||
                copy == nullptr)
            {
                return false;
            }

            immediate->CopyResource(
                copy,
                source);

            D3D11_MAPPED_SUBRESOURCE mapped{};

            if (FAILED(
                    immediate->Map(
                        copy,
                        0,
                        D3D11_MAP_READ,
                        0,
                        &mapped)))
            {
                copy->Release();
                return false;
            }

            values.resize(
                static_cast<std::size_t>(
                    desc.Width) *
                desc.Height);

            for (UINT y = 0;
                 y < desc.Height;
                 ++y)
            {
                const auto* row =
                    static_cast<
                        const std::uint8_t*>(
                            mapped.pData) +
                    static_cast<std::size_t>(y) *
                        mapped.RowPitch;

                for (UINT x = 0;
                     x < desc.Width;
                     ++x)
                {
                    values[
                        static_cast<std::size_t>(y) *
                            desc.Width +
                        x] =
                        row[x * 4] /
                        255.0f;
                }
            }

            immediate->Unmap(
                copy,
                0);

            copy->Release();
            return true;
        }

        bool WriteGrayBmp(
            const fs::path& path,
            const std::vector<float>& values)
        {
            if (values.size() !=
                static_cast<std::size_t>(
                    g_runtime.width) *
                g_runtime.height)
            {
                return false;
            }

            constexpr UINT outWidth = 640;
            constexpr UINT outHeight = 360;

            BITMAPFILEHEADER file{};
            BITMAPINFOHEADER info{};

            const DWORD imageBytes =
                outWidth *
                outHeight *
                4;

            file.bfType = 0x4D42;
            file.bfOffBits =
                sizeof(BITMAPFILEHEADER) +
                sizeof(BITMAPINFOHEADER);
            file.bfSize =
                file.bfOffBits +
                imageBytes;

            info.biSize =
                sizeof(BITMAPINFOHEADER);
            info.biWidth =
                static_cast<LONG>(
                    outWidth);
            info.biHeight =
                static_cast<LONG>(
                    outHeight);
            info.biPlanes = 1;
            info.biBitCount = 32;
            info.biCompression = BI_RGB;
            info.biSizeImage =
                imageBytes;

            std::ofstream out(
                path,
                std::ios::binary |
                    std::ios::trunc);

            if (!out)
            {
                return false;
            }

            out.write(
                reinterpret_cast<
                    const char*>(&file),
                sizeof(file));

            out.write(
                reinterpret_cast<
                    const char*>(&info),
                sizeof(info));

            std::vector<std::uint8_t>
                row(
                    outWidth * 4);

            for (UINT y = 0;
                 y < outHeight;
                 ++y)
            {
                const UINT sourceY =
                    std::min(
                        g_runtime.height - 1,
                        (outHeight - 1 - y) *
                            g_runtime.height /
                            outHeight);

                for (UINT x = 0;
                     x < outWidth;
                     ++x)
                {
                    const UINT sourceX =
                        std::min(
                            g_runtime.width - 1,
                            x *
                                g_runtime.width /
                                outWidth);

                    const float value =
                        std::clamp(
                            values[
                                static_cast<std::size_t>(
                                    sourceY) *
                                    g_runtime.width +
                                sourceX],
                            0.0f,
                            1.0f);

                    const auto byte =
                        static_cast<std::uint8_t>(
                            value *
                                255.0f +
                            0.5f);

                    row[x * 4 + 0] = byte;
                    row[x * 4 + 1] = byte;
                    row[x * 4 + 2] = byte;
                    row[x * 4 + 3] = 255;
                }

                out.write(
                    reinterpret_cast<
                        const char*>(
                            row.data()),
                    static_cast<
                        std::streamsize>(
                            row.size()));
            }

            return static_cast<bool>(out);
        }

        std::string SignalStats(
            const char* name,
            std::vector<float> values)
        {
            if (values.empty())
            {
                return
                    std::string(name) +
                    "_status=empty\n";
            }

            for (auto& value : values)
            {
                value =
                    std::clamp(
                        value,
                        0.0f,
                        1.0f);
            }

            std::sort(
                values.begin(),
                values.end());

            const auto percentile =
                [&](const double p)
                {
                    const std::size_t index =
                        static_cast<std::size_t>(
                            std::llround(
                                p *
                                static_cast<double>(
                                    values.size() -
                                    1)));

                    return values[index];
                };

            const double sum =
                std::accumulate(
                    values.begin(),
                    values.end(),
                    0.0);

            const auto fractionBelow =
                [&](const float threshold)
                {
                    return
                        static_cast<double>(
                            std::lower_bound(
                                values.begin(),
                                values.end(),
                                threshold) -
                            values.begin()) /
                        static_cast<double>(
                            values.size());
                };

            const auto fractionAbove =
                [&](const float threshold)
                {
                    return
                        static_cast<double>(
                            values.end() -
                            std::upper_bound(
                                values.begin(),
                                values.end(),
                                threshold)) /
                        static_cast<double>(
                            values.size());
                };

            std::ostringstream out;
            out
                << std::fixed
                << std::setprecision(6)
                << name << "_min="
                << values.front()
                << "\n"
                << name << "_max="
                << values.back()
                << "\n"
                << name << "_mean="
                << sum /
                    static_cast<double>(
                        values.size())
                << "\n"
                << name << "_p01="
                << percentile(0.01)
                << "\n"
                << name << "_p50="
                << percentile(0.50)
                << "\n"
                << name << "_p95="
                << percentile(0.95)
                << "\n";

            if (std::string(name) ==
                "final_visibility")
            {
                out
                    << name
                    << "_fraction_lt_025="
                    << fractionBelow(0.25f)
                    << "\n"
                    << name
                    << "_fraction_lt_050="
                    << fractionBelow(0.50f)
                    << "\n"
                    << name
                    << "_fraction_lt_075="
                    << fractionBelow(0.75f)
                    << "\n"
                    << name
                    << "_fraction_gt_095="
                    << fractionAbove(0.95f)
                    << "\n";
            }

            return out.str();
        }

        void WriteTimingSummary(
            const fs::path& directory)
        {
            if (g_timingHistory.empty())
            {
                WriteText(
                    directory /
                        L"gpu_timing_summary.txt",
                    "status=no_resolved_samples\n");
                return;
            }

            std::vector<double> totals;
            totals.reserve(
                g_timingHistory.size());

            double raw = 0.0;
            double h = 0.0;
            double v = 0.0;
            double temporal = 0.0;
            double pack = 0.0;

            for (const auto& sample :
                 g_timingHistory)
            {
                totals.push_back(
                    sample.totalMs);
                raw += sample.rawMs;
                h +=
                    sample.spatialHorizontalMs;
                v +=
                    sample.spatialVerticalMs;
                temporal +=
                    sample.temporalMs;
                pack +=
                    sample.packMs;
            }

            std::sort(
                totals.begin(),
                totals.end());

            const double count =
                static_cast<double>(
                    g_timingHistory.size());

            const std::size_t p95 =
                static_cast<std::size_t>(
                    std::floor(
                        0.95 *
                        (totals.size() - 1)));

            std::ostringstream out;
            out
                << std::fixed
                << std::setprecision(4)
                << "samples="
                << g_timingHistory.size()
                << "\n"
                << "mean_raw_ms="
                << raw / count
                << "\n"
                << "mean_spatial_h_ms="
                << h / count
                << "\n"
                << "mean_spatial_v_ms="
                << v / count
                << "\n"
                << "mean_temporal_ms="
                << temporal / count
                << "\n"
                << "mean_pack_ms="
                << pack / count
                << "\n"
                << "p95_total_ms="
                << totals[p95]
                << "\n"
                << "max_total_ms="
                << totals.back()
                << "\n";

            WriteText(
                directory /
                    L"gpu_timing_summary.txt",
                out.str());
        }

        void Capture(
            const execution::ExecutionLease& lease,
            const std::uint64_t frame,
            const std::uint32_t mode)
        {
            std::error_code error;
            fs::create_directories(
                g_capture.directory,
                error);

            if (error)
            {
                return;
            }

            std::vector<float> raw;
            std::vector<float> spatial;
            std::vector<float> finalVisibility;
            std::vector<float> stock;
            std::vector<float> finalAo;

            const bool ok =
                ReadR16(
                    lease.device,
                    lease.immediate,
                    g_runtime.raw.texture,
                    raw) &&
                ReadR16(
                    lease.device,
                    lease.immediate,
                    g_runtime.spatial.texture,
                    spatial) &&
                ReadR16(
                    lease.device,
                    lease.immediate,
                    g_runtime.history[
                        g_signalHistory.readIndex].texture,
                    finalVisibility) &&
                ReadRgba8Red(
                    lease.device,
                    lease.immediate,
                    g_runtime.stockCopy,
                    stock) &&
                ReadRgba8Red(
                    lease.device,
                    lease.immediate,
                    g_runtime.finalCopy,
                    finalAo);

            if (ok)
            {
                WriteGrayBmp(
                    g_capture.directory /
                        L"stock_sao_filtered.bmp",
                    stock);

                WriteGrayBmp(
                    g_capture.directory /
                        L"gzfx_visibility_raw.bmp",
                    raw);

                WriteGrayBmp(
                    g_capture.directory /
                        L"gzfx_visibility_spatial.bmp",
                    spatial);

                WriteGrayBmp(
                    g_capture.directory /
                        L"gzfx_visibility_temporal.bmp",
                    finalVisibility);

                WriteGrayBmp(
                    g_capture.directory /
                        L"final_sao_written.bmp",
                    finalAo);

                std::vector<float> difference(
                    finalAo.size());

                for (std::size_t i = 0;
                     i < difference.size();
                     ++i)
                {
                    difference[i] =
                        std::abs(
                            finalAo[i] -
                            stock[i]);
                }

                WriteGrayBmp(
                    g_capture.directory /
                        L"stock_vs_gzfx_difference.bmp",
                    difference);

                std::ostringstream stats;
                stats
                    << SignalStats(
                        "stock_sao",
                        stock)
                    << SignalStats(
                        "raw_visibility",
                        raw)
                    << SignalStats(
                        "spatial_visibility",
                        spatial)
                    << SignalStats(
                        "final_visibility",
                        finalVisibility);

                WriteText(
                    g_capture.directory /
                        L"stats.txt",
                    stats.str());
            }

            WriteTimingSummary(
                g_capture.directory);

            std::ostringstream summary;
            summary
                << "GZFX Ambient Visibility v2 capture\n"
                << "version="
                << gzfx::kVersion
                << "\n"
                << "frame="
                << frame
                << "\n"
                << "mode="
                << mode
                << "\n"
                << "resolution="
                << g_runtime.width
                << "x"
                << g_runtime.height
                << "\n"
                << "algorithm=gtao_inspired_full_resolution\n"
                << "integration=replace_sao_filtered_before_ambient_composite\n"
                << "radius_world=240\n"
                << "slices=3\n"
                << "steps_per_slice=6\n"
                << "capture="
                << (ok
                    ? "complete"
                    : "partial")
                << "\n";

            WriteText(
                g_capture.directory /
                    L"summary.txt",
                summary.str());
        }

        void WriteStatus(
            const std::uint64_t frame,
            const char* status,
            const std::string& detail = {})
        {
            const fs::path root =
                DiagnosticsRoot();

            if (root.empty())
            {
                return;
            }

            std::ostringstream out;
            out
                << "GZFX Ambient Visibility v2\n"
                << "version="
                << gzfx::kVersion
                << "\n"
                << "frame="
                << frame
                << "\n"
                << "status="
                << status
                << "\n"
                << "mode="
                << state::GetDevelopmentMode()
                << "\n"
                << "computes="
                << g_computeCount
                << "\n"
                << "applications="
                << g_applyCount
                << "\n"
                << "skips="
                << g_skipCount
                << "\n"
                << "signal_history_valid="
                << (g_signalHistory.valid
                    ? "true"
                    : "false")
                << "\n"
                << "wave3_history_generation="
                << g_signalHistory.wave3HistoryGeneration
                << "\n"
                << "final_visibility_frame="
                << g_finalVisibilityFrame
                << "\n"
                << "last_applied_frame="
                << g_lastAppliedFrame
                << "\n";

            if (!detail.empty())
            {
                out
                    << "detail="
                    << detail
                    << "\n";
            }

            WriteText(
                root /
                    L"ambient_visibility_v2_status.txt",
                out.str());
        }

        void BeginCaptureIfRequested(
            const std::uint64_t frame)
        {
            if (!g_captureRequested.exchange(
                    false,
                    std::memory_order_acq_rel) ||
                g_capture.active)
            {
                return;
            }

            g_capture = {};
            g_capture.active = true;
            g_capture.index =
                ++g_captureIndex;
            g_capture.targetFrame =
                frame +
                kCaptureDelayFrames;

            std::wostringstream name;
            name
                << L"manual"
                << std::setw(4)
                << std::setfill(L'0')
                << g_capture.index;

            g_capture.directory =
                DiagnosticsRoot() /
                name.str();
        }

        bool GetCurrentStockAO(
            const execution::ExecutionLease& lease,
            const std::uint64_t frame,
            ID3D11Texture2D** outTexture,
            std::uintptr_t* outIdentity,
            D3D11_TEXTURE2D_DESC* outDesc,
            std::string& error)
        {
            if (outTexture == nullptr ||
                outIdentity == nullptr ||
                outDesc == nullptr)
            {
                return false;
            }

            *outTexture = nullptr;
            *outIdentity = 0;
            *outDesc = {};

            semantic_authority::SemanticView view{};

            if (!semantic_authority::QuerySemantic(
                    semantic_authority::Semantic::AOFiltered,
                    semantic_authority::QueryMode::CurrentFrame,
                    frame,
                    stage_scheduler::Boundary::AfterStockAO,
                    &view))
            {
                error =
                    "SAO_FILTERED unavailable";
                return false;
            }

            ID3D11RenderTargetView* rtv =
                nullptr;

            lease.immediate->OMGetRenderTargets(
                1,
                &rtv,
                nullptr);

            if (rtv == nullptr)
            {
                error =
                    "stock AO RT0 unavailable";
                return false;
            }

            ID3D11Resource* resource =
                nullptr;

            rtv->GetResource(
                &resource);

            rtv->Release();

            if (resource == nullptr)
            {
                error =
                    "stock AO RT0 resource unavailable";
                return false;
            }

            ID3D11Texture2D* texture =
                nullptr;

            resource->QueryInterface(
                __uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(
                    &texture));

            const std::uintptr_t identity =
                CanonicalId(resource);

            resource->Release();

            if (texture == nullptr ||
                identity == 0 ||
                identity !=
                    view.resource.identity)
            {
                if (texture != nullptr)
                {
                    texture->Release();
                }

                error =
                    "stock AO identity mismatch";
                return false;
            }

            D3D11_TEXTURE2D_DESC desc{};
            texture->GetDesc(&desc);

            if (desc.Width !=
                    g_runtime.width ||
                desc.Height !=
                    g_runtime.height ||
                desc.Format !=
                    DXGI_FORMAT_R8G8B8A8_UNORM ||
                desc.SampleDesc.Count != 1 ||
                (
                    desc.BindFlags &
                    D3D11_BIND_UNORDERED_ACCESS
                ) == 0)
            {
                texture->Release();
                error =
                    "stock AO shape/format/UAV contract mismatch";
                return false;
            }

            *outTexture = texture;
            *outIdentity = identity;
            *outDesc = desc;
            return true;
        }

        void OnAfterOpaque(
            const stage_scheduler::Event& event,
            void*) noexcept
        {
            try
            {
                if (event.boundary !=
                    stage_scheduler::Boundary::
                        AfterOpaqueWorldMRT ||
                    DisabledForLegacyHarness())
                {
                    return;
                }

                std::scoped_lock lock(
                    g_mutex);

                BeginCaptureIfRequested(
                    event.frame);

                const std::uint32_t mode =
                    state::GetDevelopmentMode();

                if (mode != 1 &&
                    mode != 2)
                {
                    return;
                }

                execution::ExecutionLease lease{};

                if (!execution::Acquire(
                        &lease))
                {
                    ++g_skipCount;
                    WriteStatus(
                        event.frame,
                        "skip_execution_service");
                    return;
                }

                struct LeaseGuard
                {
                    execution::ExecutionLease* value;
                    ~LeaseGuard()
                    {
                        execution::Release(
                            *value);
                    }
                } leaseGuard{&lease};

                ResolveTimers(
                    lease.immediate);

                wave3::FrameView frameView{};

                if (!wave3::AcquireFrameView(
                        event.frame,
                        &frameView))
                {
                    ++g_skipCount;
                    g_signalHistory.valid =
                        false;
                    WriteStatus(
                        event.frame,
                        "skip_wave3_frame");
                    return;
                }

                struct FrameGuard
                {
                    wave3::FrameView* value;
                    ~FrameGuard()
                    {
                        wave3::ReleaseFrameView(
                            *value);
                    }
                } frameGuard{&frameView};

                native_input::GeometryInputs inputs{};
                native_input::Failure failure =
                    native_input::Failure::None;

                if (!native_input::AcquireGeometryInputs(
                        event.frame,
                        event.boundary,
                        lease,
                        &inputs,
                        &failure))
                {
                    ++g_skipCount;
                    g_signalHistory.valid =
                        false;
                    WriteStatus(
                        event.frame,
                        "skip_native_inputs",
                        native_input::FailureName(
                            failure));
                    return;
                }

                struct InputsGuard
                {
                    native_input::GeometryInputs* value;
                    ~InputsGuard()
                    {
                        native_input::ReleaseGeometryInputs(
                            *value);
                    }
                } inputsGuard{&inputs};

                bool rebuilt = false;
                std::string error;

                if (!EnsureRuntime(
                        lease,
                        inputs.width,
                        inputs.height,
                        error,
                        &rebuilt))
                {
                    ++g_skipCount;
                    g_signalHistory.valid =
                        false;
                    WriteStatus(
                        event.frame,
                        "skip_runtime",
                        error);
                    return;
                }

                const bool temporal =
                    !rebuilt &&
                    g_signalHistory.valid &&
                    g_signalHistory.lastFrame + 1 ==
                        event.frame &&
                    g_signalHistory.wave3HistoryGeneration ==
                        frameView.historyGeneration &&
                    frameView.historyValid;

                const std::size_t previousIndex =
                    g_signalHistory.readIndex;

                const std::size_t writeIndex =
                    temporal
                        ? 1u - previousIndex
                        : 0u;

                ID3D11CommandList* commands =
                    nullptr;

                if (!RecordVisibilityCommands(
                        inputs,
                        frameView,
                        previousIndex,
                        writeIndex,
                        temporal,
                        &commands,
                        error))
                {
                    if (commands != nullptr)
                    {
                        commands->Release();
                    }

                    ++g_skipCount;
                    g_signalHistory.valid =
                        false;
                    WriteStatus(
                        event.frame,
                        "skip_record",
                        error);
                    return;
                }

                const bool executed =
                    execution::ExecuteOwned(
                        lease,
                        commands,
                        execution::OwnedPass::
                            Wave4AmbientVisibility,
                        event.frame,
                        true);

                commands->Release();

                if (!executed)
                {
                    ++g_skipCount;
                    g_signalHistory.valid =
                        false;
                    WriteStatus(
                        event.frame,
                        "skip_execute");
                    return;
                }

                ++g_computeCount;

                g_signalHistory.valid =
                    true;
                g_signalHistory.lastFrame =
                    event.frame;
                g_signalHistory.wave3HistoryGeneration =
                    frameView.historyGeneration;
                g_signalHistory.readIndex =
                    writeIndex;

                g_finalVisibilityFrame =
                    event.frame;

                if (g_currentCamera != nullptr)
                {
                    g_currentCamera->Release();
                    g_currentCamera = nullptr;
                }

                g_currentCamera =
                    inputs.cameraBuffer;
                g_currentCamera->AddRef();
                g_currentCameraFrame =
                    event.frame;

                if (g_capture.active &&
                    event.frame >=
                        g_capture.targetFrame)
                {
                    g_capture.selectedFrame =
                        event.frame;
                }

                if (event.frame <= 3 ||
                    event.frame -
                        g_lastStatusFrame >=
                    120 ||
                    g_capture.selectedFrame ==
                        event.frame)
                {
                    WriteStatus(
                        event.frame,
                        temporal
                            ? "computed_temporal"
                            : "computed_fresh");
                    g_lastStatusFrame =
                        event.frame;
                }
            }
            catch (...)
            {
            }
        }

        void OnAfterStockAO(
            const stage_scheduler::Event& event,
            void*) noexcept
        {
            try
            {
                if (event.boundary !=
                    stage_scheduler::Boundary::
                        AfterStockAO ||
                    DisabledForLegacyHarness())
                {
                    return;
                }

                std::scoped_lock lock(
                    g_mutex);

                const std::uint32_t mode =
                    state::GetDevelopmentMode();

                if ((mode != 1 &&
                     mode != 2) ||
                    g_finalVisibilityFrame !=
                        event.frame ||
                    !g_signalHistory.valid)
                {
                    return;
                }

                execution::ExecutionLease lease{};

                if (!execution::Acquire(
                        &lease))
                {
                    ++g_skipCount;
                    return;
                }

                struct LeaseGuard
                {
                    execution::ExecutionLease* value;
                    ~LeaseGuard()
                    {
                        execution::Release(
                            *value);
                    }
                } leaseGuard{&lease};

                ResolveTimers(
                    lease.immediate);

                ID3D11Texture2D* stock =
                    nullptr;
                std::uintptr_t identity = 0;
                D3D11_TEXTURE2D_DESC desc{};
                std::string error;

                if (!GetCurrentStockAO(
                        lease,
                        event.frame,
                        &stock,
                        &identity,
                        &desc,
                        error))
                {
                    ++g_skipCount;
                    WriteStatus(
                        event.frame,
                        "stock_fallback",
                        error);
                    return;
                }

                struct TextureGuard
                {
                    ID3D11Texture2D* value;
                    ~TextureGuard()
                    {
                        if (value != nullptr)
                        {
                            value->Release();
                        }
                    }
                } textureGuard{stock};

                if (!EnsureStockUav(
                        lease.device,
                        stock,
                        identity,
                        error))
                {
                    ++g_skipCount;
                    WriteStatus(
                        event.frame,
                        "stock_fallback",
                        error);
                    return;
                }

                const bool capture =
                    g_capture.active &&
                    g_capture.selectedFrame ==
                        event.frame;

                const bool needStockCopy =
                    mode == 1 ||
                    capture;

                ID3D11CommandList* commands =
                    nullptr;

                if (g_currentCamera == nullptr ||
                    g_currentCameraFrame !=
                        event.frame)
                {
                    ++g_skipCount;
                    WriteStatus(
                        event.frame,
                        "stock_fallback",
                        "current frame camera unavailable");
                    return;
                }

                if (!RecordPackCommands(
                        event.frame,
                        mode,
                        g_currentCamera,
                        stock,
                        needStockCopy,
                        capture,
                        &commands,
                        error))
                {
                    if (commands != nullptr)
                    {
                        commands->Release();
                    }

                    ++g_skipCount;
                    WriteStatus(
                        event.frame,
                        "stock_fallback",
                        error);
                    return;
                }

                const bool executed =
                    execution::ExecuteOwned(
                        lease,
                        commands,
                        execution::OwnedPass::
                            Wave4AmbientInjection,
                        event.frame,
                        true);

                commands->Release();

                if (!executed)
                {
                    ++g_skipCount;
                    WriteStatus(
                        event.frame,
                        "stock_fallback",
                        "owned AO injection execution failed");
                    return;
                }

                ++g_applyCount;
                g_lastAppliedFrame =
                    event.frame;

                if (capture)
                {
                    Capture(
                        lease,
                        event.frame,
                        mode);

                    g_capture.active =
                        false;
                    g_capture.selectedFrame =
                        0;
                }

                if (capture ||
                    event.frame <= 3 ||
                    event.frame -
                        g_lastStatusFrame >=
                    120)
                {
                    WriteStatus(
                        event.frame,
                        mode == 1
                            ? "applied_split"
                            : "applied_full");

                    g_lastStatusFrame =
                        event.frame;
                }
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
                            stage_scheduler::Boundary::
                                AfterOpaqueWorldMRT,
                            &OnAfterOpaque,
                            nullptr);

                    const bool stockAo =
                        stage_scheduler::RegisterCallback(
                            stage_scheduler::Boundary::
                                AfterStockAO,
                            &OnAfterStockAO,
                            nullptr);

                    g_initialized =
                        opaque &&
                        stockAo;
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
        return g_initialized;
    }

    bool WasAppliedFrame(
        const std::uint64_t frame) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            return
                g_lastAppliedFrame ==
                frame;
        }
        catch (...)
        {
            return false;
        }
    }

    bool OwnsDevelopmentMode(
        const std::uint32_t mode) noexcept
    {
        return
            g_initialized &&
            !DisabledForLegacyHarness() &&
            (mode == 1 ||
             mode == 2);
    }

    void RequestManualCapture() noexcept
    {
        try
        {
            g_captureRequested.store(
                true,
                std::memory_order_release);
        }
        catch (...)
        {
        }
    }
}
