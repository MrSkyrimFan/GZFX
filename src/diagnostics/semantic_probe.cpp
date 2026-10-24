#include "gzfx/diagnostics/semantic_probe.hpp"

#include "gzfx/diagnostics/resource_intelligence.hpp"
#include "gzfx/diagnostics/pipeline_cartography.hpp"
#include "gzfx/paths.hpp"
#include "gzfx/version.hpp"

#include <Windows.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace gzfx::diagnostics::semantic_probe
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr std::size_t kMaxSlots = 32;
        constexpr std::size_t kMaxEvents = 24000;
        constexpr std::uint64_t kStableGameplayFrames = 30;
        constexpr std::uint64_t kTraceFrames = 3;
        constexpr std::size_t kMaxPreviewCandidates = 14;

        enum class EventKind : std::uint32_t
        {
            RenderTargets,
            PixelResources,
            ComputeResources,
            ComputeUavs
        };

        struct ResourceStats
        {
            ID3D11Texture2D* texture = nullptr;
            D3D11_TEXTURE2D_DESC desc{};
            std::uint64_t firstFrame = 0;
            std::uint64_t lastFrame = 0;
            std::uint64_t rtvWrites = 0;
            std::uint64_t dsvWrites = 0;
            std::uint64_t psReads = 0;
            std::uint64_t csReads = 0;
            std::uint64_t csUavWrites = 0;
            std::uint64_t coReadWithMainDepth = 0;
            std::uint64_t psSlots = 0;
            std::uint64_t csSlots = 0;
        };

        struct Event
        {
            std::uint64_t frame = 0;
            std::uint64_t ordinal = 0;
            EventKind kind = EventKind::RenderTargets;
            char stage = '-';
            UINT slot = 0;
            std::uintptr_t shader = 0;
            std::uintptr_t resource = 0;
            UINT width = 0;
            UINT height = 0;
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
            UINT bindFlags = 0;
            UINT viewportWidth = 0;
            UINT viewportHeight = 0;
        };

        struct PreviewRuntime
        {
            ID3D11Device* device = nullptr;
            ID3D11DeviceContext* deferred = nullptr;
            ID3D11VertexShader* vertexShader = nullptr;
            ID3D11PixelShader* pixelShader = nullptr;
            ID3D11SamplerState* sampler = nullptr;
            ID3D11Buffer* parameters = nullptr;
        };

        struct PreviewParameters
        {
            std::uint32_t mode = 0;
            float scale = 1.0f;
            float padding[2]{};
        };

        struct PreviewStats
        {
            std::uint64_t pixels = 0;
            std::array<std::uint64_t, 4> sums{};
            std::array<std::uint64_t, 4> nonzero{};
            std::array<std::uint8_t, 4> minimum{
                255, 255, 255, 255
            };
            std::array<std::uint8_t, 4> maximum{};
        };

        std::mutex g_mutex;
        std::unordered_map<std::uintptr_t, ResourceStats> g_resources;
        std::vector<Event> g_events;

        std::array<std::uintptr_t, kMaxSlots> g_psResources{};
        std::array<std::uintptr_t, kMaxSlots> g_csResources{};

        std::uintptr_t g_currentPixelShader = 0;
        std::uintptr_t g_currentComputeShader = 0;

        UINT g_viewportWidth = 0;
        UINT g_viewportHeight = 0;
        UINT g_screenWidth = 0;
        UINT g_screenHeight = 0;

        std::uintptr_t g_knownDepth = 0;
        std::uintptr_t g_knownSceneColor = 0;
        std::uintptr_t g_knownMotion = 0;
        std::uintptr_t g_knownNormal = 0;
        std::uintptr_t g_knownAux = 0;

        std::uint64_t g_stableGameplay = 0;
        bool g_traceActive = false;
        std::uint64_t g_traceStartFrame = 0;
        std::uint64_t g_traceEndFrame = 0;
        bool g_complete = false;

        bool g_manualCaptureRequested = false;
        bool g_manualTrace = false;
        std::uint32_t g_manualCaptureIndex = 0;
        bool g_manualTestTriggered = false;

        PreviewRuntime g_preview{};

        bool FastTestMode()
        {
            wchar_t value[8]{};

            return
                GetEnvironmentVariableW(
                    L"GZFX_TEST_SEMANTIC_CAPTURE",
                    value,
                    static_cast<DWORD>(
                        std::size(value))) > 0;
        }

        bool ManualTestMode()
        {
            wchar_t value[8]{};

            return
                GetEnvironmentVariableW(
                    L"GZFX_TEST_SEMANTIC_MANUAL_CAPTURE",
                    value,
                    static_cast<DWORD>(
                        std::size(value))) > 0;
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
                        reinterpret_cast<void**>(
                            &identity))) ||
                identity == nullptr)
            {
                return
                    reinterpret_cast<std::uintptr_t>(
                        value);
            }

            const auto id =
                reinterpret_cast<std::uintptr_t>(
                    identity);

            identity->Release();
            return id;
        }

        std::string Ptr(const std::uintptr_t value)
        {
            std::ostringstream stream;
            stream
                << "0x"
                << std::hex
                << std::uppercase
                << value;

            return stream.str();
        }

        std::uintptr_t TrackTexture(
            ID3D11View* view,
            const std::uint64_t frame)
        {
            if (view == nullptr)
            {
                return 0;
            }

            ID3D11Resource* resource = nullptr;
            view->GetResource(&resource);

            if (resource == nullptr)
            {
                return 0;
            }

            ID3D11Texture2D* texture = nullptr;

            const HRESULT query =
                resource->QueryInterface(
                    __uuidof(ID3D11Texture2D),
                    reinterpret_cast<void**>(
                        &texture));

            resource->Release();

            if (FAILED(query) ||
                texture == nullptr)
            {
                return 0;
            }

            const auto id =
                CanonicalId(texture);

            auto it =
                g_resources.find(id);

            if (it == g_resources.end())
            {
                ResourceStats stats{};
                stats.texture = texture;
                stats.texture->AddRef();
                stats.texture->GetDesc(
                    &stats.desc);
                stats.firstFrame = frame;
                stats.lastFrame = frame;

                it =
                    g_resources.emplace(
                        id,
                        stats).first;
            }
            else
            {
                it->second.lastFrame = frame;
            }

            texture->Release();
            return id;
        }

        void AppendEvent(
            const std::uint64_t frame,
            const std::uint64_t ordinal,
            const EventKind kind,
            const char stage,
            const UINT slot,
            const std::uintptr_t shader,
            const std::uintptr_t resource)
        {
            if (!g_traceActive ||
                g_events.size() >= kMaxEvents ||
                frame < g_traceStartFrame ||
                frame > g_traceEndFrame)
            {
                return;
            }

            Event event{};
            event.frame = frame;
            event.ordinal = ordinal;
            event.kind = kind;
            event.stage = stage;
            event.slot = slot;
            event.shader = shader;
            event.resource = resource;
            event.viewportWidth =
                g_viewportWidth;
            event.viewportHeight =
                g_viewportHeight;

            const auto found =
                g_resources.find(resource);

            if (found != g_resources.end())
            {
                event.width =
                    found->second.desc.Width;

                event.height =
                    found->second.desc.Height;

                event.format =
                    found->second.desc.Format;

                event.bindFlags =
                    found->second.desc.BindFlags;
            }

            g_events.push_back(event);
        }

        bool ContainsKnownDepth(
            const std::array<std::uintptr_t, kMaxSlots>& resources)
        {
            return
                g_knownDepth != 0 &&
                std::find(
                    resources.begin(),
                    resources.end(),
                    g_knownDepth) !=
                resources.end();
        }

        void CreditDepthCoReads(
            const std::array<std::uintptr_t, kMaxSlots>& resources)
        {
            if (!ContainsKnownDepth(resources))
            {
                return;
            }

            for (const auto id : resources)
            {
                if (id == 0 ||
                    id == g_knownDepth)
                {
                    continue;
                }

                auto found =
                    g_resources.find(id);

                if (found != g_resources.end())
                {
                    ++found->second.coReadWithMainDepth;
                }
            }
        }

        DXGI_FORMAT SrvFormat(
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

            case DXGI_FORMAT_R8G8B8A8_TYPELESS:
                return DXGI_FORMAT_R8G8B8A8_UNORM;

            default:
                return format;
            }
        }

        bool Compile(
            ID3D11Device* device,
            const char* source,
            const char* entry,
            const char* profile,
            ID3DBlob** blob)
        {
            ID3DBlob* errors = nullptr;

            const HRESULT result =
                D3DCompile(
                    source,
                    std::strlen(source),
                    "GZFXSemanticProbe",
                    nullptr,
                    nullptr,
                    entry,
                    profile,
                    D3DCOMPILE_OPTIMIZATION_LEVEL3,
                    0,
                    blob,
                    &errors);

            if (errors != nullptr)
            {
                errors->Release();
            }

            (void)device;
            return SUCCEEDED(result);
        }

        bool EnsurePreviewRuntime(
            ID3D11Device* device)
        {
            if (device == nullptr)
            {
                return false;
            }

            if (g_preview.device == device &&
                g_preview.deferred != nullptr)
            {
                return true;
            }

            const char* shaderSource =
R"(
struct V
{
    float4 p : SV_POSITION;
    float2 uv : TEXCOORD0;
};

V VSMain(uint id : SV_VertexID)
{
    float2 uv =
        float2(
            (id << 1) & 2,
            id & 2);

    V o;
    o.uv = uv;
    o.p =
        float4(
            uv * float2(2, -2) +
            float2(-1, 1),
            0,
            1);

    return o;
}

cbuffer P : register(b0)
{
    uint Mode;
    float Scale;
    float2 Pad;
};

Texture2D<float4> Source : register(t0);
SamplerState Samp : register(s0);

float3 DecodeVanillaNormal(float2 encoded)
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

    float3 n =
        float3(
            e * q,
            nz);

    return normalize(n);
}

float4 PSMain(V input) : SV_TARGET
{
    float4 value =
        Source.SampleLevel(
            Samp,
            input.uv,
            0);

    if (Mode == 1)
    {
        float3 normal =
            DecodeVanillaNormal(
                value.rg);

        return float4(
            normal * 0.5 + 0.5,
            1);
    }

    if (Mode == 2)
    {
        float2 motion =
            value.rg;

        return float4(
            saturate(
                0.5 +
                motion.x * Scale),
            saturate(
                0.5 +
                motion.y * Scale),
            saturate(
                length(motion) *
                abs(Scale)),
            1);
    }

    if (Mode == 3)
    {
        float scalar =
            pow(
                saturate(
                    abs(value.r) *
                    Scale),
                0.25);

        return
            float4(
                scalar,
                scalar,
                scalar,
                1);
    }

    if (Mode >= 4 && Mode <= 7)
    {
        float channel =
            Mode == 4 ? value.r :
            Mode == 5 ? value.g :
            Mode == 6 ? value.b :
                        value.a;

        channel = saturate(channel);

        return float4(
            channel,
            channel,
            channel,
            1);
    }

    if (Mode == 8)
    {
        return float4(
            saturate(value.rgb),
            1);
    }

    float3 color =
        max(
            value.rgb,
            0.0);

    color =
        color /
        (1.0 + color);

    color =
        pow(
            color,
            1.0 / 2.2);

    return float4(
        color,
        1);
}
)";

            ID3DBlob* vsBlob = nullptr;
            ID3DBlob* psBlob = nullptr;

            if (!Compile(
                    device,
                    shaderSource,
                    "VSMain",
                    "vs_5_0",
                    &vsBlob) ||
                !Compile(
                    device,
                    shaderSource,
                    "PSMain",
                    "ps_5_0",
                    &psBlob))
            {
                if (vsBlob) vsBlob->Release();
                if (psBlob) psBlob->Release();
                return false;
            }

            HRESULT result =
                device->CreateDeferredContext(
                    0,
                    &g_preview.deferred);

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateVertexShader(
                        vsBlob->GetBufferPointer(),
                        vsBlob->GetBufferSize(),
                        nullptr,
                        &g_preview.vertexShader);
            }

            if (SUCCEEDED(result))
            {
                result =
                    device->CreatePixelShader(
                        psBlob->GetBufferPointer(),
                        psBlob->GetBufferSize(),
                        nullptr,
                        &g_preview.pixelShader);
            }

            D3D11_SAMPLER_DESC sampler{};
            sampler.Filter =
                D3D11_FILTER_MIN_MAG_MIP_POINT;
            sampler.AddressU =
                D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.AddressV =
                D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.AddressW =
                D3D11_TEXTURE_ADDRESS_CLAMP;
            sampler.MaxLOD =
                D3D11_FLOAT32_MAX;

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateSamplerState(
                        &sampler,
                        &g_preview.sampler);
            }

            D3D11_BUFFER_DESC buffer{};
            buffer.ByteWidth =
                sizeof(PreviewParameters);
            buffer.Usage =
                D3D11_USAGE_DEFAULT;
            buffer.BindFlags =
                D3D11_BIND_CONSTANT_BUFFER;

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateBuffer(
                        &buffer,
                        nullptr,
                        &g_preview.parameters);
            }

            vsBlob->Release();
            psBlob->Release();

            if (FAILED(result))
            {
                return false;
            }

            g_preview.device = device;
            device->AddRef();
            return true;
        }

        bool WriteBmp(
            const fs::path& path,
            ID3D11Texture2D* staging,
            ID3D11DeviceContext* context,
            const UINT width,
            const UINT height,
            PreviewStats* stats = nullptr)
        {
            D3D11_MAPPED_SUBRESOURCE mapped{};

            if (FAILED(
                    context->Map(
                        staging,
                        0,
                        D3D11_MAP_READ,
                        0,
                        &mapped)))
            {
                return false;
            }

            BITMAPFILEHEADER fileHeader{};
            BITMAPINFOHEADER infoHeader{};

            const DWORD bytes =
                width *
                height *
                4;

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
            infoHeader.biCompression =
                BI_RGB;
            infoHeader.biSizeImage = bytes;

            std::ofstream out(
                path,
                std::ios::binary |
                    std::ios::trunc);

            if (!out)
            {
                context->Unmap(
                    staging,
                    0);

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
                row(
                    static_cast<std::size_t>(
                        width) *
                    4);

            for (UINT y = 0;
                 y < height;
                 ++y)
            {
                const UINT sourceY =
                    height - 1 - y;

                const auto* source =
                    static_cast<
                        const std::uint8_t*>(
                            mapped.pData) +
                    static_cast<std::size_t>(
                        sourceY) *
                        mapped.RowPitch;

                for (UINT x = 0;
                     x < width;
                     ++x)
                {
                    const std::uint8_t rgba[4] =
                    {
                        source[x * 4 + 0],
                        source[x * 4 + 1],
                        source[x * 4 + 2],
                        source[x * 4 + 3]
                    };

                    if (stats != nullptr)
                    {
                        ++stats->pixels;

                        for (std::size_t channel = 0;
                             channel < 4;
                             ++channel)
                        {
                            stats->sums[channel] +=
                                rgba[channel];

                            if (rgba[channel] != 0)
                            {
                                ++stats->nonzero[channel];
                            }

                            stats->minimum[channel] =
                                (std::min)(
                                    stats->minimum[channel],
                                    rgba[channel]);

                            stats->maximum[channel] =
                                (std::max)(
                                    stats->maximum[channel],
                                    rgba[channel]);
                        }
                    }

                    row[x * 4 + 0] = rgba[2];
                    row[x * 4 + 1] = rgba[1];
                    row[x * 4 + 2] = rgba[0];
                    row[x * 4 + 3] = rgba[3];
                }

                out.write(
                    reinterpret_cast<
                        const char*>(
                            row.data()),
                    static_cast<
                        std::streamsize>(
                            row.size()));
            }

            context->Unmap(
                staging,
                0);

            return static_cast<bool>(out);
        }

        bool RenderPreview(
            ID3D11Device* device,
            ID3D11DeviceContext* context,
            ResourceStats& resource,
            const std::uint32_t mode,
            const float scale,
            const fs::path& path,
            PreviewStats* stats = nullptr)
        {
            if (!EnsurePreviewRuntime(device) ||
                resource.texture == nullptr)
            {
                return false;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format =
                SrvFormat(
                    resource.desc.Format);

            if (srvDesc.Format ==
                    DXGI_FORMAT_UNKNOWN ||
                (resource.desc.BindFlags &
                    D3D11_BIND_SHADER_RESOURCE) == 0)
            {
                return false;
            }

            srvDesc.ViewDimension =
                D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;

            ID3D11ShaderResourceView* srv = nullptr;

            if (FAILED(
                    device->CreateShaderResourceView(
                        resource.texture,
                        &srvDesc,
                        &srv)) ||
                srv == nullptr)
            {
                return false;
            }

            constexpr UINT width = 640;
            constexpr UINT height = 360;

            D3D11_TEXTURE2D_DESC outputDesc{};
            outputDesc.Width = width;
            outputDesc.Height = height;
            outputDesc.MipLevels = 1;
            outputDesc.ArraySize = 1;
            outputDesc.Format =
                DXGI_FORMAT_R8G8B8A8_UNORM;
            outputDesc.SampleDesc.Count = 1;
            outputDesc.Usage =
                D3D11_USAGE_DEFAULT;
            outputDesc.BindFlags =
                D3D11_BIND_RENDER_TARGET;

            ID3D11Texture2D* output = nullptr;
            ID3D11RenderTargetView* rtv = nullptr;

            HRESULT result =
                device->CreateTexture2D(
                    &outputDesc,
                    nullptr,
                    &output);

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateRenderTargetView(
                        output,
                        nullptr,
                        &rtv);
            }

            D3D11_TEXTURE2D_DESC stagingDesc =
                outputDesc;
            stagingDesc.Usage =
                D3D11_USAGE_STAGING;
            stagingDesc.BindFlags = 0;
            stagingDesc.CPUAccessFlags =
                D3D11_CPU_ACCESS_READ;

            ID3D11Texture2D* staging = nullptr;

            if (SUCCEEDED(result))
            {
                result =
                    device->CreateTexture2D(
                        &stagingDesc,
                        nullptr,
                        &staging);
            }

            if (SUCCEEDED(result))
            {
                PreviewParameters parameters{};
                parameters.mode = mode;
                parameters.scale = scale;

                g_preview.deferred->ClearState();
                g_preview.deferred->UpdateSubresource(
                    g_preview.parameters,
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

                g_preview.deferred->OMSetRenderTargets(
                    1,
                    &rtv,
                    nullptr);
                g_preview.deferred->RSSetViewports(
                    1,
                    &viewport);
                g_preview.deferred->IASetInputLayout(
                    nullptr);
                g_preview.deferred->IASetPrimitiveTopology(
                    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                g_preview.deferred->VSSetShader(
                    g_preview.vertexShader,
                    nullptr,
                    0);
                g_preview.deferred->PSSetShader(
                    g_preview.pixelShader,
                    nullptr,
                    0);
                g_preview.deferred->PSSetConstantBuffers(
                    0,
                    1,
                    &g_preview.parameters);
                g_preview.deferred->PSSetShaderResources(
                    0,
                    1,
                    &srv);
                g_preview.deferred->PSSetSamplers(
                    0,
                    1,
                    &g_preview.sampler);
                g_preview.deferred->Draw(
                    3,
                    0);

                ID3D11ShaderResourceView*
                    nullSrv = nullptr;
                g_preview.deferred->PSSetShaderResources(
                    0,
                    1,
                    &nullSrv);
                g_preview.deferred->OMSetRenderTargets(
                    0,
                    nullptr,
                    nullptr);

                ID3D11CommandList* commands = nullptr;

                result =
                    g_preview.deferred->FinishCommandList(
                        FALSE,
                        &commands);

                if (SUCCEEDED(result) &&
                    commands != nullptr)
                {
                    context->ExecuteCommandList(
                        commands,
                        TRUE);

                    context->CopyResource(
                        staging,
                        output);

                    commands->Release();

                    result =
                        WriteBmp(
                            path,
                            staging,
                            context,
                            width,
                            height,
                            stats)
                        ? S_OK
                        : E_FAIL;
                }
            }

            if (staging) staging->Release();
            if (rtv) rtv->Release();
            if (output) output->Release();
            srv->Release();

            return SUCCEEDED(result);
        }

        void WriteBindings(
            const std::wstring& suffix)
        {
            const fs::path root =
                gzfx::paths::DiagnosticsRoot();

            if (root.empty())
            {
                return;
            }

            std::ofstream out(
                root /
                    (
                        std::wstring(
                            L"semantic_bindings") +
                        suffix +
                        L".csv"
                    ),
                std::ios::out |
                    std::ios::trunc);

            if (!out)
            {
                return;
            }

            out
                << "frame,ordinal,kind,stage,slot,"
                   "shader,resource,width,height,"
                   "format,bind_flags,viewport_width,"
                   "viewport_height\n";

            for (const auto& event : g_events)
            {
                out
                    << event.frame
                    << ","
                    << event.ordinal
                    << ","
                    << static_cast<
                        std::uint32_t>(
                            event.kind)
                    << ","
                    << event.stage
                    << ","
                    << event.slot
                    << ","
                    << Ptr(event.shader)
                    << ","
                    << Ptr(event.resource)
                    << ","
                    << event.width
                    << ","
                    << event.height
                    << ","
                    << static_cast<
                        std::uint32_t>(
                            event.format)
                    << ",0x"
                    << std::hex
                    << event.bindFlags
                    << std::dec
                    << ","
                    << event.viewportWidth
                    << ","
                    << event.viewportHeight
                    << "\n";
            }
        }

        std::string FormatName(
            const DXGI_FORMAT format)
        {
            std::ostringstream stream;
            stream
                << static_cast<
                    std::uint32_t>(
                        format);

            return stream.str();
        }

        void CaptureKnownSurfaces(
            ID3D11Device* device,
            ID3D11DeviceContext* context,
            const std::wstring& suffix)
        {
            const fs::path root =
                gzfx::paths::DiagnosticsRoot();

            if (root.empty())
            {
                return;
            }

            std::ofstream report(
                root /
                    (
                        std::wstring(
                            L"known_surfaces") +
                        suffix +
                        L".txt"
                    ),
                std::ios::out |
                    std::ios::trunc);

            if (!report)
            {
                return;
            }

            report
                << "GZFX known surface channel evidence\n"
                << "version="
                << gzfx::kVersion
                << "\n"
                << "scene_color="
                << Ptr(g_knownSceneColor)
                << "\n"
                << "depth="
                << Ptr(g_knownDepth)
                << "\n"
                << "motion="
                << Ptr(g_knownMotion)
                << "\n"
                << "normal="
                << Ptr(g_knownNormal)
                << "\n"
                << "aux="
                << Ptr(g_knownAux)
                << "\n\n";

            const auto capture =
                [&](const char* label,
                    const std::uintptr_t id,
                    const std::uint32_t mode,
                    const float scale)
                {
                    report
                        << "surface="
                        << label
                        << " id="
                        << Ptr(id);

                    const auto found =
                        g_resources.find(id);

                    if (id == 0 ||
                        found == g_resources.end() ||
                        found->second.texture ==
                            nullptr)
                    {
                        report
                            << " status=missing\n";

                        return;
                    }

                    const auto& desc =
                        found->second.desc;

                    std::wstring wideLabel;

                    {
                        const int needed =
                            MultiByteToWideChar(
                                CP_UTF8,
                                0,
                                label,
                                -1,
                                nullptr,
                                0);

                        if (needed > 1)
                        {
                            wideLabel.resize(
                                static_cast<
                                    std::size_t>(
                                        needed));

                            MultiByteToWideChar(
                                CP_UTF8,
                                0,
                                label,
                                -1,
                                wideLabel.data(),
                                needed);

                            if (!wideLabel.empty() &&
                                wideLabel.back() ==
                                    L'\0')
                            {
                                wideLabel.pop_back();
                            }
                        }
                    }

                    const fs::path path =
                        root /
                        (
                            std::wstring(
                                L"known_surface") +
                            suffix +
                            L"_" +
                            wideLabel +
                            L".bmp"
                        );

                    PreviewStats stats{};

                    const bool wrote =
                        RenderPreview(
                            device,
                            context,
                            found->second,
                            mode,
                            scale,
                            path,
                            &stats);

                    report
                        << " status="
                        << (wrote
                            ? "ok"
                            : "failed")
                        << " size="
                        << desc.Width
                        << "x"
                        << desc.Height
                        << " format="
                        << static_cast<
                            std::uint32_t>(
                                desc.Format)
                        << " mode="
                        << mode;

                    if (wrote &&
                        stats.pixels != 0)
                    {
                        report
                            << std::fixed
                            << std::setprecision(6);

                        for (std::size_t channel = 0;
                             channel < 4;
                             ++channel)
                        {
                            const double mean =
                                static_cast<double>(
                                    stats.sums[channel]) /
                                (
                                    static_cast<double>(
                                        stats.pixels) *
                                    255.0
                                );

                            const double fraction =
                                static_cast<double>(
                                    stats.nonzero[channel]) /
                                static_cast<double>(
                                    stats.pixels);

                            report
                                << " c"
                                << channel
                                << "_mean="
                                << mean
                                << " c"
                                << channel
                                << "_nonzero="
                                << fraction
                                << " c"
                                << channel
                                << "_min="
                                << static_cast<unsigned>(
                                    stats.minimum[channel])
                                << " c"
                                << channel
                                << "_max="
                                << static_cast<unsigned>(
                                    stats.maximum[channel]);
                        }
                    }

                    report << "\n";
                };

            capture(
                "scene_color",
                g_knownSceneColor,
                0,
                1.0f);

            capture(
                "main_depth",
                g_knownDepth,
                3,
                1.0f);

            capture(
                "motion",
                g_knownMotion,
                2,
                24.0f);

            capture(
                "normal_decoded",
                g_knownNormal,
                1,
                1.0f);

            capture(
                "normal_raw",
                g_knownNormal,
                8,
                1.0f);

            capture(
                "normal_r",
                g_knownNormal,
                4,
                1.0f);

            capture(
                "normal_g",
                g_knownNormal,
                5,
                1.0f);

            capture(
                "normal_b",
                g_knownNormal,
                6,
                1.0f);

            capture(
                "normal_a",
                g_knownNormal,
                7,
                1.0f);

            capture(
                "aux_raw",
                g_knownAux,
                8,
                1.0f);

            capture(
                "aux_r",
                g_knownAux,
                4,
                1.0f);

            capture(
                "aux_g",
                g_knownAux,
                5,
                1.0f);
        }

        void CaptureLandmarkSurfaces(
            ID3D11Device* device,
            ID3D11DeviceContext* context,
            const std::wstring& suffix)
        {
            const fs::path root =
                gzfx::paths::DiagnosticsRoot();

            if (root.empty())
            {
                return;
            }

            const auto landmarks =
                pipeline_cartography::
                    GetCaptureLandmarks();

            std::ofstream report(
                root /
                    (
                        std::wstring(
                            L"atlas_surfaces") +
                        suffix +
                        L".txt"
                    ),
                std::ios::out |
                    std::ios::trunc);

            if (!report)
            {
                return;
            }

            report
                << "GZFX structural landmark surface atlas\n"
                << "version="
                << gzfx::kVersion
                << "\n"
                << "landmarks="
                << landmarks.size()
                << "\n\n";

            std::map<
                std::pair<
                    std::uint32_t,
                    std::uintptr_t>,
                std::uint32_t>
                    seen;

            std::map<std::uint32_t, std::uint32_t>
                kindIndex;

            const auto writeStats =
                [&](const char* label,
                    const std::uintptr_t id,
                    const std::uint64_t passId,
                    const std::uint32_t confidence,
                    const std::uint32_t mode,
                    const float scale,
                    const fs::path& path)
                {
                    const auto found =
                        g_resources.find(id);

                    report
                        << "surface="
                        << label
                        << " id="
                        << Ptr(id)
                        << " pass="
                        << passId
                        << " confidence="
                        << confidence
                        << " mode="
                        << mode;

                    if (found == g_resources.end() ||
                        found->second.texture ==
                            nullptr)
                    {
                        report
                            << " status=not_available_as_texture2d\n";

                        return false;
                    }

                    PreviewStats stats{};

                    const bool wrote =
                        RenderPreview(
                            device,
                            context,
                            found->second,
                            mode,
                            scale,
                            path,
                            &stats);

                    report
                        << " status="
                        << (wrote
                            ? "ok"
                            : "failed")
                        << " format="
                        << static_cast<
                            std::uint32_t>(
                                found->second.desc.Format)
                        << " size="
                        << found->second.desc.Width
                        << "x"
                        << found->second.desc.Height;

                    if (wrote &&
                        stats.pixels != 0)
                    {
                        report
                            << std::fixed
                            << std::setprecision(6);

                        for (std::size_t channel = 0;
                             channel < 4;
                             ++channel)
                        {
                            const double mean =
                                static_cast<double>(
                                    stats.sums[channel]) /
                                (
                                    static_cast<double>(
                                        stats.pixels) *
                                    255.0
                                );

                            const double fraction =
                                static_cast<double>(
                                    stats.nonzero[channel]) /
                                static_cast<double>(
                                    stats.pixels);

                            report
                                << " c"
                                << channel
                                << "_mean="
                                << mean
                                << " c"
                                << channel
                                << "_nonzero="
                                << fraction
                                << " c"
                                << channel
                                << "_min="
                                << static_cast<unsigned>(
                                    stats.minimum[channel])
                                << " c"
                                << channel
                                << "_max="
                                << static_cast<unsigned>(
                                    stats.maximum[channel]);
                        }
                    }

                    report << "\n";
                    return wrote;
                };

            for (const auto& landmark :
                 landmarks)
            {
                const auto kindValue =
                    static_cast<std::uint32_t>(
                        landmark.kind);

                const auto key =
                    std::make_pair(
                        kindValue,
                        landmark.resource);

                if (!seen.emplace(
                        key,
                        1).second)
                {
                    continue;
                }

                const char* label =
                    pipeline_cartography::
                        LandmarkName(
                            landmark.kind);

                const auto found =
                    g_resources.find(
                        landmark.resource);

                if (found == g_resources.end() ||
                    found->second.texture ==
                        nullptr)
                {
                    report
                        << "surface="
                        << label
                        << " id="
                        << Ptr(
                            landmark.resource)
                        << " pass="
                        << landmark.passId
                        << " confidence="
                        << landmark.confidence
                        << " status=not_available_as_texture2d\n";

                    continue;
                }

                const auto format =
                    found->second.desc.Format;

                std::uint32_t mode = 0;
                float scale = 1.0f;

                if (format ==
                        DXGI_FORMAT_R32_FLOAT ||
                    format ==
                        DXGI_FORMAT_R16_FLOAT ||
                    format ==
                        DXGI_FORMAT_R16_UNORM ||
                    format ==
                        DXGI_FORMAT_R24G8_TYPELESS ||
                    format ==
                        DXGI_FORMAT_R16_TYPELESS)
                {
                    mode = 3;
                }
                else if (
                    format ==
                        DXGI_FORMAT_R8G8B8A8_UNORM ||
                    format ==
                        DXGI_FORMAT_R8G8_UNORM)
                {
                    mode = 8;
                }

                const std::uint32_t index =
                    ++kindIndex[kindValue];

                std::wostringstream base;
                base
                    << L"atlas_surface"
                    << suffix
                    << L"_"
                    << label
                    << L"_"
                    << std::setw(2)
                    << std::setfill(L'0')
                    << index;

                const fs::path combined =
                    root /
                    (
                        base.str() +
                        L".bmp"
                    );

                writeStats(
                    label,
                    landmark.resource,
                    landmark.passId,
                    landmark.confidence,
                    mode,
                    scale,
                    combined);

                const bool splitChannels =
                    format ==
                        DXGI_FORMAT_R8G8B8A8_UNORM &&
                    (
                        landmark.kind ==
                            pipeline_cartography::
                                LandmarkKind::ShadowMask ||
                        landmark.kind ==
                            pipeline_cartography::
                                LandmarkKind::ReflectionMask ||
                        landmark.kind ==
                            pipeline_cartography::
                                LandmarkKind::SaoRawAo ||
                        landmark.kind ==
                            pipeline_cartography::
                                LandmarkKind::SaoBlurred
                    );

                if (!splitChannels)
                {
                    continue;
                }

                const wchar_t channelNames[4] =
                {
                    L'r', L'g', L'b', L'a'
                };

                for (std::uint32_t channel = 0;
                     channel < 4;
                     ++channel)
                {
                    const fs::path channelPath =
                        root /
                        (
                            base.str() +
                            L"_" +
                            std::wstring(
                                1,
                                channelNames[channel]) +
                            L".bmp"
                        );

                    const char channelChars[4] =
                    {
                        'r', 'g', 'b', 'a'
                    };

                    std::string channelLabel =
                        std::string(label) +
                        "_" +
                        channelChars[channel];

                    // Explicit channel mode mapping is R=4,G=5,B=6,A=7.
                    writeStats(
                        channelLabel.c_str(),
                        landmark.resource,
                        landmark.passId,
                        landmark.confidence,
                        4u + channel,
                        1.0f,
                        channelPath);
                }
            }
        }

        void CaptureCandidates(
            ID3D11Device* device,
            ID3D11DeviceContext* context,
            const std::wstring& suffix)
        {
            const fs::path root =
                gzfx::paths::DiagnosticsRoot();

            if (root.empty())
            {
                return;
            }

            std::vector<std::pair<
                std::uintptr_t,
                ResourceStats*>> candidates;

            for (auto& [id, stats] :
                 g_resources)
            {
                if (stats.texture == nullptr ||
                    stats.desc.Width == 0 ||
                    stats.desc.Height == 0)
                {
                    continue;
                }

                const bool fullResolution =
                    stats.desc.Width ==
                        g_screenWidth &&
                    stats.desc.Height ==
                        g_screenHeight;

                const bool shadowLike =
                    stats.dsvWrites > 0 &&
                    stats.desc.Width >= 512 &&
                    stats.desc.Height >= 512;

                const bool imageSpaceLike =
                    stats.psReads > 0 ||
                    stats.csReads > 0 ||
                    stats.rtvWrites > 0 ||
                    stats.csUavWrites > 0;

                if ((fullResolution &&
                     imageSpaceLike) ||
                    shadowLike)
                {
                    candidates.emplace_back(
                        id,
                        &stats);
                }
            }

            std::sort(
                candidates.begin(),
                candidates.end(),
                [](const auto& left,
                   const auto& right)
                {
                    const auto score =
                        [](const ResourceStats* stats)
                        {
                            return
                                stats->rtvWrites * 4 +
                                stats->csUavWrites * 4 +
                                stats->psReads * 2 +
                                stats->csReads * 2 +
                                stats->coReadWithMainDepth * 8 +
                                stats->dsvWrites;
                        };

                    return
                        score(left.second) >
                        score(right.second);
                });

            std::ofstream report(
                root /
                    (
                        std::wstring(
                            L"renderer_semantics") +
                        suffix +
                        L".txt"
                    ),
                std::ios::out |
                    std::ios::trunc);

            if (!report)
            {
                return;
            }

            report
                << "GZFX renderer semantic map\n"
                << "version="
                << gzfx::kVersion
                << "\n"
                << "screen="
                << g_screenWidth
                << "x"
                << g_screenHeight
                << "\n"
                << "known_depth="
                << Ptr(g_knownDepth)
                << "\n"
                << "known_scene_color="
                << Ptr(g_knownSceneColor)
                << "\n"
                << "trace_frames="
                << g_traceStartFrame
                << "-"
                << g_traceEndFrame
                << "\n"
                << "events="
                << g_events.size()
                << "\n"
                << "resources="
                << g_resources.size()
                << "\n\n";

            std::size_t captured = 0;

            for (std::size_t i = 0;
                 i < candidates.size();
                 ++i)
            {
                const auto id =
                    candidates[i].first;

                auto* stats =
                    candidates[i].second;

                report
                    << "candidate="
                    << (i + 1)
                    << " id="
                    << Ptr(id)
                    << " size="
                    << stats->desc.Width
                    << "x"
                    << stats->desc.Height
                    << " format="
                    << FormatName(
                        stats->desc.Format)
                    << " bind_flags=0x"
                    << std::hex
                    << stats->desc.BindFlags
                    << std::dec
                    << " rtv_writes="
                    << stats->rtvWrites
                    << " dsv_writes="
                    << stats->dsvWrites
                    << " ps_reads="
                    << stats->psReads
                    << " cs_reads="
                    << stats->csReads
                    << " cs_uav_writes="
                    << stats->csUavWrites
                    << " co_read_with_main_depth="
                    << stats->coReadWithMainDepth
                    << " ps_slots=0x"
                    << std::hex
                    << stats->psSlots
                    << " cs_slots=0x"
                    << stats->csSlots
                    << std::dec
                    << "\n";

                if (captured >=
                    kMaxPreviewCandidates ||
                    (stats->desc.BindFlags &
                        D3D11_BIND_SHADER_RESOURCE) ==
                        0)
                {
                    continue;
                }

                std::wostringstream base;
                base
                    << L"semantic"
                    << suffix
                    << L"_candidate_"
                    << std::setw(2)
                    << std::setfill(L'0')
                    << (captured + 1)
                    << L"_f"
                    << static_cast<
                        std::uint32_t>(
                            stats->desc.Format);

                std::uint32_t mode = 0;
                float scale = 1.0f;

                if (stats->desc.Format ==
                    DXGI_FORMAT_R8G8B8A8_UNORM)
                {
                    mode = 1;
                }
                else if (
                    stats->desc.Format ==
                    DXGI_FORMAT_R16G16_FLOAT)
                {
                    mode = 2;
                    scale = 24.0f;
                }
                else if (
                    stats->desc.Format ==
                        DXGI_FORMAT_R32_FLOAT ||
                    stats->desc.Format ==
                        DXGI_FORMAT_R16_FLOAT ||
                    stats->desc.Format ==
                        DXGI_FORMAT_R16_UNORM ||
                    stats->desc.Format ==
                        DXGI_FORMAT_R24G8_TYPELESS ||
                    stats->desc.Format ==
                        DXGI_FORMAT_R16_TYPELESS)
                {
                    mode = 3;
                    scale = 1.0f;
                }

                const bool wrote =
                    RenderPreview(
                        device,
                        context,
                        *stats,
                        mode,
                        scale,
                        root /
                            (
                                base.str() +
                                L".bmp"
                            ));

                report
                    << "  preview="
                    << (wrote
                        ? "ok"
                        : "failed")
                    << " mode="
                    << mode
                    << "\n";

                ++captured;
            }

            report
                << "\nsemantic_hints:\n"
                << "- Full-resolution RGBA8 candidates with high depth-co-read counts are strong stock normal/TAA/SSR candidates.\n"
                << "- Full-resolution R16G16_FLOAT candidates are strong motion-vector candidates.\n"
                << "- Full-resolution R32_FLOAT/R16_FLOAT candidates may be scalar depth/mask/image-space intermediates.\n"
                << "- 4096/512 depth-stencil candidates are likely directional/local shadow resources.\n"
                << "- semantic_bindings.csv preserves shader/slot/output chronology for SSAO, lighting, post, water, and exposure reconstruction.\n";
        }

        void BeginTrace(
            const std::uint64_t frame,
            IDXGISwapChain* swapChain,
            ID3D11DeviceContext* context)
        {
            resource_intelligence::LiveInputs inputs{};

            std::uintptr_t knownMotion = 0;
            std::uintptr_t knownNormal = 0;
            std::uintptr_t knownAux = 0;

            if (resource_intelligence::
                    AcquireLiveInputs(
                        swapChain,
                        inputs))
            {
                g_knownDepth =
                    CanonicalId(
                        inputs.depthTexture);

                g_knownSceneColor =
                    CanonicalId(
                        inputs.sceneColorTexture);

                knownMotion =
                    CanonicalId(
                        inputs.motionVectorTexture);

                knownNormal =
                    CanonicalId(
                        inputs.nativeNormalTexture);

                knownAux =
                    CanonicalId(
                        inputs.nativeAuxTexture);

                g_knownMotion = knownMotion;
                g_knownNormal = knownNormal;
                g_knownAux = knownAux;

                resource_intelligence::
                    ReleaseLiveInputs(
                        inputs);
            }

            g_events.clear();
            g_traceStartFrame =
                frame + 1;
            g_traceEndFrame =
                g_traceStartFrame +
                kTraceFrames - 1;

            pipeline_cartography::BeginCapture(
                g_traceStartFrame,
                g_traceEndFrame,
                context,
                g_knownSceneColor,
                g_knownDepth,
                knownMotion,
                knownNormal,
                knownAux);

            g_traceActive = true;
        }
    }

    void SetPixelShader(
        ID3D11PixelShader* shader) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            g_currentPixelShader =
                reinterpret_cast<
                    std::uintptr_t>(
                        shader);
        }
        catch (...)
        {
        }
    }

    void SetComputeShader(
        ID3D11ComputeShader* shader) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            g_currentComputeShader =
                reinterpret_cast<
                    std::uintptr_t>(
                        shader);
        }
        catch (...)
        {
        }
    }

    void SetViewport(
        const UINT count,
        const D3D11_VIEWPORT* viewports) noexcept
    {
        try
        {
            if (count == 0 ||
                viewports == nullptr)
            {
                return;
            }

            std::scoped_lock lock(
                g_mutex);

            g_viewportWidth =
                static_cast<UINT>(
                    viewports[0].Width);

            g_viewportHeight =
                static_cast<UINT>(
                    viewports[0].Height);
        }
        catch (...)
        {
        }
    }

    void ObserveRenderTargets(
        const std::uint64_t frame,
        const std::uint64_t ordinal,
        const UINT count,
        ID3D11RenderTargetView* const* renderTargets,
        ID3D11DepthStencilView* depthTarget) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            for (UINT i = 0;
                 i < count;
                 ++i)
            {
                const auto id =
                    TrackTexture(
                        renderTargets
                            ? renderTargets[i]
                            : nullptr,
                        frame);

                if (id != 0)
                {
                    ++g_resources[id].
                        rtvWrites;

                    AppendEvent(
                        frame,
                        ordinal,
                        EventKind::RenderTargets,
                        'O',
                        i,
                        g_currentPixelShader,
                        id);
                }
            }

            const auto depthId =
                TrackTexture(
                    depthTarget,
                    frame);

            if (depthId != 0)
            {
                ++g_resources[depthId].
                    dsvWrites;

                AppendEvent(
                    frame,
                    ordinal,
                    EventKind::RenderTargets,
                    'D',
                    0,
                    g_currentPixelShader,
                    depthId);
            }
        }
        catch (...)
        {
        }
    }

    void ObservePixelResources(
        const std::uint64_t frame,
        const std::uint64_t ordinal,
        const UINT startSlot,
        const UINT count,
        ID3D11ShaderResourceView* const* resources) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            for (UINT i = 0;
                 i < count &&
                 startSlot + i <
                    kMaxSlots;
                 ++i)
            {
                const UINT slot =
                    startSlot + i;

                const auto id =
                    TrackTexture(
                        resources
                            ? resources[i]
                            : nullptr,
                        frame);

                g_psResources[slot] = id;

                if (id != 0)
                {
                    auto& stats =
                        g_resources[id];

                    ++stats.psReads;

                    if (slot < 64)
                    {
                        stats.psSlots |=
                            1ull << slot;
                    }

                    AppendEvent(
                        frame,
                        ordinal,
                        EventKind::PixelResources,
                        'P',
                        slot,
                        g_currentPixelShader,
                        id);
                }
            }

            CreditDepthCoReads(
                g_psResources);
        }
        catch (...)
        {
        }
    }

    void ObserveComputeResources(
        const std::uint64_t frame,
        const std::uint64_t ordinal,
        const UINT startSlot,
        const UINT count,
        ID3D11ShaderResourceView* const* resources) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            for (UINT i = 0;
                 i < count &&
                 startSlot + i <
                    kMaxSlots;
                 ++i)
            {
                const UINT slot =
                    startSlot + i;

                const auto id =
                    TrackTexture(
                        resources
                            ? resources[i]
                            : nullptr,
                        frame);

                g_csResources[slot] = id;

                if (id != 0)
                {
                    auto& stats =
                        g_resources[id];

                    ++stats.csReads;

                    if (slot < 64)
                    {
                        stats.csSlots |=
                            1ull << slot;
                    }

                    AppendEvent(
                        frame,
                        ordinal,
                        EventKind::ComputeResources,
                        'C',
                        slot,
                        g_currentComputeShader,
                        id);
                }
            }

            CreditDepthCoReads(
                g_csResources);
        }
        catch (...)
        {
        }
    }

    void ObserveComputeUavs(
        const std::uint64_t frame,
        const std::uint64_t ordinal,
        const UINT startSlot,
        const UINT count,
        ID3D11UnorderedAccessView* const* resources) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            for (UINT i = 0;
                 i < count;
                 ++i)
            {
                const auto id =
                    TrackTexture(
                        resources
                            ? resources[i]
                            : nullptr,
                        frame);

                if (id != 0)
                {
                    ++g_resources[id].
                        csUavWrites;

                    AppendEvent(
                        frame,
                        ordinal,
                        EventKind::ComputeUavs,
                        'U',
                        startSlot + i,
                        g_currentComputeShader,
                        id);
                }
            }
        }
        catch (...)
        {
        }
    }

    void OnPresent(
        const std::uint64_t frame,
        const std::uint64_t drawIndexedActivity,
        IDXGISwapChain* swapChain,
        ID3D11Device* device,
        ID3D11DeviceContext* context) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (swapChain == nullptr ||
                device == nullptr ||
                context == nullptr)
            {
                return;
            }

            DXGI_SWAP_CHAIN_DESC description{};

            if (SUCCEEDED(
                    swapChain->GetDesc(
                        &description)))
            {
                g_screenWidth =
                    description.BufferDesc.Width;

                g_screenHeight =
                    description.BufferDesc.Height;
            }

            const bool gameplay =
                FastTestMode() ||
                drawIndexedActivity >= 1000;

            if (gameplay)
            {
                ++g_stableGameplay;
            }
            else
            {
                g_stableGameplay = 0;
            }

            if (g_complete &&
                ManualTestMode() &&
                !g_manualTestTriggered &&
                !g_traceActive)
            {
                g_manualCaptureRequested = true;
                g_manualTestTriggered = true;
            }

            if (g_manualCaptureRequested &&
                !g_traceActive &&
                gameplay)
            {
                g_manualCaptureRequested = false;
                g_manualTrace = true;

                BeginTrace(
                    frame,
                    swapChain,
                    context);

                return;
            }

            if (!g_complete &&
                !g_traceActive &&
                g_stableGameplay >=
                    (FastTestMode()
                        ? 1
                        : kStableGameplayFrames))
            {
                g_manualTrace = false;

                BeginTrace(
                    frame,
                    swapChain,
                    context);

                return;
            }

            if (g_traceActive &&
                frame >
                    g_traceEndFrame)
            {
                g_traceActive = false;

                std::wstring suffix;

                if (g_manualTrace)
                {
                    ++g_manualCaptureIndex;

                    std::wostringstream stream;
                    stream
                        << L"_manual_"
                        << std::setw(4)
                        << std::setfill(L'0')
                        << g_manualCaptureIndex;

                    suffix =
                        stream.str();
                }

                WriteBindings(
                    suffix);

                CaptureCandidates(
                    device,
                    context,
                    suffix);

                CaptureKnownSurfaces(
                    device,
                    context,
                    suffix);

                CaptureLandmarkSurfaces(
                    device,
                    context,
                    suffix);

                pipeline_cartography::FinishCapture(
                    suffix);

                if (g_manualTrace)
                {
                    g_manualTrace = false;
                }
                else
                {
                    g_complete = true;
                }
            }
        }
        catch (...)
        {
        }
    }

    void RequestManualCapture() noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (!FastTestMode())
            {
                g_manualCaptureRequested = true;
            }
        }
        catch (...)
        {
        }
    }

    bool SuppressSceneEffects() noexcept
    {
        try
        {
            // The dedicated semantic harness always asks for stock capture.
            if (FastTestMode())
            {
                return true;
            }

            // Other CI harnesses must retain normal effect behavior.
            wchar_t testRoot[4]{};

            if (GetEnvironmentVariableW(
                    L"GZFX_TEST_OUTPUT_ROOT",
                    testRoot,
                    static_cast<DWORD>(
                        std::size(testRoot))) > 0)
            {
                return false;
            }

            // Real Skyrim is stock only during the initial mapping window
            // or an explicitly requested manual semantic window.
            return
                !g_complete ||
                g_traceActive;
        }
        catch (...)
        {
            return false;
        }
    }
}
