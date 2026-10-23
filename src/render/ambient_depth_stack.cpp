#include "gzfx/render/ambient_depth_stack.hpp"

#include "gzfx/paths.hpp"
#include "gzfx/render/runtime_state.hpp"
#include "gzfx/settings.hpp"
#include "gzfx/version.hpp"

#include <Windows.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

namespace gzfx::render::ambient_depth
{
    namespace fs = std::filesystem;

    namespace
    {
        struct Parameters
        {
            float fullInvWidth = 0.0f;
            float fullInvHeight = 0.0f;
            float halfInvWidth = 0.0f;
            float halfInvHeight = 0.0f;

            float aoStrength = 0.90f;
            float aoRadius = 28.0f;
            float indirectStrength = 0.20f;
            float indirectRadius = 36.0f;

            float indirectSaturation = 0.85f;
            std::uint32_t quality = 2;
            std::uint32_t mode = 1;
            std::uint32_t aoEnabled = 1;

            std::uint32_t indirectEnabled = 1;
            float axisX = 0.0f;
            float axisY = 0.0f;
            float padding0 = 0.0f;
        };

        struct SurfacePair
        {
            ID3D11Texture2D* ao = nullptr;
            ID3D11RenderTargetView* aoRtv = nullptr;
            ID3D11ShaderResourceView* aoSrv = nullptr;

            ID3D11Texture2D* indirect = nullptr;
            ID3D11RenderTargetView* indirectRtv = nullptr;
            ID3D11ShaderResourceView* indirectSrv = nullptr;
        };

        struct Runtime
        {
            ID3D11Device* device = nullptr;
            ID3D11DeviceContext* deferred = nullptr;

            ID3D11VertexShader* vertexShader = nullptr;
            ID3D11PixelShader* downsampleShader = nullptr;
            ID3D11PixelShader* analyzeShader = nullptr;
            ID3D11PixelShader* denoiseShader = nullptr;
            ID3D11PixelShader* compositeShader = nullptr;

            ID3D11SamplerState* linearSampler = nullptr;
            ID3D11SamplerState* pointSampler = nullptr;
            ID3D11Buffer* parameters = nullptr;

            ID3D11Texture2D* sceneCopy = nullptr;
            ID3D11ShaderResourceView* sceneCopySrv = nullptr;

            ID3D11Texture2D* workingDepth = nullptr;
            ID3D11RenderTargetView* workingDepthRtv = nullptr;
            ID3D11ShaderResourceView* workingDepthSrv = nullptr;

            ID3D11Texture2D* workingColor = nullptr;
            ID3D11RenderTargetView* workingColorRtv = nullptr;
            ID3D11ShaderResourceView* workingColorSrv = nullptr;

            SurfacePair first{};
            SurfacePair second{};

            ID3D11Texture2D* composite = nullptr;
            ID3D11RenderTargetView* compositeRtv = nullptr;

            UINT fullWidth = 0;
            UINT fullHeight = 0;
            UINT halfWidth = 0;
            UINT halfHeight = 0;
            DXGI_FORMAT sceneFormat = DXGI_FORMAT_UNKNOWN;
        };

        std::mutex g_mutex;
        Runtime g_runtime{};

        std::uint64_t g_applyCount = 0;
        std::uint64_t g_lastFrame = 0;

        void WriteLog(
            const std::uint64_t frame,
            const char* status,
            const std::string& detail = {})
        {
            const fs::path root =
                gzfx::paths::DiagnosticsRoot();

            if (root.empty())
            {
                return;
            }

            std::error_code error;
            fs::create_directories(
                root,
                error);

            if (error)
            {
                return;
            }

            const auto& settings =
                gzfx::settings::Get();

            std::ofstream out(
                root / L"ambient_depth_stack.txt",
                std::ios::out |
                    std::ios::trunc);

            if (!out)
            {
                return;
            }

            out
                << "GZFX ambient depth stack\n"
                << "version=" << gzfx::kVersion << "\n"
                << "status=" << status << "\n"
                << "frame=" << frame << "\n"
                << "applications=" << g_applyCount << "\n"
                << "half_resolution="
                << g_runtime.halfWidth
                << "x"
                << g_runtime.halfHeight
                << "\n"
                << "horizon_enabled="
                << (settings.horizonOcclusionEnabled
                    ? "true"
                    : "false")
                << "\n"
                << "horizon_strength="
                << settings.horizonOcclusionStrength
                << "\n"
                << "horizon_radius="
                << settings.horizonOcclusionRadiusPixels
                << "\n"
                << "quality="
                << settings.horizonOcclusionQuality
                << "\n"
                << "indirect_enabled="
                << (settings.indirectBounceEnabled
                    ? "true"
                    : "false")
                << "\n"
                << "indirect_strength="
                << settings.indirectBounceStrength
                << "\n"
                << "indirect_radius="
                << settings.indirectBounceRadiusPixels
                << "\n"
                << "indirect_saturation="
                << settings.indirectBounceSaturation
                << "\n";

            if (!detail.empty())
            {
                out
                    << "detail="
                    << detail
                    << "\n";
            }
        }

        void ReleaseSurface(
            SurfacePair& surface)
        {
            if (surface.indirectSrv) surface.indirectSrv->Release();
            if (surface.indirectRtv) surface.indirectRtv->Release();
            if (surface.indirect) surface.indirect->Release();

            if (surface.aoSrv) surface.aoSrv->Release();
            if (surface.aoRtv) surface.aoRtv->Release();
            if (surface.ao) surface.ao->Release();

            surface = {};
        }

        void ReleaseRuntime()
        {
            if (g_runtime.compositeRtv) g_runtime.compositeRtv->Release();
            if (g_runtime.composite) g_runtime.composite->Release();

            ReleaseSurface(g_runtime.second);
            ReleaseSurface(g_runtime.first);

            if (g_runtime.workingColorSrv) g_runtime.workingColorSrv->Release();
            if (g_runtime.workingColorRtv) g_runtime.workingColorRtv->Release();
            if (g_runtime.workingColor) g_runtime.workingColor->Release();

            if (g_runtime.workingDepthSrv) g_runtime.workingDepthSrv->Release();
            if (g_runtime.workingDepthRtv) g_runtime.workingDepthRtv->Release();
            if (g_runtime.workingDepth) g_runtime.workingDepth->Release();

            if (g_runtime.sceneCopySrv) g_runtime.sceneCopySrv->Release();
            if (g_runtime.sceneCopy) g_runtime.sceneCopy->Release();

            if (g_runtime.parameters) g_runtime.parameters->Release();
            if (g_runtime.pointSampler) g_runtime.pointSampler->Release();
            if (g_runtime.linearSampler) g_runtime.linearSampler->Release();

            if (g_runtime.compositeShader) g_runtime.compositeShader->Release();
            if (g_runtime.denoiseShader) g_runtime.denoiseShader->Release();
            if (g_runtime.analyzeShader) g_runtime.analyzeShader->Release();
            if (g_runtime.downsampleShader) g_runtime.downsampleShader->Release();
            if (g_runtime.vertexShader) g_runtime.vertexShader->Release();

            if (g_runtime.deferred) g_runtime.deferred->Release();
            if (g_runtime.device) g_runtime.device->Release();

            g_runtime = {};
        }

        bool Compile(
            const char* source,
            const char* entry,
            const char* profile,
            ID3DBlob** blob,
            std::string& error)
        {
            ID3DBlob* errors = nullptr;

            const HRESULT result =
                D3DCompile(
                    source,
                    std::strlen(source),
                    "GZFXAmbientDepth",
                    nullptr,
                    nullptr,
                    entry,
                    profile,
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
                    error =
                        "D3DCompile failed.";
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
            return R"(
struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput VSMain(uint id : SV_VertexID)
{
    float2 uv =
        float2(
            (id << 1) & 2,
            id & 2);

    VSOutput output;
    output.uv = uv;
    output.position =
        float4(
            uv * float2(2.0, -2.0) +
            float2(-1.0, 1.0),
            0.0,
            1.0);

    return output;
}

cbuffer GZFXAmbientDepthParameters : register(b0)
{
    float2 FullInvSize;
    float2 HalfInvSize;

    float AoStrength;
    float AoRadius;
    float IndirectStrength;
    float IndirectRadius;

    float IndirectSaturation;
    uint Quality;
    uint Mode;
    uint AoEnabled;

    uint IndirectEnabled;
    float2 FilterAxis;
    float ParameterPadding0;
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

Texture2D<float4> SceneColor : register(t0);
Texture2D<float> SceneDepth : register(t1);

Texture2D<float> AoInput : register(t2);
Texture2D<float4> IndirectInput : register(t3);

Texture2D<float> WorkingDepth : register(t4);
Texture2D<float4> WorkingColor : register(t5);

SamplerState LinearSampler : register(s0);
SamplerState PointSampler : register(s1);

float2 DepthUv(float2 uv)
{
    return saturate(
        uv *
        DynamicResolutionParams1.xy);
}

float3 ReconstructView(
    float2 uv,
    float depth)
{
    float4 clip =
        float4(
            2.0 *
                float2(
                    uv.x,
                    1.0 - uv.y) -
                1.0,
            depth,
            1.0);

    float4 view =
        mul(
            CameraProjInverse,
            clip);

    return
        view.xyz /
        max(
            abs(view.w),
            1e-6);
}

float3 ReconstructNormal(
    float2 uv,
    float centerDepth)
{
    float2 dx =
        float2(
            HalfInvSize.x,
            0.0);

    float2 dy =
        float2(
            0.0,
            HalfInvSize.y);

    float depthX =
        WorkingDepth.SampleLevel(
            PointSampler,
            saturate(uv + dx),
            0).x;

    float depthY =
        WorkingDepth.SampleLevel(
            PointSampler,
            saturate(uv + dy),
            0).x;

    float3 center =
        ReconstructView(
            uv,
            centerDepth);

    float3 px =
        ReconstructView(
            saturate(uv + dx),
            depthX);

    float3 py =
        ReconstructView(
            saturate(uv + dy),
            depthY);

    float3 normal =
        normalize(
            cross(
                py - center,
                px - center));

    if (dot(
            normal,
            -center) < 0.0)
    {
        normal = -normal;
    }

    return normal;
}

float Luminance(float3 color)
{
    return
        dot(
            color,
            float3(
                0.2126,
                0.7152,
                0.0722));
}

float3 ApplySaturation(
    float3 color,
    float saturation)
{
    float luma =
        Luminance(color);

    return
        lerp(
            luma.xxx,
            color,
            saturation);
}

uint DirectionCount()
{
    if (Quality == 0) return 3;
    if (Quality == 1) return 4;
    if (Quality == 2) return 6;
    return 8;
}

uint StepCount()
{
    if (Quality == 0) return 3;
    if (Quality == 1) return 3;
    if (Quality == 2) return 4;
    return 5;
}

float2 SpatialNoise(float2 pixel)
{
    float first =
        frac(
            52.9829189 *
            frac(
                dot(
                    pixel,
                    float2(
                        0.06711056,
                        0.00583715))));

    float second =
        frac(
            0.5 +
            first *
            0.7548776662 +
            dot(
                pixel,
                float2(
                    0.569840296,
                    0.438289)));

    return float2(
        first,
        second);
}

static const float2 Directions[12] =
{
    float2( 1.0000,  0.0000),
    float2( 0.8660,  0.5000),
    float2( 0.5000,  0.8660),
    float2( 0.0000,  1.0000),
    float2(-0.5000,  0.8660),
    float2(-0.8660,  0.5000),
    float2(-1.0000,  0.0000),
    float2(-0.8660, -0.5000),
    float2(-0.5000, -0.8660),
    float2( 0.0000, -1.0000),
    float2( 0.5000, -0.8660),
    float2( 0.8660, -0.5000)
};

struct DownsampleOutput
{
    float depth : SV_TARGET0;
    float4 color : SV_TARGET1;
};

DownsampleOutput DownsamplePS(
    VSOutput input)
{
    DownsampleOutput output;

    float2 offset =
        FullInvSize * 0.5;

    float2 uv0 =
        saturate(
            input.uv +
            float2(
                -offset.x,
                -offset.y));

    float2 uv1 =
        saturate(
            input.uv +
            float2(
                 offset.x,
                -offset.y));

    float2 uv2 =
        saturate(
            input.uv +
            float2(
                -offset.x,
                 offset.y));

    float2 uv3 =
        saturate(
            input.uv +
            float2(
                 offset.x,
                 offset.y));

    float depths[4] =
    {
        SceneDepth.SampleLevel(
            PointSampler,
            DepthUv(uv0),
            0).x,
        SceneDepth.SampleLevel(
            PointSampler,
            DepthUv(uv1),
            0).x,
        SceneDepth.SampleLevel(
            PointSampler,
            DepthUv(uv2),
            0).x,
        SceneDepth.SampleLevel(
            PointSampler,
            DepthUv(uv3),
            0).x
    };

    float2 uvs[4] =
    {
        uv0,
        uv1,
        uv2,
        uv3
    };

    uint closest = 0;

    [unroll]
    for (uint i = 1;
         i < 4;
         ++i)
    {
        if (depths[i] < depths[closest])
        {
            closest = i;
        }
    }

    output.depth =
        depths[closest];

    output.color =
        SceneColor.SampleLevel(
            LinearSampler,
            uvs[closest],
            0);

    return output;
}

struct AnalyzeOutput
{
    float ao : SV_TARGET0;
    float4 indirect : SV_TARGET1;
};

AnalyzeOutput AnalyzePS(
    VSOutput input)
{
    AnalyzeOutput output;

    float centerDepth =
        WorkingDepth.SampleLevel(
            PointSampler,
            input.uv,
            0).x;

    if (centerDepth >= 0.99999)
    {
        output.ao = 0.0;
        output.indirect =
            float4(
                0.0,
                0.0,
                0.0,
                1e6);

        return output;
    }

    float3 center =
        ReconstructView(
            input.uv,
            centerDepth);

    float3 normal =
        ReconstructNormal(
            input.uv,
            centerDepth);

    float2 pixel =
        floor(
            input.uv /
            HalfInvSize);

    float2 noise =
        SpatialNoise(pixel);

    float rotationAngle =
        noise.x *
        6.28318530718;

    float rotationCos =
        cos(rotationAngle);

    float rotationSin =
        sin(rotationAngle);

    float2x2 rotation =
        float2x2(
            rotationCos,
            -rotationSin,
            rotationSin,
            rotationCos);

    uint directionCount =
        DirectionCount();

    uint stepCount =
        StepCount();

    float horizonSum = 0.0;
    float3 indirectSum = 0.0;
    float indirectWeight = 0.0;

    [loop]
    for (uint directionIndex = 0;
         directionIndex < 12;
         ++directionIndex)
    {
        if (directionIndex >= directionCount)
        {
            break;
        }

        float2 direction =
            mul(
                rotation,
                Directions[directionIndex]);

        float maxHorizon = 0.0;

        [loop]
        for (uint stepIndex = 1;
             stepIndex <= 6;
             ++stepIndex)
        {
            if (stepIndex > stepCount)
            {
                break;
            }

            float stepFraction =
                (
                    (float)stepIndex -
                    0.65 +
                    noise.y * 0.30
                ) /
                (float)stepCount;

            stepFraction =
                saturate(
                    stepFraction);

            // Bias samples toward the receiver to retain fine detail
            // while still reaching the configured far radius.
            stepFraction *=
                stepFraction;

            float radiusPixels =
                max(
                    AoRadius,
                    IndirectRadius);

            float2 sampleUv =
                saturate(
                    input.uv +
                    direction *
                    FullInvSize *
                    radiusPixels *
                    stepFraction);

            float sampleDepth =
                WorkingDepth.SampleLevel(
                    PointSampler,
                    sampleUv,
                    0).x;

            if (sampleDepth >= 0.99999)
            {
                continue;
            }

            float3 samplePosition =
                ReconstructView(
                    sampleUv,
                    sampleDepth);

            float3 delta =
                samplePosition -
                center;

            float distanceToSample =
                length(delta);

            if (distanceToSample <= 1e-4)
            {
                continue;
            }

            float3 direction3 =
                delta /
                distanceToSample;

            float distanceFalloff =
                saturate(
                    1.0 -
                    stepFraction);

            if (AoEnabled != 0)
            {
                float horizon =
                    saturate(
                        dot(
                            normal,
                            direction3));

                float facingDistance =
                    saturate(
                        1.0 -
                        distanceToSample /
                        max(
                            abs(center.z) * 0.18,
                            8.0));

                maxHorizon =
                    max(
                        maxHorizon,
                        horizon *
                        facingDistance);
            }

            if (IndirectEnabled != 0)
            {
                float indirectRadiusWeight =
                    saturate(
                        1.0 -
                        (
                            radiusPixels *
                            stepFraction
                        ) /
                        max(
                            IndirectRadius,
                            1.0));

                float receiverFacing =
                    saturate(
                        dot(
                            normal,
                            direction3));

                float weight =
                    receiverFacing *
                    indirectRadiusWeight *
                    (0.35 + 0.65 * distanceFalloff);

                if (weight > 0.0)
                {
                    float3 radiance =
                        max(
                            WorkingColor.SampleLevel(
                                LinearSampler,
                                sampleUv,
                                0).rgb,
                            0.0);

                    // Prevent a tiny hot pixel from turning the
                    // whole neighborhood into a nuclear reactor.
                    radiance =
                        min(
                            radiance,
                            8.0);

                    indirectSum +=
                        radiance *
                        weight;

                    indirectWeight +=
                        weight;
                }
            }
        }

        horizonSum +=
            maxHorizon;
    }

    output.ao =
        AoEnabled != 0
        ? saturate(
            horizonSum /
            max(
                (float)directionCount,
                1.0))
        : 0.0;

    float3 bounce =
        indirectWeight > 0.0
        ? indirectSum /
            indirectWeight
        : 0.0;

    bounce =
        ApplySaturation(
            bounce,
            IndirectSaturation);

    output.indirect =
        float4(
            bounce,
            abs(center.z));

    return output;
}

struct FilterOutput
{
    float ao : SV_TARGET0;
    float4 indirect : SV_TARGET1;
};

FilterOutput DenoisePS(
    VSOutput input)
{
    FilterOutput output;

    float4 centerIndirect =
        IndirectInput.SampleLevel(
            PointSampler,
            input.uv,
            0);

    float centerDepth =
        centerIndirect.a;

    float aoSum = 0.0;
    float3 indirectSum = 0.0;
    float weightSum = 0.0;

    static const float Weights[5] =
    {
        0.06136,
        0.24477,
        0.38774,
        0.24477,
        0.06136
    };

    [unroll]
    for (int offset = -2;
         offset <= 2;
         ++offset)
    {
        float2 sampleUv =
            saturate(
                input.uv +
                FilterAxis *
                HalfInvSize *
                (float)offset);

        float4 indirect =
            IndirectInput.SampleLevel(
                PointSampler,
                sampleUv,
                0);

        float ao =
            AoInput.SampleLevel(
                PointSampler,
                sampleUv,
                0);

        float relativeDepth =
            abs(
                indirect.a -
                centerDepth) /
            max(
                centerDepth,
                1.0);

        float bilateral =
            exp2(
                -relativeDepth *
                96.0);

        float weight =
            Weights[offset + 2] *
            bilateral;

        aoSum +=
            ao *
            weight;

        indirectSum +=
            indirect.rgb *
            weight;

        weightSum +=
            weight;
    }

    weightSum =
        max(
            weightSum,
            1e-5);

    output.ao =
        aoSum /
        weightSum;

    output.indirect =
        float4(
            indirectSum /
                weightSum,
            centerDepth);

    return output;
}

float4 CompositePS(
    VSOutput input) : SV_TARGET
{
    float4 original =
        SceneColor.SampleLevel(
            LinearSampler,
            input.uv,
            0);

    if (Mode != 1 &&
        Mode != 2)
    {
        return original;
    }

    if (Mode == 1 &&
        input.uv.x < 0.5)
    {
        return original;
    }

    float fullDepth =
        SceneDepth.SampleLevel(
            PointSampler,
            DepthUv(input.uv),
            0).x;

    float fullViewDepth =
        fullDepth >= 0.99999
        ? 1e6
        : abs(
            ReconstructView(
                input.uv,
                fullDepth).z);

    static const float2 offsets[4] =
    {
        float2(-0.5, -0.5),
        float2( 0.5, -0.5),
        float2(-0.5,  0.5),
        float2( 0.5,  0.5)
    };

    float aoSum = 0.0;
    float3 indirectSum = 0.0;
    float weightSum = 0.0;

    [unroll]
    for (uint i = 0;
         i < 4;
         ++i)
    {
        float2 sampleUv =
            saturate(
                input.uv +
                offsets[i] *
                HalfInvSize);

        float4 indirectSample =
            IndirectInput.SampleLevel(
                PointSampler,
                sampleUv,
                0);

        float aoSample =
            AoInput.SampleLevel(
                PointSampler,
                sampleUv,
                0);

        float relativeDepth =
            abs(
                indirectSample.a -
                fullViewDepth) /
            max(
                fullViewDepth,
                1.0);

        float depthWeight =
            exp2(
                -relativeDepth *
                128.0);

        float spatialWeight =
            1.0 /
            (
                1.0 +
                dot(
                    offsets[i],
                    offsets[i])
            );

        float weight =
            max(
                depthWeight *
                spatialWeight,
                1e-4);

        aoSum +=
            aoSample *
            weight;

        indirectSum +=
            indirectSample.rgb *
            weight;

        weightSum +=
            weight;
    }

    weightSum =
        max(
            weightSum,
            1e-4);

    float ao =
        aoSum /
        weightSum;

    float3 indirect =
        indirectSum /
        weightSum;

    float3 color =
        max(
            original.rgb,
            0.0);

    // Exponential visibility keeps the full slider useful without
    // turning high AO values into a flat gray multiplication.
    float visibility =
        exp2(
            -ao *
            AoStrength *
            1.15);

    color *= visibility;

    if (IndirectEnabled != 0)
    {
        float centerLuminance =
            max(
                Luminance(color),
                0.02);

        float bounceLuminance =
            max(
                Luminance(indirect),
                1e-4);

        // Bound bounced energy relative to the receiving pixel.
        // This prevents high settings from becoming a second exposure pass.
        float maximumBounce =
            0.10 +
            centerLuminance *
            0.45;

        float limiter =
            min(
                1.0,
                maximumBounce /
                bounceLuminance);

        color +=
            indirect *
            IndirectStrength *
            limiter;
    }

    return
        float4(
            color,
            original.a);
}

)";
        }

        DXGI_FORMAT DepthSrvFormat(
            const DXGI_FORMAT format)
        {
            if (format ==
                DXGI_FORMAT_R24G8_TYPELESS)
            {
                return
                    DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            }

            if (format ==
                DXGI_FORMAT_R32_TYPELESS)
            {
                return
                    DXGI_FORMAT_R32_FLOAT;
            }

            if (format ==
                DXGI_FORMAT_R16_TYPELESS)
            {
                return
                    DXGI_FORMAT_R16_UNORM;
            }

            return
                DXGI_FORMAT_UNKNOWN;
        }

        bool CreateSurface(
            ID3D11Device* device,
            const UINT width,
            const UINT height,
            SurfacePair& surface)
        {
            D3D11_TEXTURE2D_DESC aoDesc{};
            aoDesc.Width = width;
            aoDesc.Height = height;
            aoDesc.MipLevels = 1;
            aoDesc.ArraySize = 1;
            aoDesc.Format =
                DXGI_FORMAT_R16_FLOAT;
            aoDesc.SampleDesc.Count = 1;
            aoDesc.Usage =
                D3D11_USAGE_DEFAULT;
            aoDesc.BindFlags =
                D3D11_BIND_RENDER_TARGET |
                D3D11_BIND_SHADER_RESOURCE;

            HRESULT result =
                device->CreateTexture2D(
                    &aoDesc,
                    nullptr,
                    &surface.ao);

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateRenderTargetView(
                        surface.ao,
                        nullptr,
                        &surface.aoRtv);
            }

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateShaderResourceView(
                        surface.ao,
                        nullptr,
                        &surface.aoSrv);
            }

            D3D11_TEXTURE2D_DESC indirectDesc =
                aoDesc;

            indirectDesc.Format =
                DXGI_FORMAT_R16G16B16A16_FLOAT;

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateTexture2D(
                        &indirectDesc,
                        nullptr,
                        &surface.indirect);
            }

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateRenderTargetView(
                        surface.indirect,
                        nullptr,
                        &surface.indirectRtv);
            }

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateShaderResourceView(
                        surface.indirect,
                        nullptr,
                        &surface.indirectSrv);
            }

            return
                SUCCEEDED(result);
        }

        bool EnsureRuntime(
            ID3D11Device* device,
            const D3D11_TEXTURE2D_DESC& sceneDesc,
            std::string& error)
        {
            const UINT halfWidth =
                (sceneDesc.Width + 1) / 2;

            const UINT halfHeight =
                (sceneDesc.Height + 1) / 2;

            if (g_runtime.device == device &&
                g_runtime.fullWidth ==
                    sceneDesc.Width &&
                g_runtime.fullHeight ==
                    sceneDesc.Height &&
                g_runtime.sceneFormat ==
                    sceneDesc.Format)
            {
                return true;
            }

            ReleaseRuntime();

            if (device == nullptr ||
                sceneDesc.Width == 0 ||
                sceneDesc.Height == 0 ||
                sceneDesc.SampleDesc.Count != 1)
            {
                error =
                    "unsupported scene dimensions/sample count";
                return false;
            }

            g_runtime.device = device;
            device->AddRef();

            HRESULT result =
                device->CreateDeferredContext(
                    0,
                    &g_runtime.deferred);

            ID3DBlob* vertexBlob = nullptr;
            ID3DBlob* downsampleBlob = nullptr;
            ID3DBlob* analyzeBlob = nullptr;
            ID3DBlob* denoiseBlob = nullptr;
            ID3DBlob* compositeBlob = nullptr;

            if (SUCCEEDED(result) &&
                !Compile(
                    ShaderSource(),
                    "VSMain",
                    "vs_5_0",
                    &vertexBlob,
                    error))
            {
                result = E_FAIL;
            }

            if (SUCCEEDED(result) &&
                !Compile(
                    ShaderSource(),
                    "DownsamplePS",
                    "ps_5_0",
                    &downsampleBlob,
                    error))
            {
                result = E_FAIL;
            }

            if (SUCCEEDED(result) &&
                !Compile(
                    ShaderSource(),
                    "AnalyzePS",
                    "ps_5_0",
                    &analyzeBlob,
                    error))
            {
                result = E_FAIL;
            }

            if (SUCCEEDED(result) &&
                !Compile(
                    ShaderSource(),
                    "DenoisePS",
                    "ps_5_0",
                    &denoiseBlob,
                    error))
            {
                result = E_FAIL;
            }

            if (SUCCEEDED(result) &&
                !Compile(
                    ShaderSource(),
                    "CompositePS",
                    "ps_5_0",
                    &compositeBlob,
                    error))
            {
                result = E_FAIL;
            }

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateVertexShader(
                        vertexBlob->GetBufferPointer(),
                        vertexBlob->GetBufferSize(),
                        nullptr,
                        &g_runtime.vertexShader);
            }

            if (SUCCEEDED(result))
            {
                result =
                    device->CreatePixelShader(
                        downsampleBlob->GetBufferPointer(),
                        downsampleBlob->GetBufferSize(),
                        nullptr,
                        &g_runtime.downsampleShader);
            }

            if (SUCCEEDED(result))
            {
                result =
                    device->CreatePixelShader(
                        analyzeBlob->GetBufferPointer(),
                        analyzeBlob->GetBufferSize(),
                        nullptr,
                        &g_runtime.analyzeShader);
            }

            if (SUCCEEDED(result))
            {
                result =
                    device->CreatePixelShader(
                        denoiseBlob->GetBufferPointer(),
                        denoiseBlob->GetBufferSize(),
                        nullptr,
                        &g_runtime.denoiseShader);
            }

            if (SUCCEEDED(result))
            {
                result =
                    device->CreatePixelShader(
                        compositeBlob->GetBufferPointer(),
                        compositeBlob->GetBufferSize(),
                        nullptr,
                        &g_runtime.compositeShader);
            }

            D3D11_SAMPLER_DESC linearDesc{};
            linearDesc.Filter =
                D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            linearDesc.AddressU =
                D3D11_TEXTURE_ADDRESS_CLAMP;
            linearDesc.AddressV =
                D3D11_TEXTURE_ADDRESS_CLAMP;
            linearDesc.AddressW =
                D3D11_TEXTURE_ADDRESS_CLAMP;
            linearDesc.MaxLOD =
                D3D11_FLOAT32_MAX;

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateSamplerState(
                        &linearDesc,
                        &g_runtime.linearSampler);
            }

            D3D11_SAMPLER_DESC pointDesc =
                linearDesc;

            pointDesc.Filter =
                D3D11_FILTER_MIN_MAG_MIP_POINT;

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateSamplerState(
                        &pointDesc,
                        &g_runtime.pointSampler);
            }

            D3D11_BUFFER_DESC parameterDesc{};
            parameterDesc.ByteWidth =
                sizeof(Parameters);
            parameterDesc.Usage =
                D3D11_USAGE_DEFAULT;
            parameterDesc.BindFlags =
                D3D11_BIND_CONSTANT_BUFFER;

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateBuffer(
                        &parameterDesc,
                        nullptr,
                        &g_runtime.parameters);
            }

            D3D11_TEXTURE2D_DESC copyDesc =
                sceneDesc;

            copyDesc.Usage =
                D3D11_USAGE_DEFAULT;
            copyDesc.BindFlags =
                D3D11_BIND_SHADER_RESOURCE;
            copyDesc.CPUAccessFlags = 0;
            copyDesc.MiscFlags = 0;

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateTexture2D(
                        &copyDesc,
                        nullptr,
                        &g_runtime.sceneCopy);
            }

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateShaderResourceView(
                        g_runtime.sceneCopy,
                        nullptr,
                        &g_runtime.sceneCopySrv);
            }

            D3D11_TEXTURE2D_DESC workingDepthDesc{};
            workingDepthDesc.Width = halfWidth;
            workingDepthDesc.Height = halfHeight;
            workingDepthDesc.MipLevels = 1;
            workingDepthDesc.ArraySize = 1;
            workingDepthDesc.Format =
                DXGI_FORMAT_R32_FLOAT;
            workingDepthDesc.SampleDesc.Count = 1;
            workingDepthDesc.Usage =
                D3D11_USAGE_DEFAULT;
            workingDepthDesc.BindFlags =
                D3D11_BIND_RENDER_TARGET |
                D3D11_BIND_SHADER_RESOURCE;

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateTexture2D(
                        &workingDepthDesc,
                        nullptr,
                        &g_runtime.workingDepth);
            }

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateRenderTargetView(
                        g_runtime.workingDepth,
                        nullptr,
                        &g_runtime.workingDepthRtv);
            }

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateShaderResourceView(
                        g_runtime.workingDepth,
                        nullptr,
                        &g_runtime.workingDepthSrv);
            }

            D3D11_TEXTURE2D_DESC workingColorDesc =
                workingDepthDesc;

            workingColorDesc.Format =
                DXGI_FORMAT_R16G16B16A16_FLOAT;

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateTexture2D(
                        &workingColorDesc,
                        nullptr,
                        &g_runtime.workingColor);
            }

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateRenderTargetView(
                        g_runtime.workingColor,
                        nullptr,
                        &g_runtime.workingColorRtv);
            }

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateShaderResourceView(
                        g_runtime.workingColor,
                        nullptr,
                        &g_runtime.workingColorSrv);
            }

            if (SUCCEEDED(result) &&
                !CreateSurface(
                    device,
                    halfWidth,
                    halfHeight,
                    g_runtime.first))
            {
                result = E_FAIL;
            }

            if (SUCCEEDED(result) &&
                !CreateSurface(
                    device,
                    halfWidth,
                    halfHeight,
                    g_runtime.second))
            {
                result = E_FAIL;
            }

            D3D11_TEXTURE2D_DESC compositeDesc =
                sceneDesc;

            compositeDesc.Usage =
                D3D11_USAGE_DEFAULT;
            compositeDesc.BindFlags =
                D3D11_BIND_RENDER_TARGET;
            compositeDesc.CPUAccessFlags = 0;
            compositeDesc.MiscFlags = 0;

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateTexture2D(
                        &compositeDesc,
                        nullptr,
                        &g_runtime.composite);
            }

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateRenderTargetView(
                        g_runtime.composite,
                        nullptr,
                        &g_runtime.compositeRtv);
            }

            if (vertexBlob) vertexBlob->Release();
            if (downsampleBlob) downsampleBlob->Release();
            if (analyzeBlob) analyzeBlob->Release();
            if (denoiseBlob) denoiseBlob->Release();
            if (compositeBlob) compositeBlob->Release();

            if (FAILED(result))
            {
                if (error.empty())
                {
                    error =
                        "ambient depth resource allocation failed";
                }

                ReleaseRuntime();
                return false;
            }

            g_runtime.fullWidth =
                sceneDesc.Width;

            g_runtime.fullHeight =
                sceneDesc.Height;

            g_runtime.halfWidth =
                halfWidth;

            g_runtime.halfHeight =
                halfHeight;

            g_runtime.sceneFormat =
                sceneDesc.Format;

            return true;
        }

        bool RunPass(
            ID3D11DeviceContext* immediate,
            ID3D11RenderTargetView* const* targets,
            const UINT targetCount,
            ID3D11PixelShader* shader,
            ID3D11ShaderResourceView* const* resources,
            const UINT resourceCount,
            const UINT width,
            const UINT height,
            const Parameters& parameters,
            ID3D11Buffer* cameraBuffer,
            std::string& error)
        {
            g_runtime.deferred->ClearState();

            g_runtime.deferred->UpdateSubresource(
                g_runtime.parameters,
                0,
                nullptr,
                &parameters,
                0,
                0);

            D3D11_VIEWPORT viewport{};
            viewport.Width =
                static_cast<float>(width);
            viewport.Height =
                static_cast<float>(height);
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;

            g_runtime.deferred->OMSetRenderTargets(
                targetCount,
                targets,
                nullptr);

            g_runtime.deferred->RSSetViewports(
                1,
                &viewport);

            g_runtime.deferred->IASetInputLayout(
                nullptr);

            g_runtime.deferred->IASetPrimitiveTopology(
                D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            g_runtime.deferred->VSSetShader(
                g_runtime.vertexShader,
                nullptr,
                0);

            g_runtime.deferred->PSSetShader(
                shader,
                nullptr,
                0);

            g_runtime.deferred->PSSetConstantBuffers(
                0,
                1,
                &g_runtime.parameters);

            if (cameraBuffer != nullptr)
            {
                g_runtime.deferred->PSSetConstantBuffers(
                    12,
                    1,
                    &cameraBuffer);
            }

            g_runtime.deferred->PSSetShaderResources(
                0,
                resourceCount,
                resources);

            ID3D11SamplerState* samplers[2] =
            {
                g_runtime.linearSampler,
                g_runtime.pointSampler
            };

            g_runtime.deferred->PSSetSamplers(
                0,
                2,
                samplers);

            g_runtime.deferred->Draw(
                3,
                0);

            ID3D11ShaderResourceView*
                nullResources[6] =
            {
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr
            };

            g_runtime.deferred->PSSetShaderResources(
                0,
                6,
                nullResources);

            g_runtime.deferred->OMSetRenderTargets(
                0,
                nullptr,
                nullptr);

            ID3D11CommandList* commands = nullptr;

            HRESULT result =
                g_runtime.deferred->FinishCommandList(
                    FALSE,
                    &commands);

            if (SUCCEEDED(result) &&
                commands != nullptr)
            {
                immediate->ExecuteCommandList(
                    commands,
                    TRUE);
            }

            if (commands)
            {
                commands->Release();
            }

            if (FAILED(result))
            {
                std::ostringstream stream;
                stream
                    << "FinishCommandList HRESULT=0x"
                    << std::hex
                    << std::uppercase
                    << static_cast<std::uint32_t>(
                        result);

                error = stream.str();
                return false;
            }

            return true;
        }
    }

    bool Apply(
        const std::uint64_t frame,
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const gzfx::diagnostics::
            resource_intelligence::
            LiveInputs& inputs) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            const std::uint32_t mode =
                gzfx::render::state::
                    GetDevelopmentMode();

            if (mode != 1 &&
                mode != 2)
            {
                WriteLog(
                    frame,
                    "skipped_mode");

                return false;
            }

            const auto& settings =
                gzfx::settings::Get();

            if (!settings.horizonOcclusionEnabled &&
                !settings.indirectBounceEnabled)
            {
                WriteLog(
                    frame,
                    "skipped_disabled");

                return false;
            }

            if (inputs.sceneColorTexture == nullptr ||
                inputs.depthTexture == nullptr ||
                inputs.cameraBuffer == nullptr)
            {
                WriteLog(
                    frame,
                    "skipped_inputs");

                return false;
            }

            std::string error;

            if (!EnsureRuntime(
                    device,
                    inputs.sceneColorDesc,
                    error))
            {
                WriteLog(
                    frame,
                    "failed_runtime",
                    error);

                return false;
            }

            const DXGI_FORMAT depthFormat =
                DepthSrvFormat(
                    inputs.depthDesc.Format);

            if (depthFormat ==
                DXGI_FORMAT_UNKNOWN)
            {
                WriteLog(
                    frame,
                    "failed_depth_format");

                return false;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC
                depthSrvDesc{};

            depthSrvDesc.Format =
                depthFormat;

            depthSrvDesc.ViewDimension =
                D3D11_SRV_DIMENSION_TEXTURE2D;

            depthSrvDesc.Texture2D.MipLevels = 1;

            ID3D11ShaderResourceView* depthSrv = nullptr;

            HRESULT result =
                device->CreateShaderResourceView(
                    inputs.depthTexture,
                    &depthSrvDesc,
                    &depthSrv);

            if (FAILED(result) ||
                depthSrv == nullptr)
            {
                WriteLog(
                    frame,
                    "failed_depth_srv");

                return false;
            }

            context->CopyResource(
                g_runtime.sceneCopy,
                inputs.sceneColorTexture);

            Parameters parameters{};
            parameters.fullInvWidth =
                1.0f /
                static_cast<float>(
                    g_runtime.fullWidth);

            parameters.fullInvHeight =
                1.0f /
                static_cast<float>(
                    g_runtime.fullHeight);

            parameters.halfInvWidth =
                1.0f /
                static_cast<float>(
                    g_runtime.halfWidth);

            parameters.halfInvHeight =
                1.0f /
                static_cast<float>(
                    g_runtime.halfHeight);

            parameters.aoStrength =
                settings.horizonOcclusionStrength;

            parameters.aoRadius =
                settings.horizonOcclusionRadiusPixels;

            parameters.indirectStrength =
                settings.indirectBounceStrength;

            parameters.indirectRadius =
                settings.indirectBounceRadiusPixels;

            parameters.indirectSaturation =
                settings.indirectBounceSaturation;

            parameters.quality =
                settings.horizonOcclusionQuality;

            parameters.mode =
                mode;

            parameters.aoEnabled =
                settings.horizonOcclusionEnabled
                ? 1u
                : 0u;

            parameters.indirectEnabled =
                settings.indirectBounceEnabled
                ? 1u
                : 0u;

            ID3D11RenderTargetView*
                downsampleTargets[2] =
            {
                g_runtime.workingDepthRtv,
                g_runtime.workingColorRtv
            };

            ID3D11ShaderResourceView*
                downsampleResources[2] =
            {
                g_runtime.sceneCopySrv,
                depthSrv
            };

            if (!RunPass(
                    context,
                    downsampleTargets,
                    2,
                    g_runtime.downsampleShader,
                    downsampleResources,
                    2,
                    g_runtime.halfWidth,
                    g_runtime.halfHeight,
                    parameters,
                    nullptr,
                    error))
            {
                depthSrv->Release();

                WriteLog(
                    frame,
                    "failed_downsample",
                    error);

                return false;
            }

            ID3D11RenderTargetView*
                analyzeTargets[2] =
            {
                g_runtime.first.aoRtv,
                g_runtime.first.indirectRtv
            };

            ID3D11ShaderResourceView*
                analyzeResources[6] =
            {
                g_runtime.sceneCopySrv,
                depthSrv,
                nullptr,
                nullptr,
                g_runtime.workingDepthSrv,
                g_runtime.workingColorSrv
            };

            if (!RunPass(
                    context,
                    analyzeTargets,
                    2,
                    g_runtime.analyzeShader,
                    analyzeResources,
                    6,
                    g_runtime.halfWidth,
                    g_runtime.halfHeight,
                    parameters,
                    inputs.cameraBuffer,
                    error))
            {
                depthSrv->Release();

                WriteLog(
                    frame,
                    "failed_analyze",
                    error);

                return false;
            }

            parameters.axisX = 1.0f;
            parameters.axisY = 0.0f;

            ID3D11RenderTargetView*
                horizontalTargets[2] =
            {
                g_runtime.second.aoRtv,
                g_runtime.second.indirectRtv
            };

            ID3D11ShaderResourceView*
                horizontalResources[4] =
            {
                nullptr,
                nullptr,
                g_runtime.first.aoSrv,
                g_runtime.first.indirectSrv
            };

            if (!RunPass(
                    context,
                    horizontalTargets,
                    2,
                    g_runtime.denoiseShader,
                    horizontalResources,
                    4,
                    g_runtime.halfWidth,
                    g_runtime.halfHeight,
                    parameters,
                    nullptr,
                    error))
            {
                depthSrv->Release();

                WriteLog(
                    frame,
                    "failed_denoise_horizontal",
                    error);

                return false;
            }

            parameters.axisX = 0.0f;
            parameters.axisY = 1.0f;

            ID3D11RenderTargetView*
                verticalTargets[2] =
            {
                g_runtime.first.aoRtv,
                g_runtime.first.indirectRtv
            };

            ID3D11ShaderResourceView*
                verticalResources[4] =
            {
                nullptr,
                nullptr,
                g_runtime.second.aoSrv,
                g_runtime.second.indirectSrv
            };

            if (!RunPass(
                    context,
                    verticalTargets,
                    2,
                    g_runtime.denoiseShader,
                    verticalResources,
                    4,
                    g_runtime.halfWidth,
                    g_runtime.halfHeight,
                    parameters,
                    nullptr,
                    error))
            {
                depthSrv->Release();

                WriteLog(
                    frame,
                    "failed_denoise_vertical",
                    error);

                return false;
            }

            ID3D11RenderTargetView*
                compositeTargets[1] =
            {
                g_runtime.compositeRtv
            };

            ID3D11ShaderResourceView*
                compositeResources[4] =
            {
                g_runtime.sceneCopySrv,
                depthSrv,
                g_runtime.first.aoSrv,
                g_runtime.first.indirectSrv
            };

            if (!RunPass(
                    context,
                    compositeTargets,
                    1,
                    g_runtime.compositeShader,
                    compositeResources,
                    4,
                    g_runtime.fullWidth,
                    g_runtime.fullHeight,
                    parameters,
                    nullptr,
                    error))
            {
                depthSrv->Release();

                WriteLog(
                    frame,
                    "failed_composite",
                    error);

                return false;
            }

            context->CopyResource(
                inputs.sceneColorTexture,
                g_runtime.composite);

            depthSrv->Release();

            ++g_applyCount;
            g_lastFrame = frame;

            WriteLog(
                frame,
                "applied");

            return true;
        }
        catch (...)
        {
            WriteLog(
                frame,
                "exception");

            return false;
        }
    }
}
