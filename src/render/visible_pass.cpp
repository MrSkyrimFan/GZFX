#include "gzfx/render/visible_pass.hpp"

#include "gzfx/diagnostics/resource_intelligence.hpp"
#include "gzfx/paths.hpp"
#include "gzfx/settings.hpp"
#include "gzfx/ui/settings_menu.hpp"
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
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace gzfx::render
{
    namespace fs = std::filesystem;
    using gzfx::diagnostics::resource_intelligence::LiveInputs;

    namespace
    {
        struct Parameters
        {
            float invWidth = 0.0f;
            float invHeight = 0.0f;
            float strength = 0.55f;
            float radiusPixels = 4.0f;
            std::uint32_t mode = 1;
            std::uint32_t accessibleDiagnostics = 1;
            std::uint32_t menuVisible = 0;
            std::uint32_t padding0 = 0;

            float menuWidth = 0.0f;
            float menuHeight = 0.0f;
            float padding1[2]{};
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
            ID3D11ShaderResourceView* sourceCopySrv = nullptr;

            ID3D11Texture2D* outputTexture = nullptr;
            ID3D11RenderTargetView* outputRtv = nullptr;
            ID3D11ShaderResourceView* outputSrv = nullptr;

            ID3D11Texture2D* staging = nullptr;

            UINT width = 0;
            UINT height = 0;
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        };

        std::mutex g_mutex;
        Runtime g_runtime;

        std::uint32_t g_mode = 1;
        bool g_modeInitialized = false;
        bool g_cameraValidated = false;
        ID3D11Buffer* g_validatedCamera = nullptr;

        int g_worldActivityScore = 0;
        bool g_worldActive = false;
        bool g_captureRequested = false;
        std::uint64_t g_worldEntryFrame = 0;
        std::uint64_t g_drawIndexedThisFrame = 0;

        bool FastTestMode()
        {
            wchar_t value[8]{};
            return GetEnvironmentVariableW(
                L"GZFX_TEST_FAST_CAPTURE",
                value,
                static_cast<DWORD>(std::size(value))) > 0;
        }

        std::uint64_t ActivationFrame()
        {
            return FastTestMode() ? 3 : 120;
        }

        void WriteText(
            const wchar_t* fileName,
            const std::string& text)
        {
            const fs::path root = gzfx::paths::DiagnosticsRoot();
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
                root / fileName,
                std::ios::out | std::ios::trunc);

            if (out)
            {
                out << text;
            }
        }

        std::string PointerText(const void* value)
        {
            std::ostringstream stream;
            stream << "0x" << std::hex << std::uppercase
                   << reinterpret_cast<std::uintptr_t>(value);
            return stream.str();
        }

        const char* ModeName(const std::uint32_t mode)
        {
            switch (mode)
            {
            case 0: return "off";
            case 1: return "split_ambient_contact";
            case 2: return "full_ambient_contact";
            case 3: return "depth_view";
            case 4: return "pre_ui_scene_color";
            default: return "unknown";
            }
        }

        void LogState(
            const std::uint64_t frame,
            const char* status,
            const LiveInputs* inputs = nullptr)
        {
            std::ostringstream stream;
            stream
                << "GZFX visible pass\n"
                << "version=" << gzfx::kVersion << "\n"
                << "frame=" << frame << "\n"
                << "status=" << status << "\n"
                << "mode=" << g_mode
                << " (" << ModeName(g_mode) << ")\n"
                << "world_active="
                << (g_worldActive ? "true" : "false")
                << "\n"
                << "world_activity_score="
                << g_worldActivityScore
                << "\n"
                << "draw_indexed_this_frame="
                << g_drawIndexedThisFrame
                << "\n";

            if (inputs != nullptr)
            {
                stream
                    << "depth=" << PointerText(inputs->depthTexture)
                    << " " << inputs->depthDesc.Width
                    << "x" << inputs->depthDesc.Height
                    << " format="
                    << static_cast<unsigned>(inputs->depthDesc.Format)
                    << "\n"
                    << "scene_color="
                    << PointerText(inputs->sceneColorTexture)
                    << " " << inputs->sceneColorDesc.Width
                    << "x" << inputs->sceneColorDesc.Height
                    << " format="
                    << static_cast<unsigned>(
                        inputs->sceneColorDesc.Format)
                    << "\n"
                    << "camera_buffer="
                    << PointerText(inputs->cameraBuffer)
                    << "\n";
            }

            stream
                << "controls=Shift+Numpad1 menu; "
                   "Shift+Numpad2 reload Settings.ini; "
                   "Shift+Numpad3 cycles Off/Split Contact/"
                   "Full Contact/Depth/Pre-UI Color\n"
                << "diagnostic_accessibility="
                << (gzfx::settings::Get().accessibleDiagnostics
                    ? "monochrome_labels_and_pattern"
                    : "disabled")
                << "\n";

            WriteText(L"visible_pass.txt", stream.str());
        }

        void ReleaseRuntime()
        {
            if (g_runtime.staging) g_runtime.staging->Release();
            if (g_runtime.outputSrv) g_runtime.outputSrv->Release();
            if (g_runtime.outputRtv) g_runtime.outputRtv->Release();
            if (g_runtime.outputTexture) g_runtime.outputTexture->Release();
            if (g_runtime.sourceCopySrv) g_runtime.sourceCopySrv->Release();
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

            const HRESULT result = D3DCompile(
                source,
                std::strlen(source),
                "GZFXVisiblePass",
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
                    error = "D3DCompile failed.";
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

cbuffer GZFXParameters : register(b0)
{
    float2 InvSize;
    float Strength;
    float RadiusPixels;

    uint Mode;
    uint AccessibleDiagnostics;
    uint MenuVisible;
    uint ParameterPadding0;

    float2 MenuSize;
    float2 ParameterPadding1;
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

Texture2D<float4> BackColor : register(t0);
Texture2D<float> SceneDepth : register(t1);
Texture2D<float4> PreUiColor : register(t2);
Texture2D<float4> MenuOverlay : register(t3);

SamplerState LinearSampler : register(s0);
SamplerState PointSampler : register(s1);

float2 DepthUv(float2 uv)
{
    return saturate(uv * DynamicResolutionParams1.xy);
}

float3 ReconstructView(float2 uv, float depth)
{
    float4 clip =
        float4(
            2.0 * float2(uv.x, 1.0 - uv.y) - 1.0,
            depth,
            1.0);

    float4 view = mul(CameraProjInverse, clip);

    return view.xyz / max(abs(view.w), 1e-6);
}

float AmbientContact(float2 uv)
{
    float2 depthUv = DepthUv(uv);
    float centerDepth =
        SceneDepth.SampleLevel(
            PointSampler,
            depthUv,
            0).x;

    if (centerDepth >= 0.99999)
    {
        return 0.0;
    }

    float3 center =
        ReconstructView(uv, centerDepth);

    static const float2 directions[8] =
    {
        float2( 1,  0),
        float2(-1,  0),
        float2( 0,  1),
        float2( 0, -1),
        float2( 0.7071,  0.7071),
        float2(-0.7071,  0.7071),
        float2( 0.7071, -0.7071),
        float2(-0.7071, -0.7071)
    };

    float occlusion = 0.0;

    [unroll]
    for (uint i = 0; i < 8; ++i)
    {
        float2 sampleUv =
            saturate(
                uv +
                directions[i] *
                InvSize *
                RadiusPixels);

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
            ReconstructView(sampleUv, sampleDepth);

        float deltaZ =
            center.z - neighbor.z;

        float bias =
            max(center.z * 0.00075, 0.05);

        float worldRadius =
            max(center.z * 0.035, 4.0);

        float proximity =
            saturate(
                1.0 -
                length(neighbor - center) /
                worldRadius);

        float closer =
            saturate(
                (deltaZ - bias) /
                max(bias * 12.0, 0.001));

        occlusion += closer * proximity;
    }

    return saturate(occlusion / 8.0);
}

float3 ToneMap(float3 color)
{
    color = max(color, 0.0);
    color = color / (1.0 + color);
    return pow(color, 1.0 / 2.2);
}


uint GlyphRow(uint code, uint row)
{
    if (code == 0) // O
    {
        if (row == 0 || row == 6) return 14;
        return 17;
    }

    if (code == 1) // R
    {
        if (row == 0 || row == 3) return 30;
        if (row == 1 || row == 2) return 17;
        if (row == 4) return 20;
        if (row == 5) return 18;
        return 17;
    }

    if (code == 2) // I
    {
        if (row == 0 || row == 6) return 31;
        return 4;
    }

    if (code == 3) // G
    {
        if (row == 0) return 14;
        if (row == 1 || row == 4 || row == 5) return 17;
        if (row == 2) return 16;
        if (row == 3) return 23;
        return 15;
    }

    if (code == 4) // N
    {
        if (row == 1) return 25;
        if (row == 2) return 21;
        if (row == 3) return 19;
        return 17;
    }

    if (code == 5) // A
    {
        if (row == 0) return 14;
        if (row == 3) return 31;
        return 17;
    }

    if (code == 6) // L
    {
        return row == 6 ? 31 : 16;
    }

    if (code == 7) // Z
    {
        if (row == 0 || row == 6) return 31;
        if (row == 1) return 1;
        if (row == 2) return 2;
        if (row == 3) return 4;
        if (row == 4) return 8;
        return 16;
    }

    if (code == 8) // F
    {
        if (row == 0) return 31;
        if (row == 3) return 30;
        return 16;
    }

    if (code == 9) // X
    {
        if (row == 0 || row == 1 || row == 5 || row == 6) return 17;
        if (row == 2 || row == 4) return 10;
        return 4;
    }

    return 0;
}

uint OriginalCode(uint index)
{
    if (index == 0) return 0;
    if (index == 1) return 1;
    if (index == 2) return 2;
    if (index == 3) return 3;
    if (index == 4) return 2;
    if (index == 5) return 4;
    if (index == 6) return 5;
    return 6;
}

uint GzfxCode(uint index)
{
    if (index == 0) return 3;
    if (index == 1) return 7;
    if (index == 2) return 8;
    return 9;
}

float WordPixel(
    float2 pixel,
    float2 origin,
    uint word,
    uint length)
{
    const float scale = 2.0;
    const float advance = 6.0 * scale;

    float2 local = pixel - origin;

    if (local.x < 0.0 ||
        local.y < 0.0 ||
        local.y >= 7.0 * scale)
    {
        return 0.0;
    }

    uint charIndex =
        (uint)floor(local.x / advance);

    if (charIndex >= length)
    {
        return 0.0;
    }

    float charX =
        local.x -
        charIndex * advance;

    if (charX >= 5.0 * scale)
    {
        return 0.0;
    }

    uint column =
        (uint)floor(charX / scale);

    uint row =
        (uint)floor(local.y / scale);

    uint code =
        word == 0
        ? OriginalCode(charIndex)
        : GzfxCode(charIndex);

    uint bits = GlyphRow(code, row);
    uint mask = 1u << (4u - column);

    return (bits & mask) != 0u ? 1.0 : 0.0;
}

float4 AccessibleSplitOverlay(
    float4 color,
    VSOutput input)
{
    if (AccessibleDiagnostics == 0 ||
        Mode != 1)
    {
        return color;
    }

    float2 pixel = input.position.xy;
    float width = 1.0 / InvSize.x;
    float center = width * 0.5;

    if (abs(pixel.x - center) <= 3.0)
    {
        uint dash =
            ((uint)floor(pixel.y / 8.0)) & 1u;

        return dash == 0u
            ? float4(1, 1, 1, 1)
            : float4(0, 0, 0, 1);
    }

    float2 leftBoxMin = float2(12, 12);
    float2 leftBoxMax = float2(116, 36);

    float2 rightBoxMin =
        float2(width - 68, 12);
    float2 rightBoxMax =
        float2(width - 12, 36);

    bool inLeft =
        all(pixel >= leftBoxMin) &&
        all(pixel < leftBoxMax);

    bool inRight =
        all(pixel >= rightBoxMin) &&
        all(pixel < rightBoxMax);

    if (inLeft)
    {
        bool border =
            pixel.x < leftBoxMin.x + 1 ||
            pixel.x >= leftBoxMax.x - 1 ||
            pixel.y < leftBoxMin.y + 1 ||
            pixel.y >= leftBoxMax.y - 1;

        float glyph =
            WordPixel(
                pixel,
                leftBoxMin + float2(4, 5),
                0,
                8);

        return (border || glyph > 0.5)
            ? float4(1, 1, 1, 1)
            : float4(0, 0, 0, 1);
    }

    if (inRight)
    {
        bool border =
            pixel.x < rightBoxMin.x + 1 ||
            pixel.x >= rightBoxMax.x - 1 ||
            pixel.y < rightBoxMin.y + 1 ||
            pixel.y >= rightBoxMax.y - 1;

        float glyph =
            WordPixel(
                pixel,
                rightBoxMin + float2(4, 5),
                1,
                4);

        return (border || glyph > 0.5)
            ? float4(1, 1, 1, 1)
            : float4(0, 0, 0, 1);
    }

    return color;
}

float4 ApplyMenuOverlay(
    float4 baseColor,
    VSOutput input)
{
    if (MenuVisible == 0)
    {
        return baseColor;
    }

    float2 pixel = input.position.xy;

    if (pixel.x < 0.0 ||
        pixel.y < 0.0 ||
        pixel.x >= MenuSize.x ||
        pixel.y >= MenuSize.y)
    {
        return baseColor;
    }

    float2 uv =
        pixel /
        max(MenuSize, float2(1.0, 1.0));

    float4 menu =
        MenuOverlay.SampleLevel(
            PointSampler,
            uv,
            0);

    return float4(
        lerp(
            baseColor.rgb,
            menu.rgb,
            saturate(menu.a)),
        baseColor.a);
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float4 original =
        BackColor.SampleLevel(
            LinearSampler,
            input.uv,
            0);

    if (Mode == 0)
    {
        return ApplyMenuOverlay(
            original,
            input);
    }

    if (Mode == 3)
    {
        float depth =
            SceneDepth.SampleLevel(
                PointSampler,
                DepthUv(input.uv),
                0).x;

        float visible =
            pow(
                saturate(1.0 - depth),
                0.20);

        return ApplyMenuOverlay(
            float4(
                visible,
                visible,
                visible,
                1.0),
            input);
    }

    if (Mode == 4)
    {
        return ApplyMenuOverlay(
            float4(
                ToneMap(
                    PreUiColor.SampleLevel(
                        LinearSampler,
                        DepthUv(input.uv),
                        0).rgb),
                1.0),
            input);
    }

    float contact =
        AmbientContact(input.uv);

    float3 effected =
        original.rgb *
        (1.0 - contact * Strength);

    if (Mode == 1)
    {
        float separator =
            abs(input.uv.x - 0.5);

        if (input.uv.x < 0.5)
        {
            return ApplyMenuOverlay(
                AccessibleSplitOverlay(
                    original,
                    input),
                input);
        }

        return ApplyMenuOverlay(
            AccessibleSplitOverlay(
                float4(
                    effected,
                    original.a),
                input),
            input);
    }

    return ApplyMenuOverlay(
        float4(
            effected,
            original.a),
        input);
}
)";
        }

        bool EnsureRuntime(
            ID3D11Device* device,
            const D3D11_TEXTURE2D_DESC& backDesc,
            std::string& error)
        {
            if (device == nullptr)
            {
                error = "device unavailable";
                return false;
            }

            if (g_runtime.device == device &&
                g_runtime.width == backDesc.Width &&
                g_runtime.height == backDesc.Height &&
                g_runtime.format == backDesc.Format)
            {
                return true;
            }

            ReleaseRuntime();

            if (backDesc.SampleDesc.Count != 1 ||
                (backDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
                 backDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB))
            {
                error = "unsupported backbuffer format/sample count";
                return false;
            }

            g_runtime.device = device;
            device->AddRef();

            HRESULT result =
                device->CreateDeferredContext(
                    0,
                    &g_runtime.deferred);

            ID3DBlob* vsBlob = nullptr;
            ID3DBlob* psBlob = nullptr;

            if (SUCCEEDED(result) &&
                !Compile(
                    ShaderSource(),
                    "VSMain",
                    "vs_5_0",
                    &vsBlob,
                    error))
            {
                result = E_FAIL;
            }

            if (SUCCEEDED(result) &&
                !Compile(
                    ShaderSource(),
                    "PSMain",
                    "ps_5_0",
                    &psBlob,
                    error))
            {
                result = E_FAIL;
            }

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateVertexShader(
                        vsBlob->GetBufferPointer(),
                        vsBlob->GetBufferSize(),
                        nullptr,
                        &g_runtime.vertexShader);
            }

            if (SUCCEEDED(result))
            {
                result =
                    device->CreatePixelShader(
                        psBlob->GetBufferPointer(),
                        psBlob->GetBufferSize(),
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
            linearDesc.MaxLOD = D3D11_FLOAT32_MAX;

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateSamplerState(
                        &linearDesc,
                        &g_runtime.linearSampler);
            }

            D3D11_SAMPLER_DESC pointDesc = linearDesc;
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
            parameterDesc.ByteWidth = sizeof(Parameters);
            parameterDesc.Usage = D3D11_USAGE_DEFAULT;
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

            D3D11_TEXTURE2D_DESC sourceDesc = backDesc;
            sourceDesc.Usage = D3D11_USAGE_DEFAULT;
            sourceDesc.BindFlags =
                D3D11_BIND_SHADER_RESOURCE;
            sourceDesc.CPUAccessFlags = 0;
            sourceDesc.MiscFlags = 0;

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateTexture2D(
                        &sourceDesc,
                        nullptr,
                        &g_runtime.sourceCopy);
            }

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateShaderResourceView(
                        g_runtime.sourceCopy,
                        nullptr,
                        &g_runtime.sourceCopySrv);
            }

            D3D11_TEXTURE2D_DESC outputDesc = backDesc;
            outputDesc.Usage = D3D11_USAGE_DEFAULT;
            outputDesc.BindFlags =
                D3D11_BIND_RENDER_TARGET |
                D3D11_BIND_SHADER_RESOURCE;
            outputDesc.CPUAccessFlags = 0;
            outputDesc.MiscFlags = 0;

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateTexture2D(
                        &outputDesc,
                        nullptr,
                        &g_runtime.outputTexture);
            }

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateRenderTargetView(
                        g_runtime.outputTexture,
                        nullptr,
                        &g_runtime.outputRtv);
            }

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateShaderResourceView(
                        g_runtime.outputTexture,
                        nullptr,
                        &g_runtime.outputSrv);
            }

            D3D11_TEXTURE2D_DESC stagingDesc = outputDesc;
            stagingDesc.Usage = D3D11_USAGE_STAGING;
            stagingDesc.BindFlags = 0;
            stagingDesc.CPUAccessFlags =
                D3D11_CPU_ACCESS_READ;

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateTexture2D(
                        &stagingDesc,
                        nullptr,
                        &g_runtime.staging);
            }

            if (vsBlob) vsBlob->Release();
            if (psBlob) psBlob->Release();

            if (FAILED(result))
            {
                std::ostringstream stream;
                stream
                    << "runtime resource creation failed HRESULT=0x"
                    << std::hex << std::uppercase
                    << static_cast<std::uint32_t>(result);

                if (error.empty())
                {
                    error = stream.str();
                }

                ReleaseRuntime();
                return false;
            }

            g_runtime.width = backDesc.Width;
            g_runtime.height = backDesc.Height;
            g_runtime.format = backDesc.Format;
            return true;
        }

        DXGI_FORMAT DepthSrvFormat(
            const DXGI_FORMAT format)
        {
            if (format == DXGI_FORMAT_R24G8_TYPELESS)
            {
                return
                    DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            }

            if (format == DXGI_FORMAT_R32_TYPELESS)
            {
                return DXGI_FORMAT_R32_FLOAT;
            }

            if (format == DXGI_FORMAT_R16_TYPELESS)
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

            for (int row = 0; row < 4; ++row)
            {
                for (int column = 0; column < 4; ++column)
                {
                    float value = 0.0f;

                    for (int k = 0; k < 4; ++k)
                    {
                        value +=
                            a[row * 4 + k] *
                            b[k * 4 + column];
                    }

                    const float expected =
                        row == column ? 1.0f : 0.0f;

                    maximum =
                        std::max(
                            maximum,
                            std::abs(
                                value - expected));
                }
            }

            return maximum;
        }

        bool ValidateCamera(
            ID3D11Device* device,
            ID3D11DeviceContext* context,
            ID3D11Buffer* camera,
            std::string& report)
        {
            if (camera == nullptr ||
                device == nullptr ||
                context == nullptr)
            {
                report =
                    "status=failed reason=missing_camera_or_device\n";
                return false;
            }

            if (g_cameraValidated &&
                g_validatedCamera == camera)
            {
                return true;
            }

            D3D11_BUFFER_DESC desc{};
            camera->GetDesc(&desc);

            if (desc.ByteWidth != 720)
            {
                report =
                    "status=failed reason=unexpected_byte_width\n";
                return false;
            }

            D3D11_BUFFER_DESC stagingDesc{};
            stagingDesc.ByteWidth = desc.ByteWidth;
            stagingDesc.Usage = D3D11_USAGE_STAGING;
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
                report =
                    "status=failed reason=staging_create\n";
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
                report =
                    "status=failed reason=map\n";
                return false;
            }

            std::array<float, 180> values{};
            std::memcpy(
                values.data(),
                mapped.pData,
                sizeof(values));

            context->Unmap(staging, 0);
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

            std::ostringstream stream;
            stream
                << "GZFX camera buffer validation\n"
                << "buffer=" << PointerText(camera)
                << "\n"
                << "bytes=" << desc.ByteWidth
                << "\n"
                << std::setprecision(9)
                << "view_inverse_error="
                << viewError << "\n"
                << "projection_inverse_error="
                << projectionError << "\n"
                << "view_projection_inverse_error="
                << viewProjectionError << "\n"
                << "max_error=" << maximum << "\n"
                << "projection_m23="
                << values[1 * 16 + 11] << "\n"
                << "projection_m32="
                << values[1 * 16 + 14] << "\n"
                << "camera_position_adjust="
                << values[160] << ","
                << values[161] << ","
                << values[162] << "\n"
                << "status="
                << (maximum < 0.001f
                    ? "validated"
                    : "rejected")
                << "\n";

            report = stream.str();

            g_cameraValidated =
                maximum < 0.001f;

            g_validatedCamera =
                g_cameraValidated
                ? camera
                : nullptr;

            return g_cameraValidated;
        }

        bool WriteDownsampledBmp(
            const fs::path& path,
            ID3D11Texture2D* staging,
            ID3D11DeviceContext* context,
            const UINT sourceWidth,
            const UINT sourceHeight)
        {
            if (staging == nullptr ||
                context == nullptr ||
                sourceWidth == 0 ||
                sourceHeight == 0)
            {
                return false;
            }

            D3D11_MAPPED_SUBRESOURCE mapped{};

            if (FAILED(context->Map(
                    staging,
                    0,
                    D3D11_MAP_READ,
                    0,
                    &mapped)))
            {
                return false;
            }

            constexpr UINT width = 640;
            constexpr UINT height = 360;

            BITMAPFILEHEADER fileHeader{};
            BITMAPINFOHEADER infoHeader{};

            const DWORD bytes =
                width * height * 4;

            fileHeader.bfType = 0x4D42;
            fileHeader.bfOffBits =
                sizeof(BITMAPFILEHEADER) +
                sizeof(BITMAPINFOHEADER);
            fileHeader.bfSize =
                fileHeader.bfOffBits +
                bytes;

            infoHeader.biSize =
                sizeof(BITMAPINFOHEADER);
            infoHeader.biWidth =
                static_cast<LONG>(width);
            infoHeader.biHeight =
                static_cast<LONG>(height);
            infoHeader.biPlanes = 1;
            infoHeader.biBitCount = 32;
            infoHeader.biCompression = BI_RGB;
            infoHeader.biSizeImage = bytes;

            std::ofstream out(
                path,
                std::ios::binary |
                std::ios::trunc);

            if (!out)
            {
                context->Unmap(staging, 0);
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

            std::vector<std::uint8_t>
                row(width * 4);

            for (UINT y = 0; y < height; ++y)
            {
                const UINT outputY =
                    height - 1 - y;

                const UINT sourceY =
                    std::min(
                        sourceHeight - 1,
                        outputY *
                        sourceHeight /
                        height);

                const auto* sourceRow =
                    static_cast<const std::uint8_t*>(
                        mapped.pData) +
                    static_cast<std::size_t>(
                        sourceY) *
                    mapped.RowPitch;

                for (UINT x = 0; x < width; ++x)
                {
                    const UINT sourceX =
                        std::min(
                            sourceWidth - 1,
                            x *
                            sourceWidth /
                            width);

                    const auto* pixel =
                        sourceRow +
                        sourceX * 4;

                    row[x * 4 + 0] = pixel[2];
                    row[x * 4 + 1] = pixel[1];
                    row[x * 4 + 2] = pixel[0];
                    row[x * 4 + 3] = pixel[3];
                }

                out.write(
                    reinterpret_cast<const char*>(
                        row.data()),
                    static_cast<std::streamsize>(
                        row.size()));
            }

            context->Unmap(staging, 0);
            return static_cast<bool>(out);
        }

        const wchar_t* ModeCaptureFile(
            const std::uint32_t mode)
        {
            switch (mode)
            {
            case 0:
                return L"visible_mode_0_off.bmp";
            case 1:
                return L"visible_mode_1_split_contact.bmp";
            case 2:
                return L"visible_mode_2_full_contact.bmp";
            case 3:
                return L"visible_mode_3_depth.bmp";
            case 4:
                return L"visible_mode_4_pre_ui_color.bmp";
            default:
                return L"visible_mode_unknown.bmp";
            }
        }

        bool ExecutePass(
            const std::uint64_t frame,
            IDXGISwapChain* swapChain,
            ID3D11Device* device,
            ID3D11DeviceContext* context,
            LiveInputs& inputs,
            std::string& error)
        {
            ID3D11Texture2D* backbuffer = nullptr;

            HRESULT result =
                swapChain->GetBuffer(
                    0,
                    __uuidof(ID3D11Texture2D),
                    reinterpret_cast<void**>(
                        &backbuffer));

            if (FAILED(result) ||
                backbuffer == nullptr)
            {
                error = "GetBuffer failed";
                return false;
            }

            D3D11_TEXTURE2D_DESC backDesc{};
            backbuffer->GetDesc(&backDesc);

            if (!EnsureRuntime(
                    device,
                    backDesc,
                    error))
            {
                backbuffer->Release();
                return false;
            }

            std::string cameraReport;

            if (!ValidateCamera(
                    device,
                    context,
                    inputs.cameraBuffer,
                    cameraReport))
            {
                WriteText(
                    L"camera_validation.txt",
                    cameraReport);

                backbuffer->Release();
                error =
                    "camera buffer validation rejected";
                return false;
            }

            if (!cameraReport.empty())
            {
                WriteText(
                    L"camera_validation.txt",
                    cameraReport);
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC
                depthSrvDesc{};

            depthSrvDesc.Format =
                DepthSrvFormat(
                    inputs.depthDesc.Format);

            depthSrvDesc.ViewDimension =
                D3D11_SRV_DIMENSION_TEXTURE2D;

            depthSrvDesc.Texture2D.MipLevels = 1;

            if (depthSrvDesc.Format ==
                DXGI_FORMAT_UNKNOWN)
            {
                backbuffer->Release();
                error =
                    "unsupported depth format";
                return false;
            }

            ID3D11ShaderResourceView* depthSrv = nullptr;

            result =
                device->CreateShaderResourceView(
                    inputs.depthTexture,
                    &depthSrvDesc,
                    &depthSrv);

            ID3D11ShaderResourceView* sceneSrv = nullptr;

            if (SUCCEEDED(result) &&
                inputs.sceneColorTexture != nullptr)
            {
                result =
                    device->CreateShaderResourceView(
                        inputs.sceneColorTexture,
                        nullptr,
                        &sceneSrv);
            }

            if (FAILED(result) ||
                depthSrv == nullptr)
            {
                if (sceneSrv) sceneSrv->Release();
                if (depthSrv) depthSrv->Release();
                backbuffer->Release();

                error =
                    "input SRV creation failed";
                return false;
            }

            gzfx::ui::MenuOverlay menuOverlay{};

            if (!gzfx::ui::GetMenuOverlay(
                    device,
                    context,
                    menuOverlay))
            {
                if (sceneSrv) sceneSrv->Release();
                depthSrv->Release();
                backbuffer->Release();

                error = "settings menu overlay unavailable";
                return false;
            }

            Parameters parameters{};
            parameters.invWidth =
                1.0f /
                static_cast<float>(
                    backDesc.Width);

            parameters.invHeight =
                1.0f /
                static_cast<float>(
                    backDesc.Height);

            const auto& settings =
                gzfx::settings::Get();

            parameters.strength =
                settings.ambientContactStrength;

            parameters.radiusPixels =
                settings.ambientContactRadiusPixels;

            parameters.mode = g_mode;

            parameters.accessibleDiagnostics =
                settings.accessibleDiagnostics
                ? 1u
                : 0u;

            parameters.menuVisible =
                menuOverlay.visible ? 1u : 0u;

            parameters.menuWidth =
                static_cast<float>(
                    menuOverlay.width);

            parameters.menuHeight =
                static_cast<float>(
                    menuOverlay.height);

            g_runtime.deferred->ClearState();

            g_runtime.deferred->UpdateSubresource(
                g_runtime.parameters,
                0,
                nullptr,
                &parameters,
                0,
                0);

            g_runtime.deferred->OMSetRenderTargets(
                0,
                nullptr,
                nullptr);

            g_runtime.deferred->CopyResource(
                g_runtime.sourceCopy,
                backbuffer);

            D3D11_VIEWPORT viewport{};
            viewport.Width =
                static_cast<float>(
                    backDesc.Width);
            viewport.Height =
                static_cast<float>(
                    backDesc.Height);
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;

            ID3D11ShaderResourceView* srvs[4] =
            {
                g_runtime.sourceCopySrv,
                depthSrv,
                sceneSrv,
                menuOverlay.srv
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
                4,
                srvs);

            g_runtime.deferred->PSSetSamplers(
                0,
                2,
                samplers);

            g_runtime.deferred->Draw(
                3,
                0);

            ID3D11ShaderResourceView* nullSrvs[4] =
            {
                nullptr,
                nullptr,
                nullptr,
                nullptr
            };

            g_runtime.deferred->PSSetShaderResources(
                0,
                4,
                nullSrvs);

            g_runtime.deferred->OMSetRenderTargets(
                0,
                nullptr,
                nullptr);

            g_runtime.deferred->CopyResource(
                backbuffer,
                g_runtime.outputTexture);

            ID3D11CommandList* commands = nullptr;

            result =
                g_runtime.deferred->FinishCommandList(
                    FALSE,
                    &commands);

            if (SUCCEEDED(result) &&
                commands != nullptr)
            {
                context->ExecuteCommandList(
                    commands,
                    TRUE);
            }

            if (SUCCEEDED(result) &&
                g_captureRequested)
            {
                context->CopyResource(
                    g_runtime.staging,
                    g_runtime.outputTexture);

                const fs::path root =
                    gzfx::paths::DiagnosticsRoot();

                const bool latest =
                    WriteDownsampledBmp(
                        root / L"visible_pass.bmp",
                        g_runtime.staging,
                        context,
                        backDesc.Width,
                        backDesc.Height);

                const bool modeCapture =
                    WriteDownsampledBmp(
                        root / ModeCaptureFile(g_mode),
                        g_runtime.staging,
                        context,
                        backDesc.Width,
                        backDesc.Height);

                g_captureRequested =
                    !(latest && modeCapture);
            }

            if (commands) commands->Release();
            if (sceneSrv) sceneSrv->Release();
            depthSrv->Release();
            backbuffer->Release();

            if (FAILED(result))
            {
                std::ostringstream stream;
                stream
                    << "FinishCommandList failed HRESULT=0x"
                    << std::hex << std::uppercase
                    << static_cast<std::uint32_t>(result);

                error = stream.str();
                return false;
            }

            LogState(
                frame,
                "active",
                &inputs);

            return true;
        }
    }

    bool RenderVisiblePass(
        const std::uint64_t frame,
        const std::uint64_t drawIndexedThisFrame,
        IDXGISwapChain* swapChain,
        ID3D11Device* device,
        ID3D11DeviceContext* context) noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);

            g_drawIndexedThisFrame =
                drawIndexedThisFrame;

            if (!g_modeInitialized)
            {
                g_mode =
                    gzfx::settings::Get().defaultMode;

                g_modeInitialized = true;
            }

            if (FastTestMode())
            {
                g_worldActivityScore = 20;
            }
            else
            {
                const auto threshold =
                    gzfx::settings::Get()
                        .activationDrawIndexedThreshold;

                if (drawIndexedThisFrame >= threshold)
                {
                    g_worldActivityScore =
                        std::min(
                            20,
                            g_worldActivityScore + 2);
                }
                else
                {
                    g_worldActivityScore =
                        std::max(
                            0,
                            g_worldActivityScore - 1);
                }
            }

            const bool wasWorldActive =
                g_worldActive;

            g_worldActive =
                g_worldActivityScore >= 6;

            if (g_worldActive &&
                !wasWorldActive)
            {
                g_worldEntryFrame = frame;
                g_captureRequested = true;

                LogState(
                    frame,
                    "world_renderer_armed");
            }
            else if (!g_worldActive &&
                     wasWorldActive)
            {
                LogState(
                    frame,
                    "world_renderer_dormant");
            }

            if (frame < ActivationFrame() ||
                !g_worldActive ||
                swapChain == nullptr ||
                device == nullptr ||
                context == nullptr)
            {
                return false;
            }

            gzfx::ui::ProcessInput(frame);

            if (gzfx::ui::ConsumeCycleModeRequest())
            {
                g_mode =
                    (g_mode + 1) % 5;

                if (gzfx::settings::Get()
                        .captureOnModeChange)
                {
                    g_captureRequested = true;
                }

                LogState(
                    frame,
                    "mode_changed");
            }

            if (gzfx::ui::ConsumeCaptureRequest())
            {
                g_captureRequested = true;
            }

            LiveInputs inputs{};

            if (!gzfx::diagnostics::
                    resource_intelligence::
                    AcquireLiveInputs(
                        swapChain,
                        inputs))
            {
                LogState(
                    frame,
                    "waiting_for_inputs");

                return false;
            }

            std::string error;

            const bool result =
                ExecutePass(
                    frame,
                    swapChain,
                    device,
                    context,
                    inputs,
                    error);

            gzfx::diagnostics::
                resource_intelligence::
                ReleaseLiveInputs(inputs);

            if (!result)
            {
                std::ostringstream stream;
                stream
                    << "GZFX visible pass\n"
                    << "frame=" << frame << "\n"
                    << "status=failed\n"
                    << "error=" << error << "\n";

                WriteText(
                    L"visible_pass.txt",
                    stream.str());
            }

            return result;
        }
        catch (...)
        {
            WriteText(
                L"visible_pass.txt",
                "GZFX visible pass\n"
                "status=failed\n"
                "error=exception\n");

            return false;
        }
    }
}
