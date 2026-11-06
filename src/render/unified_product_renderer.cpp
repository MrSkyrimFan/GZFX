#include "gzfx/render/unified_product_renderer.hpp"

#include "gzfx/paths.hpp"
#include "gzfx/render/runtime_state.hpp"
#include "gzfx/render/unified_product_math.hpp"
#include "gzfx/settings.hpp"
#include "gzfx/version.hpp"

#include <Windows.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

namespace gzfx::render::unified_product_renderer
{
    namespace fs = std::filesystem;

    namespace
    {
        struct alignas(16) Parameters
        {
            float invWidth = 0.0f;
            float invHeight = 0.0f;
            float pyramidInvWidth = 0.0f;
            float pyramidInvHeight = 0.0f;

            std::uint32_t effectMask = 0;
            std::uint32_t quality = 0;
            std::uint32_t mode = 0;
            std::uint32_t frameIndex = 0;

            std::uint32_t bloomLevels = 0;
            std::uint32_t dofSamples = 0;
            std::uint32_t motionSamples = 0;
            std::uint32_t denoiseSamples = 0;

            float manualExposureEv = 0.0f;
            float exposureCompensationEv = 0.0f;
            float exposureKey = 0.18f;
            float deltaSeconds = 1.0f / 60.0f;

            float exposureMinEv = -8.0f;
            float exposureMaxEv = 8.0f;
            float exposureSpeedUp = 2.5f;
            float exposureSpeedDown = 1.5f;

            float bloomStrength = 0.0f;
            float bloomThreshold = 1.0f;
            float bloomSoftKnee = 0.5f;
            float bloomRadius = 4.0f;

            float lensDirtStrength = 0.0f;
            float ambientStrength = 0.62f;
            float ambientRadius = 4.0f;
            float localGlowStrength = 0.18f;

            float localGlowThreshold = 1.10f;
            float localGlowRadius = 8.0f;
            float colorContrast = 1.0f;
            float colorGamma = 1.0f;

            float colorSaturation = 1.0f;
            float colorVibrance = 0.0f;
            float colorTemperature = 0.0f;
            float colorTint = 0.0f;

            float colorLift = 0.0f;
            float colorGain = 1.0f;
            float toneToe = 0.0f;
            float toneShoulder = 0.0f;

            float toneWhiteClip = 16.0f;
            float dofFocusDistance = 2500.0f;
            float dofFocusRange = 800.0f;
            float dofNearStrength = 0.0f;

            float dofFarStrength = 0.0f;
            float dofMaxRadius = 0.0f;
            float motionBlurStrength = 0.0f;
            float motionBlurMaxPixels = 24.0f;

            float depthFogStart = 2000.0f;
            float depthFogEnd = 15000.0f;
            float depthFogDensity = 0.0f;
            float depthFogColorR = 0.55f;

            float depthFogColorG = 0.62f;
            float depthFogColorB = 0.70f;
            float vignetteStrength = 0.0f;
            float vignetteRadius = 0.72f;

            float vignetteSoftness = 0.28f;
            float filmGrainStrength = 0.0f;
            float filmGrainScale = 1.0f;
            float chromaticAberrationStrength = 0.0f;

            float denoiseStrength = 0.0f;
            float sharpenStrength = 0.0f;
            float sharpenClamp = 0.25f;
            float letterboxAmount = 0.0f;

            float pillarboxAmount = 0.0f;
            float padding0 = 0.0f;
            float padding1 = 0.0f;
            float padding2 = 0.0f;

            std::uint32_t productEnabled = 0;
            std::uint32_t exposureEnabled = 0;
            std::uint32_t autoExposureEnabled = 0;
            std::uint32_t bloomEnabled = 0;

            std::uint32_t lensDirtEnabled = 0;
            std::uint32_t colorEnabled = 0;
            std::uint32_t dofEnabled = 0;
            std::uint32_t motionBlurEnabled = 0;

            std::uint32_t depthFogEnabled = 0;
            std::uint32_t vignetteEnabled = 0;
            std::uint32_t filmGrainEnabled = 0;
            std::uint32_t chromaticAberrationEnabled = 0;

            std::uint32_t denoiseEnabled = 0;
            std::uint32_t sharpenEnabled = 0;
            std::uint32_t barsEnabled = 0;
            std::uint32_t localGlowEnabled = 0;

            std::uint32_t ambientContactEnabled = 0;
            std::uint32_t exposureHistoryValid = 0;
            std::uint32_t pyramidMipCount = 1;
            std::uint32_t motionAvailable = 0;
        };

        static_assert(sizeof(Parameters) % 16 == 0);

        struct Runtime
        {
            ID3D11Device* device = nullptr;
            ID3D11DeviceContext* deferred = nullptr;

            ID3D11VertexShader* vertexShader = nullptr;
            ID3D11PixelShader* luminanceShader = nullptr;
            ID3D11PixelShader* exposureShader = nullptr;
            ID3D11PixelShader* compositeShader = nullptr;

            ID3D11SamplerState* linearSampler = nullptr;
            ID3D11SamplerState* pointSampler = nullptr;
            ID3D11Buffer* parameters = nullptr;

            ID3D11Texture2D* sourceCopy = nullptr;
            ID3D11ShaderResourceView* sourceSrv = nullptr;

            ID3D11Texture2D* output = nullptr;
            ID3D11RenderTargetView* outputRtv = nullptr;

            ID3D11Texture2D* pyramid = nullptr;
            ID3D11ShaderResourceView* pyramidSrv = nullptr;
            ID3D11RenderTargetView* pyramidRtv = nullptr;

            ID3D11Texture2D* exposure[2]{};
            ID3D11ShaderResourceView* exposureSrv[2]{};
            ID3D11RenderTargetView* exposureRtv[2]{};

            UINT width = 0;
            UINT height = 0;
            UINT pyramidWidth = 0;
            UINT pyramidHeight = 0;
            UINT pyramidMipCount = 0;
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
            std::uint32_t exposureIndex = 0;
            bool exposureHistoryValid = false;
            std::uint64_t generation = 0;
        };

        struct TimingRow
        {
            std::uint64_t frame = 0;
            double submitMilliseconds = 0.0;
            std::uint64_t effectMask = 0;
            std::uint32_t quality = 0;
        };

        std::mutex g_mutex;
        Runtime g_runtime{};
        Status g_status{};
        std::string g_lastError;
        std::deque<TimingRow> g_timings;
        std::uint64_t g_lastStatusWriteFrame = 0;
        std::uint64_t g_lastTimingFlushFrame = 0;
        std::uint64_t g_lastGraphGeneration = 0;
        std::uint32_t g_captureRemaining = 0;
        bool g_manualCaptureRequested = false;

        template <class T>
        void Release(T*& pointer)
        {
            if (pointer != nullptr)
            {
                pointer->Release();
                pointer = nullptr;
            }
        }

        void ReleaseRuntime()
        {
            for (std::uint32_t index = 0;
                 index < 2;
                 ++index)
            {
                Release(g_runtime.exposureRtv[index]);
                Release(g_runtime.exposureSrv[index]);
                Release(g_runtime.exposure[index]);
            }

            Release(g_runtime.pyramidRtv);
            Release(g_runtime.pyramidSrv);
            Release(g_runtime.pyramid);
            Release(g_runtime.outputRtv);
            Release(g_runtime.output);
            Release(g_runtime.sourceSrv);
            Release(g_runtime.sourceCopy);
            Release(g_runtime.parameters);
            Release(g_runtime.pointSampler);
            Release(g_runtime.linearSampler);
            Release(g_runtime.compositeShader);
            Release(g_runtime.exposureShader);
            Release(g_runtime.luminanceShader);
            Release(g_runtime.vertexShader);
            Release(g_runtime.deferred);
            Release(g_runtime.device);

            const std::uint64_t generation =
                g_runtime.generation;

            g_runtime = {};
            g_runtime.generation = generation;
        }

        DXGI_FORMAT DepthSrvFormat(
            const DXGI_FORMAT format)
        {
            switch (format)
            {
            case DXGI_FORMAT_R24G8_TYPELESS:
                return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;

            case DXGI_FORMAT_R32_TYPELESS:
                return DXGI_FORMAT_R32_FLOAT;

            case DXGI_FORMAT_R16_TYPELESS:
                return DXGI_FORMAT_R16_UNORM;

            case DXGI_FORMAT_D32_FLOAT:
                return DXGI_FORMAT_R32_FLOAT;

            case DXGI_FORMAT_D16_UNORM:
                return DXGI_FORMAT_R16_UNORM;

            default:
                return DXGI_FORMAT_UNKNOWN;
            }
        }

        bool Compile(
            const char* source,
            const char* entry,
            const char* profile,
            ID3DBlob** blob,
            std::string& error)
        {
            if (blob == nullptr)
            {
                error = "null shader blob target";
                return false;
            }

            *blob = nullptr;
            ID3DBlob* errors = nullptr;

            const HRESULT result =
                D3DCompile(
                    source,
                    std::strlen(source),
                    "GZFXUnifiedProductRenderer",
                    nullptr,
                    nullptr,
                    entry,
                    profile,
                    D3DCOMPILE_OPTIMIZATION_LEVEL3 |
                        D3DCOMPILE_WARNINGS_ARE_ERRORS,
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
                        "D3DCompile failed without diagnostics";
                }
            }

            Release(errors);
            return SUCCEEDED(result);
        }

        const char* ShaderSource()
        {
            static const std::string source =
                std::string(R"GZFX_HLSL(
struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput VSMain(uint id : SV_VertexID)
{
    float2 uv = float2((id << 1) & 2, id & 2);

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

cbuffer GZFXUnifiedParameters : register(b0)
{
    float InvWidth;
    float InvHeight;
    float PyramidInvWidth;
    float PyramidInvHeight;

    uint EffectMask;
    uint Quality;
    uint Mode;
    uint FrameIndex;

    uint BloomLevels;
    uint DofSamples;
    uint MotionSamples;
    uint DenoiseSamples;

    float ManualExposureEv;
    float ExposureCompensationEv;
    float ExposureKey;
    float DeltaSeconds;

    float ExposureMinEv;
    float ExposureMaxEv;
    float ExposureSpeedUp;
    float ExposureSpeedDown;

    float BloomStrength;
    float BloomThreshold;
    float BloomSoftKnee;
    float BloomRadius;

    float LensDirtStrength;
    float AmbientStrength;
    float AmbientRadius;
    float LocalGlowStrength;

    float LocalGlowThreshold;
    float LocalGlowRadius;
    float ColorContrast;
    float ColorGamma;

    float ColorSaturation;
    float ColorVibrance;
    float ColorTemperature;
    float ColorTint;

    float ColorLift;
    float ColorGain;
    float ToneToe;
    float ToneShoulder;

    float ToneWhiteClip;
    float DofFocusDistance;
    float DofFocusRange;
    float DofNearStrength;

    float DofFarStrength;
    float DofMaxRadius;
    float MotionBlurStrength;
    float MotionBlurMaxPixels;

    float DepthFogStart;
    float DepthFogEnd;
    float DepthFogDensity;
    float DepthFogColorR;

    float DepthFogColorG;
    float DepthFogColorB;
    float VignetteStrength;
    float VignetteRadius;

    float VignetteSoftness;
    float FilmGrainStrength;
    float FilmGrainScale;
    float ChromaticAberrationStrength;

    float DenoiseStrength;
    float SharpenStrength;
    float SharpenClamp;
    float LetterboxAmount;

    float PillarboxAmount;
    float ParameterPadding0;
    float ParameterPadding1;
    float ParameterPadding2;

    uint ProductEnabled;
    uint ExposureEnabled;
    uint AutoExposureEnabled;
    uint BloomEnabled;

    uint LensDirtEnabled;
    uint ColorEnabled;
    uint DofEnabled;
    uint MotionBlurEnabled;

    uint DepthFogEnabled;
    uint VignetteEnabled;
    uint FilmGrainEnabled;
    uint ChromaticAberrationEnabled;

    uint DenoiseEnabled;
    uint SharpenEnabled;
    uint BarsEnabled;
    uint LocalGlowEnabled;

    uint AmbientContactEnabled;
    uint ExposureHistoryValid;
    uint PyramidMipCount;
    uint MotionAvailable;
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
Texture2D<float4> MotionTexture : register(t2);
Texture2D<float4> PyramidTexture : register(t3);
Texture2D<float4> PreviousExposure : register(t4);

SamplerState LinearSampler : register(s0);
SamplerState PointSampler : register(s1);

static const uint EFFECT_EXPOSURE_MANUAL = 1u << 0;
static const uint EFFECT_EXPOSURE_AUTO = 1u << 1;
static const uint EFFECT_BLOOM = 1u << 2;
static const uint EFFECT_LENS_DIRT = 1u << 3;
static const uint EFFECT_COLOR = 1u << 4;
static const uint EFFECT_TONE = 1u << 5;
static const uint EFFECT_DOF = 1u << 6;
static const uint EFFECT_MOTION = 1u << 7;
static const uint EFFECT_FOG = 1u << 8;
static const uint EFFECT_VIGNETTE = 1u << 9;
static const uint EFFECT_GRAIN = 1u << 10;
static const uint EFFECT_CA = 1u << 11;
static const uint EFFECT_DENOISE = 1u << 12;
static const uint EFFECT_SHARPEN = 1u << 13;
static const uint EFFECT_BARS = 1u << 14;
static const uint EFFECT_CONTACT = 1u << 15;
static const uint EFFECT_LOCAL_GLOW = 1u << 18;

bool HasEffect(uint flag)
{
    return (EffectMask & flag) != 0;
}

float Luminance(float3 color)
{
    return dot(
        max(color, 0.0),
        float3(0.2126, 0.7152, 0.0722));
}

float2 DepthUv(float2 uv)
{
    return saturate(
        uv * DynamicResolutionParams1.xy);
}

float3 ReconstructView(
    float2 uv,
    float depth)
{
    float4 clip =
        float4(
            2.0 * float2(
                uv.x,
                1.0 - uv.y) - 1.0,
            depth,
            1.0);

    float4 view =
        mul(
            CameraProjInverse,
            clip);

    return view.xyz /
        max(abs(view.w), 1e-6);
}

float3 ReconstructNormal(
    float2 uv,
    float centerDepth)
{
    float2 dx = float2(InvWidth, 0.0);
    float2 dy = float2(0.0, InvHeight);

    float depthX =
        SceneDepth.SampleLevel(
            PointSampler,
            DepthUv(saturate(uv + dx)),
            0).x;

    float depthY =
        SceneDepth.SampleLevel(
            PointSampler,
            DepthUv(saturate(uv + dy)),
            0).x;

    float3 center =
        ReconstructView(uv, centerDepth);

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

    if (dot(normal, -center) < 0.0)
    {
        normal = -normal;
    }

    return normal;
}

float Hash12(float2 p)
{
    float3 p3 =
        frac(float3(p.xyx) * 0.1031);

    p3 += dot(
        p3,
        p3.yzx + 33.33);

    return frac((p3.x + p3.y) * p3.z);
}

float ThresholdWeight(
    float luminance,
    float threshold,
    float softKnee)
{
    float knee =
        max(
            threshold * softKnee,
            1e-5);

    float soft =
        clamp(
            luminance - threshold + knee,
            0.0,
            2.0 * knee);

    soft =
        (soft * soft) /
        (4.0 * knee + 1e-5);

    float hard =
        max(
            luminance - threshold,
            0.0);

    return max(hard, soft) /
        max(luminance, 1e-5);
}

float BloomWeight(float luminance)
{
    return ThresholdWeight(
        luminance,
        BloomThreshold,
        BloomSoftKnee);
}

float4 PSLuminanceBloom(VSOutput input) : SV_TARGET
{
    float3 color =
        max(
            SceneColor.SampleLevel(
                LinearSampler,
                input.uv,
                0).rgb,
            0.0);

    float luminance =
        max(
            Luminance(color),
            1e-5);

    float3 bright = 0.0;

    if (HasEffect(EFFECT_BLOOM))
    {
        float bloomWeight =
            BloomEnabled != 0
            ? BloomWeight(luminance)
            : 0.0;

        float localGlowWeight =
            LocalGlowEnabled != 0
            ? ThresholdWeight(
                luminance,
                max(LocalGlowThreshold, 1e-4),
                0.35)
            : 0.0;

        bright =
            color *
            max(
                bloomWeight,
                localGlowWeight);
    }

    return float4(
        bright,
        log2(luminance));
}

float4 PSExposure(VSOutput input) : SV_TARGET
{
    float4 average =
        PyramidTexture.SampleLevel(
            LinearSampler,
            float2(0.5, 0.5),
            max(float(PyramidMipCount) - 1.0, 0.0));

    float luminance =
        exp2(average.a);

    if (ExposureEnabled == 0 ||
        AutoExposureEnabled == 0)
    {
        return float4(
            1.0,
            1.0,
            luminance,
            1.0);
    }

    float target =
        ExposureKey /
        max(luminance, 1e-5);

    float targetEv =
        clamp(
            log2(max(target, 1e-6)) +
                ExposureCompensationEv,
            ExposureMinEv,
            ExposureMaxEv);

    target = exp2(targetEv);

    float previous =
        ExposureHistoryValid != 0
        ? PreviousExposure.SampleLevel(
            PointSampler,
            float2(0.5, 0.5),
            0).r
        : target;

    float speed =
        target > previous
        ? ExposureSpeedUp
        : ExposureSpeedDown;

    float alpha =
        saturate(
            1.0 -
            exp(
                -max(speed, 0.0) *
                max(DeltaSeconds, 0.0)));

    float adapted =
        lerp(
            previous,
            target,
            alpha);

    return float4(
        adapted,
        target,
        luminance,
        1.0);
}

static const float2 SAMPLE_DIRECTIONS[16] =
{
    float2( 1.0000,  0.0000),
    float2( 0.9239,  0.3827),
    float2( 0.7071,  0.7071),
    float2( 0.3827,  0.9239),
    float2( 0.0000,  1.0000),
    float2(-0.3827,  0.9239),
    float2(-0.7071,  0.7071),
    float2(-0.9239,  0.3827),
    float2(-1.0000,  0.0000),
    float2(-0.9239, -0.3827),
    float2(-0.7071, -0.7071),
    float2(-0.3827, -0.9239),
    float2( 0.0000, -1.0000),
    float2( 0.3827, -0.9239),
    float2( 0.7071, -0.7071),
    float2( 0.9239, -0.3827)
};

float AmbientContact(float2 uv)
{
    if (AmbientContactEnabled == 0 ||
        AmbientStrength <= 0.0)
    {
        return 0.0;
    }

    float centerDepth =
        SceneDepth.SampleLevel(
            PointSampler,
            DepthUv(uv),
            0).x;

    if (centerDepth >= 0.99999)
    {
        return 0.0;
    }

    float3 center =
        ReconstructView(
            uv,
            centerDepth);

    float3 normal =
        ReconstructNormal(
            uv,
            centerDepth);

    float angle =
        Hash12(
            floor(
                uv / float2(InvWidth, InvHeight))) *
        6.2831853;

    float cs = cos(angle);
    float sn = sin(angle);
    float2x2 rotation =
        float2x2(cs, -sn, sn, cs);

    float occlusion = 0.0;
    float weightTotal = 0.0;

    [unroll]
    for (uint index = 0;
         index < 12;
         ++index)
    {
        float radialScale =
            0.40 +
            0.60 *
            float((index % 3) + 1) /
            3.0;

        float2 direction =
            mul(
                rotation,
                SAMPLE_DIRECTIONS[index]);

        float2 sampleUv =
            saturate(
                uv +
                direction *
                float2(InvWidth, InvHeight) *
                AmbientRadius *
                radialScale);

        float sampleDepth =
            SceneDepth.SampleLevel(
                PointSampler,
                DepthUv(sampleUv),
                0).x;

        if (sampleDepth >= 0.99999)
        {
            continue;
        }

        float3 neighbor =
            ReconstructView(
                sampleUv,
                sampleDepth);

        float3 delta = neighbor - center;
        float distanceToSample =
)GZFX_HLSL") +
                R"GZFX_HLSL(            length(delta);

        if (distanceToSample <= 1e-5)
        {
            continue;
        }

        float3 direction3 =
            delta / distanceToSample;

        float hemisphere =
            saturate(
                dot(normal, direction3));

        float proximity =
            saturate(
                1.0 -
                distanceToSample /
                max(abs(center.z) * 0.08, 1.0));

        float bias =
            max(
                abs(center.z) * 0.00065,
                0.035);

        float closer =
            saturate(
                (center.z - neighbor.z - bias) /
                max(bias * 10.0, 0.001));

        float sampleOcclusion =
            max(
                hemisphere * 0.72,
                closer);

        float sampleWeight =
            proximity *
            (0.65 + 0.35 * radialScale);

        occlusion +=
            sampleOcclusion *
            sampleWeight;

        weightTotal +=
            sampleWeight;
    }

    return weightTotal > 0.0
        ? saturate(
            occlusion /
            weightTotal)
        : 0.0;
}

float3 DenoiseColor(
    float2 uv,
    float centerDepth,
    float3 centerColor)
{
    if (DenoiseEnabled == 0 ||
        DenoiseStrength <= 0.0)
    {
        return centerColor;
    }

    float3 sum = centerColor;
    float weight = 1.0;

    [loop]
    for (uint index = 0;
         index < 8;
         ++index)
    {
        if (index >= DenoiseSamples)
        {
            break;
        }

        float2 sampleUv =
            saturate(
                uv +
                SAMPLE_DIRECTIONS[index * 2] *
                float2(InvWidth, InvHeight) *
                1.5);

        float sampleDepth =
            SceneDepth.SampleLevel(
                PointSampler,
                DepthUv(sampleUv),
                0).x;

        float depthWeight =
            exp(
                -abs(sampleDepth - centerDepth) *
                4000.0);

        float3 sampleColor =
            SceneColor.SampleLevel(
                LinearSampler,
                sampleUv,
                0).rgb;

        sum += sampleColor * depthWeight;
        weight += depthWeight;
    }

    float3 filtered =
        sum /
        max(weight, 1e-5);

    return lerp(
        centerColor,
        filtered,
        saturate(DenoiseStrength));
}

float3 MotionBlurColor(
    float2 uv,
    float3 centerColor)
{
    if (MotionBlurEnabled == 0 ||
        MotionAvailable == 0 ||
        MotionBlurStrength <= 0.0)
    {
        return centerColor;
    }

    float2 motion =
        MotionTexture.SampleLevel(
            PointSampler,
            uv,
            0).xy;

    float2 maximumUv =
        float2(
            MotionBlurMaxPixels * InvWidth,
            MotionBlurMaxPixels * InvHeight);

    motion =
        clamp(
            motion * MotionBlurStrength,
            -maximumUv,
            maximumUv);

    float3 sum = centerColor;
    float count = 1.0;

    [loop]
    for (uint index = 0;
         index < 12;
         ++index)
    {
        if (index >= MotionSamples)
        {
            break;
        }

        float position =
            (float(index) + 0.5) /
            max(float(MotionSamples), 1.0) -
            0.5;

        sum +=
            SceneColor.SampleLevel(
                LinearSampler,
                saturate(
                    uv +
                    motion * position),
                0).rgb;

        count += 1.0;
    }

    return sum / count;
}

float3 DepthOfFieldColor(
    float2 uv,
    float depth,
    float3 centerColor)
{
    if (DofEnabled == 0 ||
        DofMaxRadius <= 0.0)
    {
        return centerColor;
    }

    float distanceToCamera =
        abs(
            ReconstructView(
                uv,
                depth).z);

    float focusMinimum =
        max(
            DofFocusDistance -
                DofFocusRange * 0.5,
            0.0);

    float focusMaximum =
        DofFocusDistance +
        DofFocusRange * 0.5;

    float nearDefocus =
        distanceToCamera < focusMinimum
        ? saturate(
            (focusMinimum - distanceToCamera) /
            max(DofFocusRange, 1.0)) *
            DofNearStrength
        : 0.0;

    float farDefocus =
        distanceToCamera > focusMaximum
        ? saturate(
            (distanceToCamera - focusMaximum) /
            max(DofFocusRange, 1.0)) *
            DofFarStrength
        : 0.0;

    float defocus =
        saturate(
            max(
                nearDefocus,
                farDefocus));

    if (defocus <= 1e-4)
    {
        return centerColor;
    }

    float radius =
        DofMaxRadius * defocus;

    float3 sum = centerColor;
    float count = 1.0;

    [loop]
    for (uint index = 0;
         index < 16;
         ++index)
    {
        if (index >= DofSamples)
        {
            break;
        }

        float radial =
            sqrt(
                (float(index) + 0.5) /
                max(float(DofSamples), 1.0));

        float2 sampleUv =
            saturate(
                uv +
                SAMPLE_DIRECTIONS[index] *
                float2(InvWidth, InvHeight) *
                radius *
                radial);

        sum +=
            SceneColor.SampleLevel(
                LinearSampler,
                sampleUv,
                0).rgb;

        count += 1.0;
    }

    return lerp(
        centerColor,
        sum / count,
        defocus);
}

float3 BloomColor(float2 uv)
{
    if (!HasEffect(EFFECT_BLOOM))
    {
        return 0.0;
    }

    float3 bloom = 0.0;
    float3 localGlow = 0.0;
    float bloomWeightTotal = 0.0;
    float glowWeightTotal = 0.0;

    float bloomRadiusScale =
        clamp(
            BloomRadius / 4.0,
            0.25,
            2.0);

    float glowRadiusScale =
        clamp(
            LocalGlowRadius / 8.0,
            0.25,
            2.0);

    float maximumLod =
        max(
            float(PyramidMipCount) - 1.0,
            0.0);

    [loop]
    for (uint level = 0;
         level < 6;
         ++level)
    {
        if (level >= BloomLevels ||
            level >= PyramidMipCount)
        {
            break;
        }

        float weight =
            1.0 /
            (1.0 + float(level));

        if (BloomEnabled != 0 &&
            BloomStrength > 0.0)
        {
            float bloomLod =
                min(
                    float(level) *
                        bloomRadiusScale,
                    maximumLod);

            bloom +=
                PyramidTexture.SampleLevel(
                    LinearSampler,
                    uv,
                    bloomLod).rgb *
                weight;

            bloomWeightTotal += weight;
        }

        if (LocalGlowEnabled != 0 &&
            LocalGlowStrength > 0.0)
        {
            float glowLod =
                min(
                    float(level) *
                        glowRadiusScale,
                    maximumLod);

            localGlow +=
                PyramidTexture.SampleLevel(
                    LinearSampler,
                    uv,
                    glowLod).rgb *
                weight;

            glowWeightTotal += weight;
        }
    }

    bloom /=
        max(
            bloomWeightTotal,
            1e-5);

    localGlow /=
        max(
            glowWeightTotal,
            1e-5);

    float3 result =
        bloom *
            max(BloomStrength, 0.0) +
        localGlow *
            max(LocalGlowStrength, 0.0);

    if (LensDirtEnabled != 0 &&
        LensDirtStrength > 0.0)
    {
        float2 cell =
            floor(uv * 13.0);

        float dirt =
            lerp(
                0.45,
                1.35,
                Hash12(cell));

        result *=
            lerp(
                1.0,
                dirt,
                saturate(LensDirtStrength));
    }

    return result;
}

float3 ApplyColor(float3 color)
{
    if (ColorEnabled == 0)
    {
        return color;
    }

    float3 whiteBalance =
        float3(
            1.0 + ColorTemperature * 0.18 - ColorTint * 0.05,
            1.0 + ColorTint * 0.12,
            1.0 - ColorTemperature * 0.18 - ColorTint * 0.05);

    color *=
        max(
            whiteBalance,
            0.1);

    color =
        max(
            color + ColorLift,
            0.0) *
        max(ColorGain, 0.0);

    color =
        pow(
            max(color, 0.0),
            1.0 / max(ColorGamma, 0.05));

    float luminance =
        Luminance(color);

    float saturation =
        max(ColorSaturation, 0.0);

    color =
        lerp(
            luminance.xxx,
            color,
            saturation);

    float maximumChannel =
        max(
            color.r,
            max(color.g, color.b));

    float minimumChannel =
        min(
            color.r,
            min(color.g, color.b));

    float currentSaturation =
        maximumChannel > 1e-5
        ? (maximumChannel - minimumChannel) /
            maximumChannel
        : 0.0;

    float vibrance =
        ColorVibrance *
        (1.0 - currentSaturation);

    color =
        lerp(
            Luminance(color).xxx,
            color,
            1.0 + vibrance);

    color =
        (color - 0.18) *
        max(ColorContrast, 0.0) +
        0.18;

    return max(color, 0.0);
}

float3 ApplyTone(float3 color)
{
    if (!HasEffect(EFFECT_TONE))
    {
        return color;
    }

    color = max(color, 0.0);

    if (ToneToe > 0.0)
    {
        color =
            pow(
                color,
                1.0 + ToneToe * 0.75);
    }

    if (ToneShoulder > 0.0)
    {
        color =
            color /
            (1.0 + color * ToneShoulder);
    }

    return min(
        color,
        max(ToneWhiteClip, 1.0));
}

float3 ApplySharpen(
    float2 uv,
    float3 color)
{
    if (SharpenEnabled == 0 ||
        SharpenStrength <= 0.0)
    {
        return color;
    }

    float3 north =
        SceneColor.SampleLevel(
            LinearSampler,
            saturate(uv + float2(0.0, -InvHeight)),
            0).rgb;

    float3 south =
        SceneColor.SampleLevel(
            LinearSampler,
            saturate(uv + float2(0.0, InvHeight)),
            0).rgb;

    float3 east =
        SceneColor.SampleLevel(
            LinearSampler,
            saturate(uv + float2(InvWidth, 0.0)),
            0).rgb;

    float3 west =
        SceneColor.SampleLevel(
            LinearSampler,
            saturate(uv + float2(-InvWidth, 0.0)),
            0).rgb;

    float3 low =
        (north + south + east + west) * 0.25;

    float3 high =
        clamp(
            color - low,
            -SharpenClamp,
            SharpenClamp);

    return max(
        color +
            high * SharpenStrength,
        0.0);
}

float4 PSComposite(VSOutput input) : SV_TARGET
{
    float4 original =
        SceneColor.SampleLevel(
            LinearSampler,
            input.uv,
            0);

    if (ProductEnabled == 0 ||
        (Mode == 1 && input.uv.x < 0.5))
    {
        return original;
    }

    float depth =
        SceneDepth.SampleLevel(
            PointSampler,
            DepthUv(input.uv),
            0).x;

    float3 color =
        DenoiseColor(
            input.uv,
            depth,
            original.rgb);

    color =
        MotionBlurColor(
            input.uv,
            color);

    color =
        DepthOfFieldColor(
            input.uv,
            depth,
            color);

    if (AmbientContactEnabled != 0)
    {
        float contact =
            AmbientContact(input.uv);

        color *=
            1.0 -
            contact *
            AmbientStrength;
    }

    if (DepthFogEnabled != 0 &&
        depth < 0.99999)
    {
        float distanceToCamera =
            abs(
                ReconstructView(
                    input.uv,
                    depth).z);

        float linearFog =
            saturate(
                (distanceToCamera - DepthFogStart) /
                max(
                    DepthFogEnd - DepthFogStart,
                    1.0));

        float exponentialFog =
            1.0 -
            exp(
                -linearFog *
)GZFX_HLSL" +
                R"GZFX_HLSL(                max(DepthFogDensity, 0.0));

        color =
            lerp(
                color,
                float3(
                    DepthFogColorR,
                    DepthFogColorG,
                    DepthFogColorB),
                saturate(exponentialFog));
    }

    color += BloomColor(input.uv);

    if (ExposureEnabled != 0)
    {
        float exposure =
            AutoExposureEnabled != 0
            ? PreviousExposure.SampleLevel(
                PointSampler,
                float2(0.5, 0.5),
                0).r
            : 1.0;

        exposure *= exp2(ManualExposureEv);
        color *= exposure;
    }

    color = ApplyColor(color);
    color = ApplyTone(color);

    if (VignetteEnabled != 0 &&
        VignetteStrength > 0.0)
    {
        float2 centered =
            input.uv * 2.0 - 1.0;

        centered.x *=
            InvHeight /
            max(InvWidth, 1e-6);

        float radius = length(centered);
        float vignette =
            smoothstep(
                VignetteRadius,
                VignetteRadius +
                    max(VignetteSoftness, 1e-3),
                radius);

        color *=
            1.0 -
            vignette *
            saturate(VignetteStrength);
    }

    if (ChromaticAberrationEnabled != 0 &&
        ChromaticAberrationStrength > 0.0)
    {
        float2 direction =
            input.uv - 0.5;

        float2 offset =
            direction *
            ChromaticAberrationStrength *
            float2(InvWidth, InvHeight);

        float3 originalCenter = original.rgb;

        color.r +=
            SceneColor.SampleLevel(
                LinearSampler,
                saturate(input.uv + offset),
                0).r -
            originalCenter.r;

        color.b +=
            SceneColor.SampleLevel(
                LinearSampler,
                saturate(input.uv - offset),
                0).b -
            originalCenter.b;
    }

    if (FilmGrainEnabled != 0 &&
        FilmGrainStrength > 0.0)
    {
        float grain =
            Hash12(
                floor(
                    input.uv /
                    float2(InvWidth, InvHeight) /
                    max(FilmGrainScale, 0.1)) +
                float2(
                    float(FrameIndex & 1023u),
                    float((FrameIndex * 17u) & 1023u))) -
            0.5;

        color += grain * FilmGrainStrength;
    }

    color =
        ApplySharpen(
            input.uv,
            color);

    if (BarsEnabled != 0)
    {
        if (input.uv.y < LetterboxAmount ||
            input.uv.y > 1.0 - LetterboxAmount ||
            input.uv.x < PillarboxAmount ||
            input.uv.x > 1.0 - PillarboxAmount)
        {
            color = 0.0;
        }
    }

    color =
        isfinite(color.r) &&
        isfinite(color.g) &&
        isfinite(color.b)
        ? max(color, 0.0)
        : max(original.rgb, 0.0);

    return float4(
        color,
        original.a);
}
)GZFX_HLSL";

            return source.c_str();
        }

        UINT ComputeMipCount(
            UINT width,
            UINT height)
        {
            UINT count = 1;

            while (width > 1 ||
                   height > 1)
            {
                width = std::max<UINT>(1, width / 2);
                height = std::max<UINT>(1, height / 2);
                ++count;
            }

            return count;
        }

        bool CreateTextureViews(
            ID3D11Device* device,
            const D3D11_TEXTURE2D_DESC& sceneDescription,
            std::string& error)
        {
            D3D11_TEXTURE2D_DESC sourceDescription =
                sceneDescription;

            sourceDescription.Usage =
                D3D11_USAGE_DEFAULT;
            sourceDescription.BindFlags =
                D3D11_BIND_SHADER_RESOURCE;
            sourceDescription.CPUAccessFlags = 0;
            sourceDescription.MiscFlags = 0;

            HRESULT result =
                device->CreateTexture2D(
                    &sourceDescription,
                    nullptr,
                    &g_runtime.sourceCopy);

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateShaderResourceView(
                        g_runtime.sourceCopy,
                        nullptr,
                        &g_runtime.sourceSrv);
            }

            D3D11_TEXTURE2D_DESC outputDescription =
                sceneDescription;

            outputDescription.Usage =
                D3D11_USAGE_DEFAULT;
            outputDescription.BindFlags =
                D3D11_BIND_RENDER_TARGET;
            outputDescription.CPUAccessFlags = 0;
            outputDescription.MiscFlags = 0;

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateTexture2D(
                        &outputDescription,
                        nullptr,
                        &g_runtime.output);
            }

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateRenderTargetView(
                        g_runtime.output,
                        nullptr,
                        &g_runtime.outputRtv);
            }

            g_runtime.pyramidWidth =
                std::max<UINT>(
                    1,
                    (sceneDescription.Width + 1) / 2);

            g_runtime.pyramidHeight =
                std::max<UINT>(
                    1,
                    (sceneDescription.Height + 1) / 2);

            g_runtime.pyramidMipCount =
                ComputeMipCount(
                    g_runtime.pyramidWidth,
                    g_runtime.pyramidHeight);

            D3D11_TEXTURE2D_DESC pyramidDescription{};
            pyramidDescription.Width =
                g_runtime.pyramidWidth;
            pyramidDescription.Height =
                g_runtime.pyramidHeight;
            pyramidDescription.MipLevels =
                g_runtime.pyramidMipCount;
            pyramidDescription.ArraySize = 1;
            pyramidDescription.Format =
                DXGI_FORMAT_R16G16B16A16_FLOAT;
            pyramidDescription.SampleDesc.Count = 1;
            pyramidDescription.Usage =
                D3D11_USAGE_DEFAULT;
            pyramidDescription.BindFlags =
                D3D11_BIND_RENDER_TARGET |
                D3D11_BIND_SHADER_RESOURCE;
            pyramidDescription.MiscFlags =
                D3D11_RESOURCE_MISC_GENERATE_MIPS;

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateTexture2D(
                        &pyramidDescription,
                        nullptr,
                        &g_runtime.pyramid);
            }

            if (SUCCEEDED(result))
            {
                D3D11_SHADER_RESOURCE_VIEW_DESC srvDescription{};
                srvDescription.Format =
                    pyramidDescription.Format;
                srvDescription.ViewDimension =
                    D3D11_SRV_DIMENSION_TEXTURE2D;
                srvDescription.Texture2D.MostDetailedMip = 0;
                srvDescription.Texture2D.MipLevels =
                    g_runtime.pyramidMipCount;

                result =
                    device->CreateShaderResourceView(
                        g_runtime.pyramid,
                        &srvDescription,
                        &g_runtime.pyramidSrv);
            }

            if (SUCCEEDED(result))
            {
                D3D11_RENDER_TARGET_VIEW_DESC rtvDescription{};
                rtvDescription.Format =
                    pyramidDescription.Format;
                rtvDescription.ViewDimension =
                    D3D11_RTV_DIMENSION_TEXTURE2D;
                rtvDescription.Texture2D.MipSlice = 0;

                result =
                    device->CreateRenderTargetView(
                        g_runtime.pyramid,
                        &rtvDescription,
                        &g_runtime.pyramidRtv);
            }

            D3D11_TEXTURE2D_DESC exposureDescription{};
            exposureDescription.Width = 1;
            exposureDescription.Height = 1;
            exposureDescription.MipLevels = 1;
            exposureDescription.ArraySize = 1;
            exposureDescription.Format =
                DXGI_FORMAT_R16G16B16A16_FLOAT;
            exposureDescription.SampleDesc.Count = 1;
            exposureDescription.Usage =
                D3D11_USAGE_DEFAULT;
            exposureDescription.BindFlags =
                D3D11_BIND_RENDER_TARGET |
                D3D11_BIND_SHADER_RESOURCE;

            for (std::uint32_t index = 0;
                 index < 2 &&
                 SUCCEEDED(result);
                 ++index)
            {
                result =
                    device->CreateTexture2D(
                        &exposureDescription,
                        nullptr,
                        &g_runtime.exposure[index]);

                if (SUCCEEDED(result))
                {
                    result =
                        device->CreateShaderResourceView(
                            g_runtime.exposure[index],
                            nullptr,
                            &g_runtime.exposureSrv[index]);
                }

                if (SUCCEEDED(result))
                {
                    result =
                        device->CreateRenderTargetView(
                            g_runtime.exposure[index],
                            nullptr,
                            &g_runtime.exposureRtv[index]);
                }
            }

            if (FAILED(result))
            {
                std::ostringstream stream;
                stream
                    << "texture/view allocation HRESULT=0x"
                    << std::hex
                    << std::uppercase
                    << static_cast<std::uint32_t>(result);

                error = stream.str();
                return false;
            }

            return true;
        }

        bool EnsureRuntime(
            ID3D11Device* device,
            const D3D11_TEXTURE2D_DESC& sceneDescription,
            std::string& error)
        {
            if (device == nullptr)
            {
                error = "device unavailable";
                return false;
            }

            if (g_runtime.device == device &&
                g_runtime.width ==
                    sceneDescription.Width &&
                g_runtime.height ==
                    sceneDescription.Height &&
                g_runtime.format ==
                    sceneDescription.Format)
            {
                return true;
            }

            ReleaseRuntime();

            if (sceneDescription.Width == 0 ||
                sceneDescription.Height == 0 ||
                sceneDescription.SampleDesc.Count != 1 ||
                sceneDescription.ArraySize != 1)
            {
                error =
                    "unsupported scene size/sample/array";
                return false;
            }

            g_runtime.device = device;
            g_runtime.device->AddRef();

            HRESULT result =
                device->CreateDeferredContext(
                    0,
                    &g_runtime.deferred);

            ID3DBlob* vertexBlob = nullptr;
            ID3DBlob* luminanceBlob = nullptr;
            ID3DBlob* exposureBlob = nullptr;
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
                    "PSLuminanceBloom",
                    "ps_5_0",
                    &luminanceBlob,
                    error))
            {
                result = E_FAIL;
            }

            if (SUCCEEDED(result) &&
                !Compile(
                    ShaderSource(),
                    "PSExposure",
                    "ps_5_0",
                    &exposureBlob,
                    error))
            {
                result = E_FAIL;
            }

            if (SUCCEEDED(result) &&
                !Compile(
                    ShaderSource(),
                    "PSComposite",
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
                        luminanceBlob->GetBufferPointer(),
                        luminanceBlob->GetBufferSize(),
                        nullptr,
                        &g_runtime.luminanceShader);
            }

            if (SUCCEEDED(result))
            {
                result =
                    device->CreatePixelShader(
                        exposureBlob->GetBufferPointer(),
                        exposureBlob->GetBufferSize(),
                        nullptr,
                        &g_runtime.exposureShader);
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

            Release(vertexBlob);
            Release(luminanceBlob);
            Release(exposureBlob);
            Release(compositeBlob);

            D3D11_SAMPLER_DESC linearDescription{};
            linearDescription.Filter =
                D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            linearDescription.AddressU =
                D3D11_TEXTURE_ADDRESS_CLAMP;
            linearDescription.AddressV =
                D3D11_TEXTURE_ADDRESS_CLAMP;
            linearDescription.AddressW =
                D3D11_TEXTURE_ADDRESS_CLAMP;
            linearDescription.MaxLOD =
                D3D11_FLOAT32_MAX;

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateSamplerState(
                        &linearDescription,
                        &g_runtime.linearSampler);
            }

            D3D11_SAMPLER_DESC pointDescription =
                linearDescription;

            pointDescription.Filter =
                D3D11_FILTER_MIN_MAG_MIP_POINT;

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateSamplerState(
                        &pointDescription,
                        &g_runtime.pointSampler);
            }

            D3D11_BUFFER_DESC parameterDescription{};
            parameterDescription.ByteWidth =
                sizeof(Parameters);
            parameterDescription.Usage =
                D3D11_USAGE_DEFAULT;
            parameterDescription.BindFlags =
                D3D11_BIND_CONSTANT_BUFFER;

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateBuffer(
                        &parameterDescription,
                        nullptr,
                        &g_runtime.parameters);
            }

            if (SUCCEEDED(result) &&
                !CreateTextureViews(
                    device,
                    sceneDescription,
                    error))
            {
                result = E_FAIL;
            }

            if (FAILED(result))
            {
                if (error.empty())
                {
                    std::ostringstream stream;
                    stream
                        << "runtime allocation HRESULT=0x"
                        << std::hex
                        << std::uppercase
                        << static_cast<std::uint32_t>(result);

                    error = stream.str();
                }

                ReleaseRuntime();
                return false;
            }

            g_runtime.width =
                sceneDescription.Width;
            g_runtime.height =
                sceneDescription.Height;
            g_runtime.format =
                sceneDescription.Format;
            g_runtime.exposureIndex = 0;
            g_runtime.exposureHistoryValid = false;
            ++g_runtime.generation;

            g_captureRemaining = 3;
            return true;
        }

        ID3D11ShaderResourceView* CreateDepthSrv(
            ID3D11Device* device,
            ID3D11Texture2D* texture,
            const D3D11_TEXTURE2D_DESC& description)
        {
            if (device == nullptr ||
                texture == nullptr)
            {
                return nullptr;
            }

            const DXGI_FORMAT format =
                DepthSrvFormat(
                    description.Format);

            if (format == DXGI_FORMAT_UNKNOWN)
            {
                return nullptr;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
            viewDescription.Format = format;
            viewDescription.ViewDimension =
                D3D11_SRV_DIMENSION_TEXTURE2D;
            viewDescription.Texture2D.MipLevels = 1;

            ID3D11ShaderResourceView* view = nullptr;

            if (FAILED(
                    device->CreateShaderResourceView(
                        texture,
                        &viewDescription,
                        &view)))
            {
                Release(view);
            }

            return view;
        }

        ID3D11ShaderResourceView* CreateOptionalSrv(
            ID3D11Device* device,
            ID3D11Texture2D* texture)
        {
            if (device == nullptr ||
                texture == nullptr)
            {
                return nullptr;
            }

            ID3D11ShaderResourceView* view = nullptr;

            if (FAILED(
                    device->CreateShaderResourceView(
                        texture,
                        nullptr,
                        &view)))
            {
                Release(view);
            }

            return view;
        }

        Parameters BuildParameters(
            const settings::Values& values,
            const std::uint64_t frame,
            const std::uint32_t mode,
            const bool motionAvailable)
        {
            const auto budget =
                unified_product::Budget(
                    values.productQuality);

            const std::uint64_t mask =
                unified_product::BuildEffectMask(
                    values);

            Parameters parameters{};
            parameters.invWidth =
                1.0f /
                static_cast<float>(
                    g_runtime.width);
            parameters.invHeight =
                1.0f /
                static_cast<float>(
                    g_runtime.height);
            parameters.pyramidInvWidth =
                1.0f /
                static_cast<float>(
                    g_runtime.pyramidWidth);
            parameters.pyramidInvHeight =
                1.0f /
                static_cast<float>(
                    g_runtime.pyramidHeight);

            parameters.effectMask =
                static_cast<std::uint32_t>(
                    mask & 0xffffffffull);
            parameters.quality =
                values.productQuality;
            parameters.mode = mode;
            parameters.frameIndex =
                static_cast<std::uint32_t>(frame);
            parameters.bloomLevels =
                std::min<std::uint32_t>(
                    budget.bloomLevels,
                    g_runtime.pyramidMipCount);
            parameters.dofSamples =
                budget.dofSamples;
            parameters.motionSamples =
                budget.motionSamples;
            parameters.denoiseSamples =
                budget.denoiseSamples;

            parameters.manualExposureEv =
                values.manualExposureEv;
            parameters.exposureCompensationEv =
                values.exposureCompensationEv;
            parameters.exposureKey =
                values.exposureKey;
            parameters.exposureMinEv =
                values.exposureMinEv;
            parameters.exposureMaxEv =
                values.exposureMaxEv;
            parameters.exposureSpeedUp =
                values.exposureSpeedUp;
            parameters.exposureSpeedDown =
                values.exposureSpeedDown;

            parameters.bloomStrength =
                values.bloomStrength;
            parameters.bloomThreshold =
                values.bloomEnabled &&
                    values.localGlowEnabled
                ? std::min(
                    values.bloomThreshold,
                    values.localGlowThreshold)
                : (values.bloomEnabled
                    ? values.bloomThreshold
                    : values.localGlowThreshold);
            parameters.bloomSoftKnee =
                values.bloomSoftKnee;
            parameters.bloomRadius =
                std::max(
                    values.bloomRadius,
                    values.localGlowRadiusPixels / 8.0f);
            parameters.lensDirtStrength =
                values.lensDirtStrength;

            parameters.ambientStrength =
                values.ambientContactStrength;
            parameters.ambientRadius =
                values.ambientContactRadiusPixels;
            parameters.localGlowStrength =
                values.localGlowStrength;
            parameters.localGlowThreshold =
                values.localGlowThreshold;
            parameters.localGlowRadius =
                values.localGlowRadiusPixels;

            parameters.colorContrast =
                values.colorContrast;
            parameters.colorGamma =
                values.colorGamma;
            parameters.colorSaturation =
                values.colorSaturation;
            parameters.colorVibrance =
                values.colorVibrance;
            parameters.colorTemperature =
                values.colorTemperature;
            parameters.colorTint =
                values.colorTint;
            parameters.colorLift =
                values.colorLift;
            parameters.colorGain =
                values.colorGain;
            parameters.toneToe =
                values.toneToe;
            parameters.toneShoulder =
                values.toneShoulder;
            parameters.toneWhiteClip =
                values.toneWhiteClip;

            parameters.dofFocusDistance =
                values.dofFocusDistance;
            parameters.dofFocusRange =
                values.dofFocusRange;
            parameters.dofNearStrength =
                values.dofNearStrength;
            parameters.dofFarStrength =
                values.dofFarStrength;
            parameters.dofMaxRadius =
                values.dofMaxRadius;

            parameters.motionBlurStrength =
                values.motionBlurStrength;
            parameters.motionBlurMaxPixels =
                values.motionBlurMaxPixels;

            parameters.depthFogStart =
                values.depthFogStart;
            parameters.depthFogEnd =
                values.depthFogEnd;
            parameters.depthFogDensity =
                values.depthFogDensity;
            parameters.depthFogColorR =
                values.depthFogColorR;
            parameters.depthFogColorG =
                values.depthFogColorG;
            parameters.depthFogColorB =
                values.depthFogColorB;

            parameters.vignetteStrength =
                values.vignetteStrength;
            parameters.vignetteRadius =
                values.vignetteRadius;
            parameters.vignetteSoftness =
                values.vignetteSoftness;
            parameters.filmGrainStrength =
                values.filmGrainStrength;
            parameters.filmGrainScale =
                values.filmGrainScale;
            parameters.chromaticAberrationStrength =
                values.chromaticAberrationStrength;
            parameters.denoiseStrength =
                values.denoiseStrength;
            parameters.sharpenStrength =
                values.sharpenStrength;
            parameters.sharpenClamp =
                values.sharpenClamp;
            parameters.letterboxAmount =
                values.letterboxAmount;
            parameters.pillarboxAmount =
                values.pillarboxAmount;

            parameters.productEnabled =
                values.productEnabled ? 1u : 0u;
            parameters.exposureEnabled =
                values.exposureEnabled ? 1u : 0u;
            parameters.autoExposureEnabled =
                values.autoExposureEnabled ? 1u : 0u;
            parameters.bloomEnabled =
                values.bloomEnabled ? 1u : 0u;
            parameters.lensDirtEnabled =
                values.lensDirtEnabled ? 1u : 0u;
            parameters.colorEnabled =
                values.colorEnabled ? 1u : 0u;
            parameters.dofEnabled =
                values.dofEnabled ? 1u : 0u;
            parameters.motionBlurEnabled =
                values.motionBlurEnabled ? 1u : 0u;
            parameters.depthFogEnabled =
                values.depthFogEnabled ? 1u : 0u;
            parameters.vignetteEnabled =
                values.vignetteEnabled ? 1u : 0u;
            parameters.filmGrainEnabled =
                values.filmGrainEnabled ? 1u : 0u;
            parameters.chromaticAberrationEnabled =
                values.chromaticAberrationEnabled ? 1u : 0u;
            parameters.denoiseEnabled =
                values.denoiseEnabled ? 1u : 0u;
            parameters.sharpenEnabled =
                values.sharpenEnabled ? 1u : 0u;
            parameters.barsEnabled =
                values.barsEnabled ? 1u : 0u;
            parameters.localGlowEnabled =
                values.localGlowEnabled ? 1u : 0u;
            parameters.ambientContactEnabled =
                values.ambientContactStrength > 0.0f
                ? 1u
                : 0u;
            parameters.exposureHistoryValid =
                g_runtime.exposureHistoryValid
                ? 1u
                : 0u;
            parameters.pyramidMipCount =
                g_runtime.pyramidMipCount;
            parameters.motionAvailable =
                motionAvailable ? 1u : 0u;

            return parameters;
        }

        std::string ActiveEffectNames(
            const std::uint64_t mask)
        {
            struct Entry
            {
                unified_product::Effect effect;
                const char* name;
            };

            static constexpr std::array<Entry, 19> entries
            {{
                {unified_product::Effect::ExposureManual, "exposure_manual"},
                {unified_product::Effect::ExposureAuto, "exposure_auto"},
                {unified_product::Effect::Bloom, "bloom"},
                {unified_product::Effect::LensDirt, "lens_dirt"},
                {unified_product::Effect::Color, "color"},
                {unified_product::Effect::ToneShaping, "tone_shaping"},
                {unified_product::Effect::DepthOfField, "dof"},
                {unified_product::Effect::MotionBlur, "motion_blur"},
                {unified_product::Effect::DepthFog, "depth_fog"},
                {unified_product::Effect::Vignette, "vignette"},
                {unified_product::Effect::FilmGrain, "film_grain"},
                {unified_product::Effect::ChromaticAberration, "chromatic_aberration"},
                {unified_product::Effect::Denoise, "denoise"},
                {unified_product::Effect::Sharpen, "sharpen"},
                {unified_product::Effect::Bars, "bars"},
                {unified_product::Effect::AmbientContact, "ambient_contact"},
                {unified_product::Effect::HorizonOcclusion, "horizon_occlusion"},
                {unified_product::Effect::IndirectBounce, "indirect_bounce"},
                {unified_product::Effect::LocalGlow, "local_glow"}
            }};

            std::ostringstream stream;
            bool first = true;

            for (const auto& entry : entries)
            {
                if ((mask & unified_product::Bit(
                        entry.effect)) == 0)
                {
                    continue;
                }

                if (!first)
                {
                    stream << ',';
                }

                stream << entry.name;
                first = false;
            }

            return first
                ? "none"
                : stream.str();
        }

        void EnsureDiagnosticsRoot(
            fs::path& root)
        {
            root =
                gzfx::paths::DiagnosticsRoot();

            if (root.empty())
            {
                return;
            }

            std::error_code error;
            fs::create_directories(root, error);

            if (error)
            {
                root.clear();
            }
        }

        void WriteGraph()
        {
            if (g_runtime.generation == 0 ||
                g_lastGraphGeneration ==
                    g_runtime.generation)
            {
                return;
            }

            fs::path root;
            EnsureDiagnosticsRoot(root);

            if (root.empty())
            {
                return;
            }

            std::ofstream out(
                root /
                    L"unified_renderer_graph.csv",
                std::ios::out |
                    std::ios::trunc);

            if (!out)
            {
                return;
            }

            out
                << "stage,input,output,temporal,quality_dependency\n"
                << "source_snapshot,MAIN_HDR,source_copy,false,false\n"
                << "luminance_bloom,source_copy,luminance_bloom_pyramid_mip0,false,true\n"
                << "pyramid_mips,luminance_bloom_pyramid_mip0,luminance_bloom_pyramid,false,true\n"
                << "exposure_history,luminance_bloom_pyramid+previous_exposure,current_exposure,true,false\n"
                << "product_composite,source_copy+depth+motion+pyramid+exposure,product_output,true,true\n"
                << "scene_writeback,product_output,MAIN_HDR,false,false\n";

            g_lastGraphGeneration =
                g_runtime.generation;
        }

        void WriteStatus(
            const std::uint64_t frame,
            const char* state,
            const settings::Values& values,
            const bool ambientDepthApplied,
            const bool ambientVisibilityApplied)
        {
            const std::uint64_t revision =
                settings::Revision();

            if (frame != 0 &&
                g_lastStatusWriteFrame != 0 &&
                frame > g_lastStatusWriteFrame &&
                frame - g_lastStatusWriteFrame < 120 &&
                revision == g_status.settingsRevision &&
                state == std::string("applied"))
            {
                return;
            }

            fs::path root;
            EnsureDiagnosticsRoot(root);

            if (root.empty())
            {
                return;
            }

            std::ofstream out(
                root /
                    L"unified_renderer_status.txt",
                std::ios::out |
                    std::ios::trunc);

            if (!out)
            {
                return;
            }

            out
                << "GZFX unified product renderer\n"
                << "version=" << gzfx::kVersion << "\n"
                << "status=" << state << "\n"
                << "frame=" << frame << "\n"
                << "applications=" << g_status.applications << "\n"
                << "skips=" << g_status.skips << "\n"
                << "failures=" << g_status.failures << "\n"
                << "settings_revision=" << revision << "\n"
                << "active_effect_mask=" << g_status.activeEffectMask << "\n"
                << "active_effect_names="
                << ActiveEffectNames(
                    g_status.activeEffectMask)
                << "\n"
                << "product_enabled="
                << (values.productEnabled ? "true" : "false")
                << "\n"
                << "quality=" << values.productQuality << "\n"
                << "preset="
                << settings::PresetName(
                    static_cast<settings::Preset>(
                        values.productPreset))
                << "\n"
                << "width=" << g_runtime.width << "\n"
                << "height=" << g_runtime.height << "\n"
                << "format="
                << static_cast<std::uint32_t>(
                    g_runtime.format)
                << "\n"
                << "pyramid_width="
                << g_runtime.pyramidWidth << "\n"
                << "pyramid_height="
                << g_runtime.pyramidHeight << "\n"
                << "pyramid_mips="
                << g_runtime.pyramidMipCount << "\n"
                << "history_valid="
                << (g_runtime.exposureHistoryValid
                    ? "true"
                    : "false")
                << "\n"
                << "history_index="
                << g_runtime.exposureIndex << "\n"
                << "ambient_depth_applied="
                << (ambientDepthApplied
                    ? "true"
                    : "false")
                << "\n"
                << "ambient_visibility_applied="
                << (ambientVisibilityApplied
                    ? "true"
                    : "false")
                << "\n"
                << "source_copy_count=1\n"
                << "scene_writeback_count=1\n"
                << "gpu_timing_valid=false\n"
                << "timing_source=cpu_submission_wall_clock\n"
                << "last_error=" << g_lastError << "\n";

            g_lastStatusWriteFrame = frame;
        }

        void FlushTimings(
            const std::uint64_t frame)
        {
            if (g_timings.empty() ||
                (g_lastTimingFlushFrame != 0 &&
                 frame > g_lastTimingFlushFrame &&
                 frame - g_lastTimingFlushFrame < 120))
            {
                return;
            }

            fs::path root;
            EnsureDiagnosticsRoot(root);

            if (root.empty())
            {
                return;
            }

            std::ofstream out(
                root /
                    L"unified_renderer_timings.csv",
                std::ios::out |
                    std::ios::trunc);

            if (!out)
            {
                return;
            }

            out
                << "frame,cpu_submit_ms,effect_mask,quality\n";

            out << std::fixed << std::setprecision(6);

            for (const auto& row : g_timings)
            {
                out
                    << row.frame << ','
                    << row.submitMilliseconds << ','
                    << row.effectMask << ','
                    << row.quality << '\n';
            }

            g_lastTimingFlushFrame = frame;
        }

        void WriteCapture(
            const std::uint64_t frame,
            const std::uint64_t drawOrdinal,
            const settings::Values& values,
            const bool motionAvailable)
        {
            if (g_manualCaptureRequested)
            {
                g_captureRemaining = 3;
                g_manualCaptureRequested = false;
            }

            if (g_captureRemaining == 0)
            {
                return;
            }

            fs::path root;
            EnsureDiagnosticsRoot(root);

            if (root.empty())
            {
                return;
            }

            const fs::path path =
                root /
                    L"unified_renderer_capture.txt";

            const bool first =
                g_captureRemaining == 3;

            std::ofstream out(
                path,
                std::ios::out |
                    (first
                        ? std::ios::trunc
                        : std::ios::app));

            if (!out)
            {
                return;
            }

            out
                << "frame=" << frame << "\n"
                << "draw_ordinal=" << drawOrdinal << "\n"
                << "settings_revision="
                << settings::Revision() << "\n"
                << "effect_mask="
                << unified_product::BuildEffectMask(
                    values)
                << "\n"
                << "quality="
                << values.productQuality << "\n"
                << "motion_available="
                << (motionAvailable
                    ? "true"
                    : "false")
                << "\n"
                << "stages=source_snapshot>luminance_bloom>pyramid_mips>exposure_history>product_composite>scene_writeback\n"
                << "source_copy_count=1\n"
                << "scene_writeback_count=1\n\n";

            --g_captureRemaining;
        }

        void BindCommonState(
            ID3D11DeviceContext* context)
        {
            context->IASetInputLayout(nullptr);
            context->IASetPrimitiveTopology(
                D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->VSSetShader(
                g_runtime.vertexShader,
                nullptr,
                0);
            context->PSSetConstantBuffers(
                0,
                1,
                &g_runtime.parameters);

            ID3D11SamplerState* samplers[2]
            {
                g_runtime.linearSampler,
                g_runtime.pointSampler
            };

            context->PSSetSamplers(
                0,
                2,
                samplers);
        }

        void UnbindResources(
            ID3D11DeviceContext* context)
        {
            ID3D11ShaderResourceView* nullViews[5]
            {
                nullptr,
                nullptr,
                nullptr,
                nullptr,
                nullptr
            };

            context->PSSetShaderResources(
                0,
                5,
                nullViews);
            context->OMSetRenderTargets(
                0,
                nullptr,
                nullptr);
        }
    }

    bool Apply(
        const ApplyInputs& inputs) noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);

            const auto started =
                std::chrono::steady_clock::now();

            const settings::Values values =
                settings::Get();

            const std::uint32_t mode =
                gzfx::render::state::
                    GetDevelopmentMode();

            if (!values.productEnabled)
            {
                ++g_status.skips;
                g_status.lastFrame = inputs.frame;
                g_status.settingsRevision =
                    settings::Revision();
                g_status.activeEffectMask = 0;
                g_lastError.clear();

                WriteStatus(
                    inputs.frame,
                    "disabled_product",
                    values,
                    inputs.ambientDepthApplied,
                    inputs.ambientVisibilityApplied);

                return false;
            }

            if (mode != 1 &&
                mode != 2)
            {
                ++g_status.skips;
                g_lastError = "unsupported development mode";

                WriteStatus(
                    inputs.frame,
                    "skipped_mode",
                    values,
                    inputs.ambientDepthApplied,
                    inputs.ambientVisibilityApplied);

                return false;
            }

            if (inputs.device == nullptr ||
                inputs.context == nullptr ||
                inputs.live == nullptr ||
                inputs.live->sceneColorTexture == nullptr ||
                inputs.live->depthTexture == nullptr ||
                inputs.live->cameraBuffer == nullptr)
            {
                ++g_status.skips;
                g_lastError = "missing device/context/scene/depth/camera";

                WriteStatus(
                    inputs.frame,
                    "skipped_inputs",
                    values,
                    inputs.ambientDepthApplied,
                    inputs.ambientVisibilityApplied);

                return false;
            }

            D3D11_BUFFER_DESC cameraDescription{};
            inputs.live->cameraBuffer->GetDesc(
                &cameraDescription);

            if (cameraDescription.ByteWidth != 720)
            {
                ++g_status.skips;
                g_lastError = "camera constant buffer byte width != 720";

                WriteStatus(
                    inputs.frame,
                    "skipped_camera",
                    values,
                    inputs.ambientDepthApplied,
                    inputs.ambientVisibilityApplied);

                return false;
            }

            if (!EnsureRuntime(
                    inputs.device,
                    inputs.live->sceneColorDesc,
                    g_lastError))
            {
                ++g_status.failures;

                WriteStatus(
                    inputs.frame,
                    "failed_runtime",
                    values,
                    inputs.ambientDepthApplied,
                    inputs.ambientVisibilityApplied);

                return false;
            }

            ID3D11ShaderResourceView* depthSrv =
                CreateDepthSrv(
                    inputs.device,
                    inputs.live->depthTexture,
                    inputs.live->depthDesc);

            if (depthSrv == nullptr)
            {
                ++g_status.failures;
                g_lastError = "depth SRV creation failed";

                WriteStatus(
                    inputs.frame,
                    "failed_depth_srv",
                    values,
                    inputs.ambientDepthApplied,
                    inputs.ambientVisibilityApplied);

                return false;
            }

            ID3D11ShaderResourceView* motionSrv =
                CreateOptionalSrv(
                    inputs.device,
                    inputs.live->motionVectorTexture);

            const bool motionAvailable =
                motionSrv != nullptr;

            const Parameters parameters =
                BuildParameters(
                    values,
                    inputs.frame,
                    mode,
                    motionAvailable);

            ID3D11DeviceContext* deferred =
                g_runtime.deferred;

            deferred->ClearState();
            deferred->OMSetRenderTargets(
                0,
                nullptr,
                nullptr);
            deferred->CopyResource(
                g_runtime.sourceCopy,
                inputs.live->sceneColorTexture);
            deferred->UpdateSubresource(
                g_runtime.parameters,
                0,
                nullptr,
                &parameters,
                0,
                0);

            BindCommonState(deferred);

            D3D11_VIEWPORT pyramidViewport{};
            pyramidViewport.Width =
                static_cast<float>(
                    g_runtime.pyramidWidth);
            pyramidViewport.Height =
                static_cast<float>(
                    g_runtime.pyramidHeight);
            pyramidViewport.MinDepth = 0.0f;
            pyramidViewport.MaxDepth = 1.0f;

            deferred->OMSetRenderTargets(
                1,
                &g_runtime.pyramidRtv,
                nullptr);
            deferred->RSSetViewports(
                1,
                &pyramidViewport);
            deferred->PSSetShader(
                g_runtime.luminanceShader,
                nullptr,
                0);
            deferred->PSSetShaderResources(
                0,
                1,
                &g_runtime.sourceSrv);
            deferred->Draw(3, 0);
            UnbindResources(deferred);
            deferred->GenerateMips(
                g_runtime.pyramidSrv);

            const std::uint32_t previousIndex =
                g_runtime.exposureIndex;
            const std::uint32_t nextIndex =
                previousIndex ^ 1u;

            D3D11_VIEWPORT exposureViewport{};
            exposureViewport.Width = 1.0f;
            exposureViewport.Height = 1.0f;
            exposureViewport.MinDepth = 0.0f;
            exposureViewport.MaxDepth = 1.0f;

            ID3D11ShaderResourceView* exposureResources[5]
            {
                nullptr,
                nullptr,
                nullptr,
                g_runtime.pyramidSrv,
                g_runtime.exposureSrv[previousIndex]
            };

            deferred->OMSetRenderTargets(
                1,
                &g_runtime.exposureRtv[nextIndex],
                nullptr);
            deferred->RSSetViewports(
                1,
                &exposureViewport);
            deferred->PSSetShader(
                g_runtime.exposureShader,
                nullptr,
                0);
            deferred->PSSetShaderResources(
                0,
                5,
                exposureResources);
            deferred->Draw(3, 0);
            UnbindResources(deferred);

            D3D11_VIEWPORT sceneViewport{};
            sceneViewport.Width =
                static_cast<float>(
                    g_runtime.width);
            sceneViewport.Height =
                static_cast<float>(
                    g_runtime.height);
            sceneViewport.MinDepth = 0.0f;
            sceneViewport.MaxDepth = 1.0f;

            ID3D11ShaderResourceView* compositeResources[5]
            {
                g_runtime.sourceSrv,
                depthSrv,
                motionSrv,
                g_runtime.pyramidSrv,
                g_runtime.exposureSrv[nextIndex]
            };

            deferred->OMSetRenderTargets(
                1,
                &g_runtime.outputRtv,
                nullptr);
            deferred->RSSetViewports(
                1,
                &sceneViewport);
            deferred->PSSetShader(
                g_runtime.compositeShader,
                nullptr,
                0);
            deferred->PSSetConstantBuffers(
                12,
                1,
                &inputs.live->cameraBuffer);
            deferred->PSSetShaderResources(
                0,
                5,
                compositeResources);
            deferred->Draw(3, 0);
            UnbindResources(deferred);
            deferred->CopyResource(
                inputs.live->sceneColorTexture,
                g_runtime.output);

            ID3D11CommandList* commandList = nullptr;

            const HRESULT finishResult =
                deferred->FinishCommandList(
                    FALSE,
                    &commandList);

            if (SUCCEEDED(finishResult) &&
                commandList != nullptr)
            {
                inputs.context->ExecuteCommandList(
                    commandList,
                    TRUE);
            }

            Release(commandList);
            Release(motionSrv);
            Release(depthSrv);

            if (FAILED(finishResult))
            {
                ++g_status.failures;

                std::ostringstream stream;
                stream
                    << "FinishCommandList HRESULT=0x"
                    << std::hex
                    << std::uppercase
                    << static_cast<std::uint32_t>(
                        finishResult);

                g_lastError = stream.str();

                WriteStatus(
                    inputs.frame,
                    "failed_execute",
                    values,
                    inputs.ambientDepthApplied,
                    inputs.ambientVisibilityApplied);

                return false;
            }

            g_runtime.exposureIndex = nextIndex;
            g_runtime.exposureHistoryValid = true;

            ++g_status.applications;
            g_status.lastFrame = inputs.frame;
            g_status.settingsRevision =
                settings::Revision();
            g_status.activeEffectMask =
                unified_product::BuildEffectMask(
                    values);
            g_status.width = g_runtime.width;
            g_status.height = g_runtime.height;
            g_status.format = g_runtime.format;
            g_status.exposure =
                values.exposureEnabled
                ? std::exp2(
                    values.manualExposureEv)
                : 1.0f;
            g_status.historyValid = true;
            g_status.sourceCopyCountOne = true;
            g_status.sceneWritebackCountOne = true;
            g_lastError.clear();

            const auto finished =
                std::chrono::steady_clock::now();

            const double milliseconds =
                std::chrono::duration<
                    double,
                    std::milli>(
                        finished - started)
                    .count();

            g_timings.push_back(
                TimingRow{
                    inputs.frame,
                    milliseconds,
                    g_status.activeEffectMask,
                    values.productQuality});

            while (g_timings.size() > 512)
            {
                g_timings.pop_front();
            }

            WriteGraph();
            WriteCapture(
                inputs.frame,
                inputs.drawOrdinal,
                values,
                motionAvailable);
            WriteStatus(
                inputs.frame,
                "applied",
                values,
                inputs.ambientDepthApplied,
                inputs.ambientVisibilityApplied);
            FlushTimings(inputs.frame);

            return true;
        }
        catch (...)
        {
            try
            {
                std::scoped_lock lock(g_mutex);
                ++g_status.failures;
                g_lastError = "unhandled exception";
            }
            catch (...)
            {
            }

            return false;
        }
    }

    bool WasAppliedFrame(
        const std::uint64_t frame) noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);
            return g_status.lastFrame == frame &&
                g_status.applications != 0;
        }
        catch (...)
        {
            return false;
        }
    }

    Status GetStatus() noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);
            return g_status;
        }
        catch (...)
        {
            return {};
        }
    }

    void RequestManualCapture() noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);
            g_manualCaptureRequested = true;
        }
        catch (...)
        {
        }
    }

    void Reset() noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);
            ReleaseRuntime();
            g_status = {};
            g_lastError.clear();
            g_timings.clear();
            g_lastStatusWriteFrame = 0;
            g_lastTimingFlushFrame = 0;
            g_lastGraphGeneration = 0;
            g_captureRemaining = 0;
            g_manualCaptureRequested = false;
        }
        catch (...)
        {
        }
    }
}
