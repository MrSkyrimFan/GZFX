#include "gzfx/render/scene_effects.hpp"

#include "gzfx/paths.hpp"
#include "gzfx/render/ambient_depth_stack.hpp"
#include "gzfx/render/runtime_state.hpp"
#include "gzfx/render/ambient_visibility_v2.hpp"
#include "gzfx/render/product_control_mapping.hpp"
#include "gzfx/settings.hpp"
#include "gzfx/version.hpp"

#include <Windows.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

namespace gzfx::render::scene_effects
{
    namespace fs = std::filesystem;

    namespace
    {
        struct Parameters
        {
            float invWidth = 0.0f;
            float invHeight = 0.0f;
            float ambientStrength = 0.62f;
            float ambientRadius = 4.0f;

            float glowStrength = 0.18f;
            float glowThreshold = 1.10f;
            float glowRadius = 8.0f;
            std::uint32_t glowEnabled = 1;

            std::uint32_t mode = 1;
            float padding[3]{};
        };

        struct Runtime
        {
            ID3D11Device* device = nullptr;
            ID3D11DeviceContext* deferred = nullptr;
            ID3D11VertexShader* vertexShader = nullptr;
            ID3D11PixelShader* pixelShader = nullptr;
            ID3D11SamplerState* linearSampler = nullptr;
            ID3D11SamplerState* pointSampler = nullptr;
            ID3D11Buffer* parameters = nullptr;

            ID3D11Texture2D* sourceCopy = nullptr;
            ID3D11ShaderResourceView* sourceSrv = nullptr;

            ID3D11Texture2D* output = nullptr;
            ID3D11RenderTargetView* outputRtv = nullptr;

            UINT width = 0;
            UINT height = 0;
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        };

        std::mutex g_mutex;
        Runtime g_runtime{};

        ID3D11Buffer* g_validatedCamera = nullptr;
        bool g_cameraValidated = false;

        std::uint64_t g_injectionCount = 0;
        std::uint64_t g_lastFrame = 0;
        std::uint64_t g_lastDraw = 0;
        std::uint64_t g_lastControlProofFrame = 0;

        void WriteLog(
            const std::uint64_t frame,
            const std::uint64_t drawOrdinal,
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
            fs::create_directories(root, error);

            if (error)
            {
                return;
            }

            std::ofstream out(
                root / L"pre_ui_effects.txt",
                std::ios::out | std::ios::trunc);

            if (!out)
            {
                return;
            }

            const auto& settings =
                gzfx::settings::Get();

            out
                << "GZFX pre-UI effect stack\n"
                << "version=" << gzfx::kVersion << "\n"
                << "status=" << status << "\n"
                << "frame=" << frame << "\n"
                << "draw_ordinal=" << drawOrdinal << "\n"
                << "injections=" << g_injectionCount << "\n"
                << "mode="
                << gzfx::render::state::GetDevelopmentMode()
                << "\n"
                << "ambient_contact_strength="
                << settings.ambientContactStrength << "\n"
                << "ambient_contact_radius="
                << settings.ambientContactRadiusPixels << "\n"
                << "local_glow_enabled="
                << (settings.localGlowEnabled ? "true" : "false")
                << "\n"
                << "local_glow_strength="
                << settings.localGlowStrength << "\n"
                << "local_glow_threshold="
                << settings.localGlowThreshold << "\n"
                << "local_glow_radius="
                << settings.localGlowRadiusPixels << "\n";

            if (!detail.empty())
            {
                out << "detail=" << detail << "\n";
            }
        }

        void WriteProductControlSubmission(
            const std::uint64_t frame,
            const bool wave4OwnsAo,
            const bool indirectApplied)
        {
            if (g_lastControlProofFrame != 0 &&
                frame > g_lastControlProofFrame &&
                frame - g_lastControlProofFrame < 120)
            {
                return;
            }

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

            const auto quality =
                gzfx::render::product_control::
                    HorizonBudget(
                        settings.
                            horizonOcclusionQuality);

            std::ofstream out(
                root /
                    L"product_control_submission.txt",
                std::ios::out |
                    std::ios::trunc);

            if (!out)
            {
                return;
            }

            out
                << "GZFX product control submission\n"
                << "version="
                << gzfx::kVersion
                << "\n"
                << "frame="
                << frame
                << "\n"
                << "mode="
                << gzfx::render::state::
                    GetDevelopmentMode()
                << "\n"
                << "ambient_contact_strength="
                << settings.ambientContactStrength
                << "\n"
                << "ambient_contact_radius="
                << settings.ambientContactRadiusPixels
                << "\n"
                << "horizon_enabled="
                << (settings.horizonOcclusionEnabled
                    ? "true"
                    : "false")
                << "\n"
                << "horizon_strength_setting="
                << settings.horizonOcclusionStrength
                << "\n"
                << "horizon_strength_multiplier="
                << gzfx::render::product_control::
                    HorizonStrengthMultiplier(
                        settings.
                            horizonOcclusionStrength)
                << "\n"
                << "horizon_radius_setting="
                << settings.horizonOcclusionRadiusPixels
                << "\n"
                << "horizon_radius_world="
                << gzfx::render::product_control::
                    HorizonRadiusWorld(
                        settings.
                            horizonOcclusionRadiusPixels)
                << "\n"
                << "horizon_quality="
                << settings.horizonOcclusionQuality
                << "\n"
                << "horizon_slices="
                << quality.slices
                << "\n"
                << "horizon_steps="
                << quality.steps
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
                << "\n"
                << "local_glow_enabled="
                << (settings.localGlowEnabled
                    ? "true"
                    : "false")
                << "\n"
                << "local_glow_strength="
                << settings.localGlowStrength
                << "\n"
                << "local_glow_threshold="
                << settings.localGlowThreshold
                << "\n"
                << "local_glow_radius="
                << settings.localGlowRadiusPixels
                << "\n"
                << "wave4_owns_ao="
                << (wave4OwnsAo
                    ? "true"
                    : "false")
                << "\n"
                << "av2_applied_current_frame="
                << (gzfx::render::
                        ambient_visibility_v2::
                        WasAppliedFrame(frame)
                    ? "true"
                    : "false")
                << "\n"
                << "indirect_stack_applied="
                << (indirectApplied
                    ? "true"
                    : "false")
                << "\n"
                << "scene_effects_injected=true\n";

            g_lastControlProofFrame =
                frame;
        }

        void ReleaseRuntime()
        {
            if (g_runtime.outputRtv) g_runtime.outputRtv->Release();
            if (g_runtime.output) g_runtime.output->Release();
            if (g_runtime.sourceSrv) g_runtime.sourceSrv->Release();
            if (g_runtime.sourceCopy) g_runtime.sourceCopy->Release();
            if (g_runtime.parameters) g_runtime.parameters->Release();
            if (g_runtime.pointSampler) g_runtime.pointSampler->Release();
            if (g_runtime.linearSampler) g_runtime.linearSampler->Release();
            if (g_runtime.pixelShader) g_runtime.pixelShader->Release();
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
                    "GZFXSceneEffects",
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
                        "D3DCompile failed without diagnostics.";
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

cbuffer GZFXSceneParameters : register(b0)
{
    float2 InvSize;
    float AmbientStrength;
    float AmbientRadius;

    float GlowStrength;
    float GlowThreshold;
    float GlowRadius;
    uint GlowEnabled;

    uint Mode;
    float3 ParameterPadding;
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

SamplerState LinearSampler : register(s0);
SamplerState PointSampler : register(s1);

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

    return
        view.xyz /
        max(abs(view.w), 1e-6);
}

float3 ReconstructNormal(
    float2 uv,
    float centerDepth)
{
    float2 dx =
        float2(InvSize.x, 0.0);

    float2 dy =
        float2(0.0, InvSize.y);

    float depthX =
        SceneDepth.SampleLevel(
            PointSampler,
            DepthUv(
                saturate(uv + dx)),
            0).x;

    float depthY =
        SceneDepth.SampleLevel(
            PointSampler,
            DepthUv(
                saturate(uv + dy)),
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

    if (dot(normal, -center) < 0.0)
    {
        normal = -normal;
    }

    return normal;
}

float HashAngle(float2 pixel)
{
    return
        frac(
            sin(
                dot(
                    pixel,
                    float2(
                        12.9898,
                        78.233))) *
            43758.5453) *
        6.2831853;
}

float AmbientContact(float2 uv)
{
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

    static const float2 baseDirections[12] =
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

    float angle =
        HashAngle(
            floor(
                uv / InvSize));

    float cs = cos(angle);
    float sn = sin(angle);

    float2x2 rotation =
        float2x2(
            cs, -sn,
            sn,  cs);

    float occlusion = 0.0;
    float weightTotal = 0.0;

    [unroll]
    for (uint i = 0; i < 12; ++i)
    {
        float radialScale =
            0.40 +
            0.60 *
            ((i % 3) + 1) /
            3.0;

        float2 direction =
            mul(
                rotation,
                baseDirections[i]);

        float2 sampleUv =
            saturate(
                uv +
                direction *
                InvSize *
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

        float3 delta =
            neighbor - center;

        float distanceToNeighbor =
            length(delta);

        if (distanceToNeighbor <= 1e-4)
        {
            continue;
        }

        float worldRadius =
            max(
                abs(center.z) * 0.045,
                5.0);

        float proximity =
            saturate(
                1.0 -
                distanceToNeighbor /
                worldRadius);

        if (proximity <= 0.0)
        {
            continue;
        }

        float3 direction3 =
            delta /
            distanceToNeighbor;

        float hemisphere =
            saturate(
                dot(
                    normal,
                    direction3));

        float bias =
            max(
                abs(center.z) * 0.00065,
                0.035);

        float closer =
            saturate(
                (
                    center.z -
                    neighbor.z -
                    bias
                ) /
                max(
                    bias * 10.0,
                    0.001));

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

    return
        weightTotal > 0.0
        ? saturate(
            occlusion /
            weightTotal)
        : 0.0;
}

float3 BrightContribution(
    float3 color)
{
    float luminance =
        dot(
            max(color, 0.0),
            float3(
                0.2126,
                0.7152,
                0.0722));

    float weight =
        saturate(
            (
                luminance -
                GlowThreshold
            ) /
            max(
                GlowThreshold,
                0.001));

    return
        max(color, 0.0) *
        weight;
}

float3 LocalGlow(float2 uv)
{
    if (GlowEnabled == 0 ||
        GlowStrength <= 0.0)
    {
        return 0.0;
    }

    static const float2 directions[8] =
    {
        float2( 1.0000,  0.0000),
        float2( 0.7071,  0.7071),
        float2( 0.0000,  1.0000),
        float2(-0.7071,  0.7071),
        float2(-1.0000,  0.0000),
        float2(-0.7071, -0.7071),
        float2( 0.0000, -1.0000),
        float2( 0.7071, -0.7071)
    };

    float3 glow = 0.0;

    [unroll]
    for (uint i = 0; i < 8; ++i)
    {
        float radiusScale =
            i < 4
            ? 0.55
            : 1.0;

        float2 sampleUv =
            saturate(
                uv +
                directions[i] *
                InvSize *
                GlowRadius *
                radiusScale);

        glow +=
            BrightContribution(
                SceneColor.SampleLevel(
                    LinearSampler,
                    sampleUv,
                    0).rgb);
    }

    return
        glow *
        (GlowStrength / 8.0);
}

float4 PSMain(VSOutput input) : SV_TARGET
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

    float contact =
        AmbientContact(
            input.uv);

    float3 effected =
        max(
            original.rgb +
            LocalGlow(input.uv),
            0.0);

    effected *=
        1.0 -
        contact *
        AmbientStrength;

    if (Mode == 1 &&
        input.uv.x < 0.5)
    {
        return original;
    }

    return float4(
        effected,
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
                return DXGI_FORMAT_R32_FLOAT;
            }

            if (format ==
                DXGI_FORMAT_R16_TYPELESS)
            {
                return DXGI_FORMAT_R16_UNORM;
            }

            return DXGI_FORMAT_UNKNOWN;
        }

        float MatrixInverseError(
            const float* a,
            const float* b)
        {
            float maximum = 0.0f;

            for (int row = 0;
                 row < 4;
                 ++row)
            {
                for (int column = 0;
                     column < 4;
                     ++column)
                {
                    float value = 0.0f;

                    for (int k = 0;
                         k < 4;
                         ++k)
                    {
                        value +=
                            a[row * 4 + k] *
                            b[k * 4 + column];
                    }

                    const float expected =
                        row == column
                        ? 1.0f
                        : 0.0f;

                    maximum =
                        std::max(
                            maximum,
                            std::abs(
                                value -
                                expected));
                }
            }

            return maximum;
        }

        bool ValidateCamera(
            ID3D11Device* device,
            ID3D11DeviceContext* context,
            ID3D11Buffer* camera,
            std::string& detail)
        {
            if (camera == nullptr ||
                device == nullptr ||
                context == nullptr)
            {
                detail =
                    "missing camera/device/context";
                return false;
            }

            if (g_cameraValidated &&
                g_validatedCamera == camera)
            {
                return true;
            }

            D3D11_BUFFER_DESC sourceDesc{};
            camera->GetDesc(&sourceDesc);

            if (sourceDesc.ByteWidth != 720)
            {
                detail =
                    "camera byte width != 720";
                return false;
            }

            D3D11_BUFFER_DESC stagingDesc{};
            stagingDesc.ByteWidth =
                sourceDesc.ByteWidth;
            stagingDesc.Usage =
                D3D11_USAGE_STAGING;
            stagingDesc.CPUAccessFlags =
                D3D11_CPU_ACCESS_READ;

            ID3D11Buffer* staging = nullptr;

            HRESULT result =
                device->CreateBuffer(
                    &stagingDesc,
                    nullptr,
                    &staging);

            if (FAILED(result) ||
                staging == nullptr)
            {
                detail =
                    "camera staging allocation failed";
                return false;
            }

            context->CopyResource(
                staging,
                camera);

            D3D11_MAPPED_SUBRESOURCE mapped{};

            result =
                context->Map(
                    staging,
                    0,
                    D3D11_MAP_READ,
                    0,
                    &mapped);

            if (FAILED(result))
            {
                staging->Release();
                detail =
                    "camera staging map failed";
                return false;
            }

            std::array<float, 180> values{};

            std::memcpy(
                values.data(),
                mapped.pData,
                sizeof(values));

            context->Unmap(
                staging,
                0);

            staging->Release();

            const float viewError =
                MatrixInverseError(
                    values.data() + 0 * 16,
                    values.data() + 7 * 16);

            const float projectionError =
                MatrixInverseError(
                    values.data() + 1 * 16,
                    values.data() + 9 * 16);

            const float viewProjectionError =
                MatrixInverseError(
                    values.data() + 2 * 16,
                    values.data() + 8 * 16);

            const float maximum =
                std::max(
                    viewError,
                    std::max(
                        projectionError,
                        viewProjectionError));

            if (maximum >= 0.001f)
            {
                std::ostringstream stream;
                stream
                    << "camera validation error="
                    << std::setprecision(9)
                    << maximum;

                detail = stream.str();
                return false;
            }

            g_cameraValidated = true;
            g_validatedCamera = camera;

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

            if (sceneDescription.SampleDesc.Count != 1 ||
                sceneDescription.Width == 0 ||
                sceneDescription.Height == 0)
            {
                error =
                    "unsupported scene texture sample count/size";
                return false;
            }

            g_runtime.device = device;
            device->AddRef();

            HRESULT result =
                device->CreateDeferredContext(
                    0,
                    &g_runtime.deferred);

            ID3DBlob* vertexBlob = nullptr;
            ID3DBlob* pixelBlob = nullptr;

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
                    "PSMain",
                    "ps_5_0",
                    &pixelBlob,
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
                        pixelBlob->GetBufferPointer(),
                        pixelBlob->GetBufferSize(),
                        nullptr,
                        &g_runtime.pixelShader);
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
                sceneDescription;

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
                        &g_runtime.sourceCopy);
            }

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateShaderResourceView(
                        g_runtime.sourceCopy,
                        nullptr,
                        &g_runtime.sourceSrv);
            }

            D3D11_TEXTURE2D_DESC outputDesc =
                sceneDescription;

            outputDesc.Usage =
                D3D11_USAGE_DEFAULT;

            outputDesc.BindFlags =
                D3D11_BIND_RENDER_TARGET;

            outputDesc.CPUAccessFlags = 0;
            outputDesc.MiscFlags = 0;

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateTexture2D(
                        &outputDesc,
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

            if (vertexBlob) vertexBlob->Release();
            if (pixelBlob) pixelBlob->Release();

            if (FAILED(result))
            {
                std::ostringstream stream;
                stream
                    << "scene effect runtime allocation HRESULT=0x"
                    << std::hex
                    << std::uppercase
                    << static_cast<std::uint32_t>(
                        result);

                if (error.empty())
                {
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

            return true;
        }
    }

    bool WasInjectedFrame(
        const std::uint64_t frame) noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);
            return g_lastFrame == frame;
        }
        catch (...)
        {
            return false;
        }
    }

    bool ApplyPreUi(
        const std::uint64_t frame,
        const std::uint64_t drawOrdinal,
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

            const bool wave4OwnsAo =
                gzfx::render::
                    ambient_visibility_v2::
                    OwnsDevelopmentMode(mode);

            if (mode != 1 &&
                mode != 2)
            {
                WriteLog(
                    frame,
                    drawOrdinal,
                    "skipped_mode");

                return false;
            }

            if (inputs.sceneColorTexture == nullptr ||
                inputs.depthTexture == nullptr ||
                inputs.cameraBuffer == nullptr)
            {
                WriteLog(
                    frame,
                    drawOrdinal,
                    "skipped_missing_inputs");

                return false;
            }

            std::string detail;

            if (!ValidateCamera(
                    device,
                    context,
                    inputs.cameraBuffer,
                    detail))
            {
                WriteLog(
                    frame,
                    drawOrdinal,
                    "skipped_camera_validation",
                    detail);

                return false;
            }

            const bool ambientDepthApplied =
                gzfx::render::ambient_depth::Apply(
                    frame,
                    device,
                    context,
                    inputs,
                    !wave4OwnsAo);

            (void)ambientDepthApplied;

            if (!EnsureRuntime(
                    device,
                    inputs.sceneColorDesc,
                    detail))
            {
                WriteLog(
                    frame,
                    drawOrdinal,
                    "skipped_runtime",
                    detail);

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
                    drawOrdinal,
                    "skipped_depth_format");

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
                    drawOrdinal,
                    "skipped_depth_srv");

                return false;
            }

            const auto& settings =
                gzfx::settings::Get();

            Parameters parameters{};

            parameters.invWidth =
                1.0f /
                static_cast<float>(
                    inputs.sceneColorDesc.Width);

            parameters.invHeight =
                1.0f /
                static_cast<float>(
                    inputs.sceneColorDesc.Height);

            parameters.ambientStrength =
                settings.ambientContactStrength;

            parameters.ambientRadius =
                settings.ambientContactRadiusPixels;

            parameters.glowStrength =
                settings.localGlowStrength;

            parameters.glowThreshold =
                settings.localGlowThreshold;

            parameters.glowRadius =
                settings.localGlowRadiusPixels;

            parameters.glowEnabled =
                settings.localGlowEnabled
                ? 1u
                : 0u;

            parameters.mode = mode;

            g_runtime.deferred->ClearState();

            g_runtime.deferred->OMSetRenderTargets(
                0,
                nullptr,
                nullptr);

            g_runtime.deferred->CopyResource(
                g_runtime.sourceCopy,
                inputs.sceneColorTexture);

            g_runtime.deferred->UpdateSubresource(
                g_runtime.parameters,
                0,
                nullptr,
                &parameters,
                0,
                0);

            D3D11_VIEWPORT viewport{};
            viewport.Width =
                static_cast<float>(
                    inputs.sceneColorDesc.Width);

            viewport.Height =
                static_cast<float>(
                    inputs.sceneColorDesc.Height);

            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;

            ID3D11ShaderResourceView* resources[2] =
            {
                g_runtime.sourceSrv,
                depthSrv
            };

            ID3D11SamplerState* samplers[2] =
            {
                g_runtime.linearSampler,
                g_runtime.pointSampler
            };

            g_runtime.deferred->OMSetRenderTargets(
                1,
                &g_runtime.outputRtv,
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
                g_runtime.pixelShader,
                nullptr,
                0);

            g_runtime.deferred->PSSetConstantBuffers(
                0,
                1,
                &g_runtime.parameters);

            g_runtime.deferred->PSSetConstantBuffers(
                12,
                1,
                &inputs.cameraBuffer);

            g_runtime.deferred->PSSetShaderResources(
                0,
                2,
                resources);

            g_runtime.deferred->PSSetSamplers(
                0,
                2,
                samplers);

            g_runtime.deferred->Draw(
                3,
                0);

            ID3D11ShaderResourceView*
                nullResources[2] =
            {
                nullptr,
                nullptr
            };

            g_runtime.deferred->PSSetShaderResources(
                0,
                2,
                nullResources);

            g_runtime.deferred->OMSetRenderTargets(
                0,
                nullptr,
                nullptr);

            g_runtime.deferred->CopyResource(
                inputs.sceneColorTexture,
                g_runtime.output);

            ID3D11CommandList* commands = nullptr;

            result =
                g_runtime.deferred->
                    FinishCommandList(
                        FALSE,
                        &commands);

            if (SUCCEEDED(result) &&
                commands != nullptr)
            {
                context->ExecuteCommandList(
                    commands,
                    TRUE);
            }

            if (commands) commands->Release();
            depthSrv->Release();

            if (FAILED(result))
            {
                std::ostringstream stream;
                stream
                    << "FinishCommandList HRESULT=0x"
                    << std::hex
                    << std::uppercase
                    << static_cast<std::uint32_t>(
                        result);

                WriteLog(
                    frame,
                    drawOrdinal,
                    "failed_execute",
                    stream.str());

                return false;
            }

            ++g_injectionCount;
            g_lastFrame = frame;
            g_lastDraw = drawOrdinal;

            WriteLog(
                frame,
                drawOrdinal,
                "pre_ui_injected");

            WriteProductControlSubmission(
                frame,
                wave4OwnsAo,
                ambientDepthApplied);

            return true;
        }
        catch (...)
        {
            WriteLog(
                frame,
                drawOrdinal,
                "exception");

            return false;
        }
    }
}
