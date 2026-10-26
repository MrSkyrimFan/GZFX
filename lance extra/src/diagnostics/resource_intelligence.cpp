#include "gzfx/render/renderer_execution_service.hpp"
#include "gzfx/diagnostics/resource_intelligence.hpp"

#include "gzfx/paths.hpp"
#include "gzfx/version.hpp"

#include <Windows.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace gzfx::diagnostics::resource_intelligence
{
    namespace fs = std::filesystem;

    namespace
    {
        struct TextureInfo
        {
            ID3D11Texture2D* texture = nullptr;
            D3D11_TEXTURE2D_DESC desc{};
            std::uint64_t rtvBinds = 0;
            std::uint64_t dsvBinds = 0;
            std::uint64_t firstFrame = 0;
            std::uint64_t lastFrame = 0;
        };

        struct BufferInfo
        {
            ID3D11Buffer* buffer = nullptr;
            D3D11_BUFFER_DESC desc{};
            std::uint64_t vsBinds = 0;
            std::uint64_t psBinds = 0;
            std::uint64_t csBinds = 0;
            std::uint64_t vsSlots = 0;
            std::uint64_t psSlots = 0;
            std::uint64_t csSlots = 0;
        };

        std::mutex g_mutex;
        std::unordered_map<std::uintptr_t, TextureInfo> g_textures;
        std::unordered_map<std::uintptr_t, BufferInfo> g_buffers;
        std::map<std::pair<std::uintptr_t, std::uintptr_t>, std::uint64_t> g_pairs;

        bool g_previewsCaptured = false;
        std::uint64_t g_lastSnapshotFrame = 0;

        UINT g_swapWidth = 0;
        UINT g_swapHeight = 0;
        std::uint64_t g_stageTraceEvents = 0;
        bool g_stageTraceEnabled = false;
        bool g_lastStageColorBound = false;
        bool g_lastStageDepthBound = false;

        std::uint64_t g_scenePairBindEvents = 0;
        std::uint64_t g_lastScenePairDraw = 0;
        bool g_preUiSignalIssued = false;

        std::uintptr_t g_nativeMotionId = 0;
        std::uintptr_t g_nativeNormalId = 0;
        std::uintptr_t g_nativeAuxId = 0;
        std::uint64_t g_nativeSurfaceFrame = 0;

        std::string Ptr(const void* pointer)
        {
            std::ostringstream stream;
            stream << "0x" << std::hex << std::uppercase
                   << reinterpret_cast<std::uintptr_t>(pointer);
            return stream.str();
        }

        bool FastTestMode()
        {
            wchar_t value[8]{};
            return GetEnvironmentVariableW(
                L"GZFX_TEST_FAST_CAPTURE",
                value,
                static_cast<DWORD>(std::size(value))) > 0;
        }

        std::uint64_t PreviewFrame()
        {
            return FastTestMode() ? 5 : 900;
        }

        bool IsSnapshotFrame(const std::uint64_t frame)
        {
            if (FastTestMode())
            {
                return frame == 4;
            }

            return frame == 600 ||
                   frame == 1200 ||
                   frame == 1800 ||
                   frame == 2400;
        }

        void WriteText(const wchar_t* name, const std::string& text)
        {
            const fs::path root = paths::DiagnosticsRoot();
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

            std::ofstream out(root / name, std::ios::out | std::ios::trunc);
            if (out)
            {
                out << text;
            }
        }

        void AppendText(
            const wchar_t* name,
            const std::string& text)
        {
            const fs::path root = paths::DiagnosticsRoot();
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
                root / name,
                std::ios::out | std::ios::app);

            if (out)
            {
                out << text;
            }
        }

        std::uintptr_t TrackView(
            ID3D11View* view,
            const std::uint64_t frame,
            const bool asRtv,
            const bool asDsv)
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
            const HRESULT query = resource->QueryInterface(
                __uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(&texture));

            resource->Release();

            if (FAILED(query) || texture == nullptr)
            {
                return 0;
            }

            const auto id = reinterpret_cast<std::uintptr_t>(texture);
            auto it = g_textures.find(id);

            if (it == g_textures.end())
            {
                TextureInfo info{};
                info.texture = texture;
                texture->GetDesc(&info.desc);
                info.firstFrame = frame;
                info.lastFrame = frame;
                it = g_textures.emplace(id, info).first;
            }
            else
            {
                texture->Release();
                it->second.lastFrame = frame;
            }

            it->second.rtvBinds += asRtv ? 1 : 0;
            it->second.dsvBinds += asDsv ? 1 : 0;
            return id;
        }

        void TrackBuffer(
            const std::uint64_t,
            const char stage,
            const UINT slot,
            ID3D11Buffer* buffer)
        {
            if (buffer == nullptr)
            {
                return;
            }

            const auto id = reinterpret_cast<std::uintptr_t>(buffer);
            auto it = g_buffers.find(id);

            if (it == g_buffers.end())
            {
                BufferInfo info{};
                info.buffer = buffer;
                buffer->AddRef();
                buffer->GetDesc(&info.desc);
                it = g_buffers.emplace(id, info).first;
            }

            const std::uint64_t bit = slot < 64 ? (1ull << slot) : 0;

            if (stage == 'V')
            {
                ++it->second.vsBinds;
                it->second.vsSlots |= bit;
            }
            else if (stage == 'P')
            {
                ++it->second.psBinds;
                it->second.psSlots |= bit;
            }
            else if (stage == 'C')
            {
                ++it->second.csBinds;
                it->second.csSlots |= bit;
            }
        }

        bool IsDepthFormat(const DXGI_FORMAT format)
        {
            return format == DXGI_FORMAT_R24G8_TYPELESS ||
                   format == DXGI_FORMAT_R32_TYPELESS ||
                   format == DXGI_FORMAT_R16_TYPELESS;
        }

        bool IsHdrColorFormat(const DXGI_FORMAT format)
        {
            return format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
                   format == DXGI_FORMAT_R11G11B10_FLOAT ||
                   format == DXGI_FORMAT_R32G32B32A32_FLOAT;
        }

        TextureInfo* SelectDepth(
            const UINT width,
            const UINT height,
            std::uintptr_t& idOut)
        {
            TextureInfo* best = nullptr;
            std::uint64_t bestScore = 0;

            for (auto& [id, info] : g_textures)
            {
                if (info.desc.Width != width ||
                    info.desc.Height != height ||
                    (info.desc.BindFlags & D3D11_BIND_DEPTH_STENCIL) == 0 ||
                    (info.desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0 ||
                    !IsDepthFormat(info.desc.Format))
                {
                    continue;
                }

                const std::uint64_t score =
                    info.dsvBinds * 1000 +
                    (info.lastFrame - info.firstFrame);

                if (score > bestScore)
                {
                    bestScore = score;
                    best = &info;
                    idOut = id;
                }
            }

            return best;
        }

        TextureInfo* SelectColor(
            const UINT width,
            const UINT height,
            const std::uintptr_t depthId,
            std::uintptr_t& idOut,
            std::uint64_t& pairCountOut)
        {
            TextureInfo* best = nullptr;
            std::uint64_t bestScore = 0;

            for (auto& [id, info] : g_textures)
            {
                if (info.desc.Width != width ||
                    info.desc.Height != height ||
                    (info.desc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0 ||
                    (info.desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) == 0)
                {
                    continue;
                }

                const auto pair = g_pairs.find({ depthId, id });
                const std::uint64_t pairCount =
                    pair == g_pairs.end() ? 0 : pair->second;

                const std::uint64_t hdrBonus =
                    IsHdrColorFormat(info.desc.Format) ? 1000000000ull : 0ull;

                const std::uint64_t score =
                    hdrBonus +
                    pairCount * 10000ull +
                    info.rtvBinds;

                if (score > bestScore)
                {
                    bestScore = score;
                    best = &info;
                    idOut = id;
                    pairCountOut = pairCount;
                }
            }

            return best;
        }

        DXGI_FORMAT DepthSrvFormat(const DXGI_FORMAT resourceFormat)
        {
            if (resourceFormat == DXGI_FORMAT_R24G8_TYPELESS)
            {
                return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            }

            if (resourceFormat == DXGI_FORMAT_R32_TYPELESS)
            {
                return DXGI_FORMAT_R32_FLOAT;
            }

            if (resourceFormat == DXGI_FORMAT_R16_TYPELESS)
            {
                return DXGI_FORMAT_R16_UNORM;
            }

            return DXGI_FORMAT_UNKNOWN;
        }

        bool CompileShader(
            const char* source,
            const char* profile,
            ID3DBlob** blob,
            std::string& error)
        {
            ID3DBlob* errors = nullptr;
            const HRESULT result = D3DCompile(
                source,
                std::strlen(source),
                "GZFXResourceIntelligence",
                nullptr,
                nullptr,
                "main",
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
                        static_cast<const char*>(errors->GetBufferPointer()),
                        errors->GetBufferSize());
                }
                else
                {
                    error = "D3DCompile failed without diagnostic text.";
                }
            }

            if (errors != nullptr)
            {
                errors->Release();
            }

            return SUCCEEDED(result);
        }

        bool WriteBmp(
            const fs::path& path,
            ID3D11Texture2D* staging,
            ID3D11DeviceContext* context,
            const UINT width,
            const UINT height)
        {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)))
            {
                return false;
            }

            BITMAPFILEHEADER fileHeader{};
            BITMAPINFOHEADER infoHeader{};

            const DWORD imageBytes = width * height * 4;
            fileHeader.bfType = 0x4D42;
            fileHeader.bfOffBits =
                sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
            fileHeader.bfSize = fileHeader.bfOffBits + imageBytes;

            infoHeader.biSize = sizeof(BITMAPINFOHEADER);
            infoHeader.biWidth = static_cast<LONG>(width);
            infoHeader.biHeight = static_cast<LONG>(height);
            infoHeader.biPlanes = 1;
            infoHeader.biBitCount = 32;
            infoHeader.biCompression = BI_RGB;
            infoHeader.biSizeImage = imageBytes;

            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                context->Unmap(staging, 0);
                return false;
            }

            out.write(
                reinterpret_cast<const char*>(&fileHeader),
                sizeof(fileHeader));
            out.write(
                reinterpret_cast<const char*>(&infoHeader),
                sizeof(infoHeader));

            std::vector<std::uint8_t> row(width * 4);

            for (UINT y = 0; y < height; ++y)
            {
                const UINT sourceY = height - 1 - y;
                const auto* source =
                    static_cast<const std::uint8_t*>(mapped.pData) +
                    static_cast<std::size_t>(sourceY) * mapped.RowPitch;

                for (UINT x = 0; x < width; ++x)
                {
                    row[x * 4 + 0] = source[x * 4 + 2];
                    row[x * 4 + 1] = source[x * 4 + 1];
                    row[x * 4 + 2] = source[x * 4 + 0];
                    row[x * 4 + 3] = source[x * 4 + 3];
                }

                out.write(
                    reinterpret_cast<const char*>(row.data()),
                    static_cast<std::streamsize>(row.size()));
            }

            context->Unmap(staging, 0);
            return static_cast<bool>(out);
        }

        bool RenderPreview(
            ID3D11Device* device,
            ID3D11DeviceContext* immediate,
            TextureInfo& source,
            const bool depth,
            const bool invertDepth,
            const wchar_t* outputName,
            std::string& error)
        {
            if (device == nullptr || immediate == nullptr || source.texture == nullptr)
            {
                error = "missing device/context/source";
                return false;
            }

            constexpr UINT outputWidth = 640;
            constexpr UINT outputHeight = 360;

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = depth
                ? DepthSrvFormat(source.desc.Format)
                : source.desc.Format;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels = 1;

            if (srvDesc.Format == DXGI_FORMAT_UNKNOWN)
            {
                error = "unsupported SRV format";
                return false;
            }

            ID3D11ShaderResourceView* sourceSrv = nullptr;
            HRESULT result = device->CreateShaderResourceView(
                source.texture,
                &srvDesc,
                &sourceSrv);

            if (FAILED(result) || sourceSrv == nullptr)
            {
                error = "CreateShaderResourceView failed";
                return false;
            }

            D3D11_TEXTURE2D_DESC outputDesc{};
            outputDesc.Width = outputWidth;
            outputDesc.Height = outputHeight;
            outputDesc.MipLevels = 1;
            outputDesc.ArraySize = 1;
            outputDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            outputDesc.SampleDesc.Count = 1;
            outputDesc.Usage = D3D11_USAGE_DEFAULT;
            outputDesc.BindFlags = D3D11_BIND_RENDER_TARGET;

            ID3D11Texture2D* outputTexture = nullptr;
            ID3D11RenderTargetView* outputRtv = nullptr;

            result = device->CreateTexture2D(
                &outputDesc,
                nullptr,
                &outputTexture);

            if (SUCCEEDED(result))
            {
                result = device->CreateRenderTargetView(
                    outputTexture,
                    nullptr,
                    &outputRtv);
            }

            D3D11_TEXTURE2D_DESC stagingDesc = outputDesc;
            stagingDesc.Usage = D3D11_USAGE_STAGING;
            stagingDesc.BindFlags = 0;
            stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

            ID3D11Texture2D* staging = nullptr;
            if (SUCCEEDED(result))
            {
                result = device->CreateTexture2D(
                    &stagingDesc,
                    nullptr,
                    &staging);
            }

            ID3D11DeviceContext* deferred = nullptr;
            if (SUCCEEDED(result))
            {
                result = device->CreateDeferredContext(0, &deferred);
            }

            const char* vertexSource =
                "struct O{float4 p:SV_POSITION;float2 uv:TEXCOORD0;};"
                "O main(uint id:SV_VertexID){"
                "float2 uv=float2((id<<1)&2,id&2);"
                "O o;o.uv=uv;o.p=float4(uv*float2(2,-2)+float2(-1,1),0,1);"
                "return o;}";

            const char* colorSource =
                "Texture2D<float4> t:register(t0);SamplerState s:register(s0);"
                "float4 main(float4 p:SV_POSITION,float2 uv:TEXCOORD0):SV_TARGET{"
                "float3 c=max(t.SampleLevel(s,uv,0).rgb,0);"
                "c=c/(1+c);c=pow(c,1.0/2.2);return float4(c,1);}";

            const char* depthRawSource =
                "Texture2D<float> t:register(t0);SamplerState s:register(s0);"
                "float4 main(float4 p:SV_POSITION,float2 uv:TEXCOORD0):SV_TARGET{"
                "float d=saturate(t.SampleLevel(s,uv,0));"
                "return float4(d,d,d,1);}";

            const char* depthInverseSource =
                "Texture2D<float> t:register(t0);SamplerState s:register(s0);"
                "float4 main(float4 p:SV_POSITION,float2 uv:TEXCOORD0):SV_TARGET{"
                "float d=saturate(1-t.SampleLevel(s,uv,0));"
                "d=pow(d,0.20);return float4(d,d,d,1);}";

            ID3DBlob* vsBlob = nullptr;
            ID3DBlob* psBlob = nullptr;

            if (SUCCEEDED(result) &&
                !CompileShader(vertexSource, "vs_5_0", &vsBlob, error))
            {
                result = E_FAIL;
            }

            const char* pixelSource = depth
                ? (invertDepth ? depthInverseSource : depthRawSource)
                : colorSource;

            if (SUCCEEDED(result) &&
                !CompileShader(pixelSource, "ps_5_0", &psBlob, error))
            {
                result = E_FAIL;
            }

            ID3D11VertexShader* vs = nullptr;
            ID3D11PixelShader* ps = nullptr;

            if (SUCCEEDED(result))
            {
                result = device->CreateVertexShader(
                    vsBlob->GetBufferPointer(),
                    vsBlob->GetBufferSize(),
                    nullptr,
                    &vs);
            }

            if (SUCCEEDED(result))
            {
                result = device->CreatePixelShader(
                    psBlob->GetBufferPointer(),
                    psBlob->GetBufferSize(),
                    nullptr,
                    &ps);
            }

            D3D11_SAMPLER_DESC samplerDesc{};
            samplerDesc.Filter = depth
                ? D3D11_FILTER_MIN_MAG_MIP_POINT
                : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

            ID3D11SamplerState* sampler = nullptr;
            if (SUCCEEDED(result))
            {
                result = device->CreateSamplerState(
                    &samplerDesc,
                    &sampler);
            }

            ID3D11CommandList* commands = nullptr;

            if (SUCCEEDED(result))
            {
                D3D11_VIEWPORT viewport{};
                viewport.Width = static_cast<FLOAT>(outputWidth);
                viewport.Height = static_cast<FLOAT>(outputHeight);
                viewport.MinDepth = 0.0f;
                viewport.MaxDepth = 1.0f;

                const FLOAT clear[4] = { 0, 0, 0, 1 };

                deferred->ClearRenderTargetView(outputRtv, clear);
                deferred->OMSetRenderTargets(1, &outputRtv, nullptr);
                deferred->RSSetViewports(1, &viewport);
                deferred->IASetInputLayout(nullptr);
                deferred->IASetPrimitiveTopology(
                    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                deferred->VSSetShader(vs, nullptr, 0);
                deferred->PSSetShader(ps, nullptr, 0);
                deferred->PSSetShaderResources(0, 1, &sourceSrv);
                deferred->PSSetSamplers(0, 1, &sampler);
                deferred->Draw(3, 0);

                ID3D11ShaderResourceView* nullSrv = nullptr;
                deferred->PSSetShaderResources(0, 1, &nullSrv);

                result = deferred->FinishCommandList(FALSE, &commands);
            }

            if (SUCCEEDED(result))
            {
                if (commands != nullptr)
                {
                    gzfx::render::execution::TagCommandList(
                        commands,
                        gzfx::render::execution::OwnedPass::ResourceDiagnostic,
                        0);

                    immediate->ExecuteCommandList(commands, TRUE);

                    gzfx::render::execution::RetireCommandList(
                        commands);
                }

                immediate->CopyResource(staging, outputTexture);

                const fs::path root = paths::DiagnosticsRoot();
                result = WriteBmp(
                    root / outputName,
                    staging,
                    immediate,
                    outputWidth,
                    outputHeight)
                    ? S_OK
                    : E_FAIL;
            }

            if (FAILED(result) && error.empty())
            {
                std::ostringstream stream;
                stream << "HRESULT=0x" << std::hex << std::uppercase
                       << static_cast<std::uint32_t>(result);
                error = stream.str();
            }

            if (commands) commands->Release();
            if (sampler) sampler->Release();
            if (ps) ps->Release();
            if (vs) vs->Release();
            if (psBlob) psBlob->Release();
            if (vsBlob) vsBlob->Release();
            if (deferred) deferred->Release();
            if (staging) staging->Release();
            if (outputRtv) outputRtv->Release();
            if (outputTexture) outputTexture->Release();
            if (sourceSrv) sourceSrv->Release();

            return SUCCEEDED(result);
        }

        void SnapshotConstantBuffers(
            const std::uint64_t frame,
            ID3D11Device* device,
            ID3D11DeviceContext* context)
        {
            if (device == nullptr || context == nullptr)
            {
                return;
            }

            std::ostringstream text;
            text << "buffer,bytes,vs_binds,ps_binds,cs_binds,"
                    "vs_slots,ps_slots,cs_slots,float_values\n";

            for (const auto& [id, info] : g_buffers)
            {
                if (info.buffer == nullptr ||
                    info.desc.ByteWidth == 0 ||
                    info.desc.ByteWidth > 4096)
                {
                    continue;
                }

                D3D11_BUFFER_DESC stagingDesc{};
                stagingDesc.ByteWidth = info.desc.ByteWidth;
                stagingDesc.Usage = D3D11_USAGE_STAGING;
                stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

                ID3D11Buffer* staging = nullptr;
                if (FAILED(device->CreateBuffer(
                        &stagingDesc,
                        nullptr,
                        &staging)) ||
                    staging == nullptr)
                {
                    continue;
                }

                context->CopyResource(staging, info.buffer);

                D3D11_MAPPED_SUBRESOURCE mapped{};
                if (SUCCEEDED(context->Map(
                        staging,
                        0,
                        D3D11_MAP_READ,
                        0,
                        &mapped)))
                {
                    text << "0x" << std::hex << id << std::dec
                         << "," << info.desc.ByteWidth
                         << "," << info.vsBinds
                         << "," << info.psBinds
                         << "," << info.csBinds
                         << ",0x" << std::hex << info.vsSlots
                         << ",0x" << info.psSlots
                         << ",0x" << info.csSlots << std::dec
                         << ",\"";

                    const auto* values =
                        static_cast<const float*>(mapped.pData);
                    const std::size_t count =
                        info.desc.ByteWidth / sizeof(float);

                    for (std::size_t i = 0; i < count; ++i)
                    {
                        if (i != 0)
                        {
                            text << ";";
                        }

                        const float value = values[i];
                        if (std::isfinite(value))
                        {
                            text << std::setprecision(9) << value;
                        }
                        else
                        {
                            text << "nan";
                        }
                    }

                    text << "\"\n";
                    context->Unmap(staging, 0);
                }

                staging->Release();
            }

            std::wostringstream name;
            name << L"constant_buffers_frame_"
                 << std::setw(6) << std::setfill(L'0')
                 << frame << L".csv";

            WriteText(name.str().c_str(), text.str());
            g_lastSnapshotFrame = frame;
        }

        void CaptureResourcePreviews(
            const std::uint64_t frame,
            IDXGISwapChain* swapChain,
            ID3D11Device* device,
            ID3D11DeviceContext* context)
        {
            if (g_previewsCaptured ||
                frame < PreviewFrame() ||
                swapChain == nullptr ||
                device == nullptr ||
                context == nullptr)
            {
                return;
            }

            DXGI_SWAP_CHAIN_DESC swapDesc{};
            if (FAILED(swapChain->GetDesc(&swapDesc)))
            {
                return;
            }

            std::uintptr_t depthId = 0;
            TextureInfo* depth = SelectDepth(
                swapDesc.BufferDesc.Width,
                swapDesc.BufferDesc.Height,
                depthId);

            std::uintptr_t colorId = 0;
            std::uint64_t pairCount = 0;
            TextureInfo* color = depth == nullptr
                ? nullptr
                : SelectColor(
                    swapDesc.BufferDesc.Width,
                    swapDesc.BufferDesc.Height,
                    depthId,
                    colorId,
                    pairCount);

            std::ostringstream report;
            report << "GZFX resource intelligence\n"
                   << "version=" << gzfx::kVersion << "\n"
                   << "frame=" << frame << "\n";

            if (depth != nullptr)
            {
                report << "depth_candidate=0x" << std::hex << depthId << std::dec
                       << " size=" << depth->desc.Width
                       << "x" << depth->desc.Height
                       << " format=" << static_cast<unsigned>(depth->desc.Format)
                       << " bind_flags=0x" << std::hex
                       << depth->desc.BindFlags << std::dec
                       << " dsv_binds=" << depth->dsvBinds << "\n";
            }
            else
            {
                report << "depth_candidate=none\n";
            }

            if (color != nullptr)
            {
                report << "color_candidate=0x" << std::hex << colorId << std::dec
                       << " size=" << color->desc.Width
                       << "x" << color->desc.Height
                       << " format=" << static_cast<unsigned>(color->desc.Format)
                       << " bind_flags=0x" << std::hex
                       << color->desc.BindFlags << std::dec
                       << " rtv_binds=" << color->rtvBinds
                       << " paired_with_depth=" << pairCount << "\n";
            }
            else
            {
                report << "color_candidate=none\n";
            }

            if (depth != nullptr)
            {
                std::string error;
                const bool raw = RenderPreview(
                    device,
                    context,
                    *depth,
                    true,
                    false,
                    L"depth_candidate_raw.bmp",
                    error);

                report << "depth_raw_preview="
                       << (raw ? "ok" : "failed")
                       << (error.empty() ? "" : " error=" + error)
                       << "\n";

                error.clear();
                const bool inverse = RenderPreview(
                    device,
                    context,
                    *depth,
                    true,
                    true,
                    L"depth_candidate_inverse.bmp",
                    error);

                report << "depth_inverse_preview="
                       << (inverse ? "ok" : "failed")
                       << (error.empty() ? "" : " error=" + error)
                       << "\n";
            }

            if (color != nullptr)
            {
                std::string error;
                const bool preview = RenderPreview(
                    device,
                    context,
                    *color,
                    false,
                    false,
                    L"scene_color_candidate.bmp",
                    error);

                report << "scene_color_preview="
                       << (preview ? "ok" : "failed")
                       << (error.empty() ? "" : " error=" + error)
                       << "\n";
            }

            WriteText(L"resource_candidates.txt", report.str());
            g_previewsCaptured = true;
        }
    }

    bool AcquireLiveInputs(
        IDXGISwapChain* swapChain,
        LiveInputs& inputs) noexcept
    {
        try
        {
            ReleaseLiveInputs(inputs);

            if (swapChain == nullptr)
            {
                return false;
            }

            DXGI_SWAP_CHAIN_DESC swapDesc{};
            if (FAILED(swapChain->GetDesc(&swapDesc)))
            {
                return false;
            }

            std::scoped_lock lock(g_mutex);

            std::uintptr_t depthId = 0;
            TextureInfo* depth = SelectDepth(
                swapDesc.BufferDesc.Width,
                swapDesc.BufferDesc.Height,
                depthId);

            if (depth == nullptr || depth->texture == nullptr)
            {
                return false;
            }

            std::uintptr_t colorId = 0;
            std::uint64_t pairCount = 0;
            TextureInfo* color = SelectColor(
                swapDesc.BufferDesc.Width,
                swapDesc.BufferDesc.Height,
                depthId,
                colorId,
                pairCount);

            BufferInfo* camera = nullptr;
            std::uint64_t cameraScore = 0;

            for (auto& [id, info] : g_buffers)
            {
                (void)id;

                constexpr std::uint64_t slot12 = 1ull << 12;
                if (info.desc.ByteWidth != 720 ||
                    (info.vsSlots & slot12) == 0 ||
                    (info.psSlots & slot12) == 0 ||
                    info.buffer == nullptr)
                {
                    continue;
                }

                const std::uint64_t score =
                    info.vsBinds + info.psBinds;

                if (score > cameraScore)
                {
                    cameraScore = score;
                    camera = &info;
                }
            }

            if (camera == nullptr)
            {
                return false;
            }

            depth->texture->AddRef();
            camera->buffer->AddRef();

            inputs.depthTexture = depth->texture;
            inputs.depthDesc = depth->desc;
            inputs.cameraBuffer = camera->buffer;

            if (color != nullptr && color->texture != nullptr)
            {
                color->texture->AddRef();
                inputs.sceneColorTexture = color->texture;
                inputs.sceneColorDesc = color->desc;
            }

            const auto acquireNative =
                [&](const std::uintptr_t id,
                    ID3D11Texture2D*& textureOut,
                    D3D11_TEXTURE2D_DESC& descOut)
                {
                    if (id == 0)
                    {
                        return;
                    }

                    const auto found =
                        g_textures.find(id);

                    if (found == g_textures.end() ||
                        found->second.texture == nullptr)
                    {
                        return;
                    }

                    found->second.texture->AddRef();
                    textureOut = found->second.texture;
                    descOut = found->second.desc;
                };

            acquireNative(
                g_nativeMotionId,
                inputs.motionVectorTexture,
                inputs.motionVectorDesc);

            acquireNative(
                g_nativeNormalId,
                inputs.nativeNormalTexture,
                inputs.nativeNormalDesc);

            acquireNative(
                g_nativeAuxId,
                inputs.nativeAuxTexture,
                inputs.nativeAuxDesc);

            inputs.nativeSurfaceFrame =
                g_nativeSurfaceFrame;

            return true;
        }
        catch (...)
        {
            ReleaseLiveInputs(inputs);
            return false;
        }
    }

    void ReleaseLiveInputs(LiveInputs& inputs) noexcept
    {
        if (inputs.cameraBuffer != nullptr)
        {
            inputs.cameraBuffer->Release();
        }

        if (inputs.nativeAuxTexture != nullptr)
        {
            inputs.nativeAuxTexture->Release();
        }

        if (inputs.nativeNormalTexture != nullptr)
        {
            inputs.nativeNormalTexture->Release();
        }

        if (inputs.motionVectorTexture != nullptr)
        {
            inputs.motionVectorTexture->Release();
        }

        if (inputs.sceneColorTexture != nullptr)
        {
            inputs.sceneColorTexture->Release();
        }

        if (inputs.depthTexture != nullptr)
        {
            inputs.depthTexture->Release();
        }

        inputs = {};
    }

    void SetStageTraceEnabled(
        const bool enabled) noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);

            if (enabled &&
                !g_stageTraceEnabled)
            {
                g_stageTraceEvents = 0;
                g_lastStageColorBound = false;
                g_lastStageDepthBound = false;
                g_scenePairBindEvents = 0;
                g_lastScenePairDraw = 0;
                g_preUiSignalIssued = false;

                WriteText(
                    L"stage_trace.csv",
                    "");
            }

            if (!enabled)
            {
                g_lastStageColorBound = false;
                g_lastStageDepthBound = false;
                g_scenePairBindEvents = 0;
                g_lastScenePairDraw = 0;
                g_preUiSignalIssued = false;
            }

            g_stageTraceEnabled = enabled;
        }
        catch (...)
        {
        }
    }

    StageSignal ObserveRenderTargets(
        const std::uint64_t frame,
        const std::uint64_t drawOrdinal,
        const UINT count,
        ID3D11RenderTargetView* const* renderTargets,
        ID3D11DepthStencilView* depthTarget) noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);

            const std::uintptr_t depthId =
                TrackView(depthTarget, frame, false, true);

            std::vector<std::uintptr_t> colorIds;
            colorIds.reserve(count);

            for (UINT i = 0; i < count; ++i)
            {
                const std::uintptr_t colorId =
                    TrackView(
                        renderTargets == nullptr ? nullptr : renderTargets[i],
                        frame,
                        true,
                        false);

                colorIds.push_back(colorId);

                if (depthId != 0 && colorId != 0)
                {
                    ++g_pairs[{ depthId, colorId }];
                }
            }

            if (g_swapWidth != 0 &&
                g_swapHeight != 0 &&
                count >= 3 &&
                depthId != 0)
            {
                std::uintptr_t selectedDepthId = 0;
                TextureInfo* selectedDepth =
                    SelectDepth(
                        g_swapWidth,
                        g_swapHeight,
                        selectedDepthId);

                std::uintptr_t selectedColorId = 0;
                std::uint64_t selectedPairCount = 0;
                TextureInfo* selectedColor =
                    selectedDepth != nullptr
                    ? SelectColor(
                        g_swapWidth,
                        g_swapHeight,
                        selectedDepthId,
                        selectedColorId,
                        selectedPairCount)
                    : nullptr;

                if (selectedDepth != nullptr &&
                    selectedColor != nullptr &&
                    depthId == selectedDepthId &&
                    !colorIds.empty() &&
                    colorIds[0] == selectedColorId)
                {
                    const auto inspectSlot =
                        [&](const UINT slot,
                            const DXGI_FORMAT expectedFormat,
                            std::uintptr_t& idOut)
                        {
                            if (slot >= colorIds.size())
                            {
                                return;
                            }

                            const std::uintptr_t id =
                                colorIds[slot];

                            const auto found =
                                g_textures.find(id);

                            if (found == g_textures.end())
                            {
                                return;
                            }

                            const auto& desc =
                                found->second.desc;

                            if (desc.Width ==
                                    g_swapWidth &&
                                desc.Height ==
                                    g_swapHeight &&
                                desc.Format ==
                                    expectedFormat)
                            {
                                idOut = id;
                            }
                        };

                    inspectSlot(
                        1,
                        DXGI_FORMAT_R16G16_FLOAT,
                        g_nativeMotionId);

                    inspectSlot(
                        2,
                        DXGI_FORMAT_R8G8B8A8_UNORM,
                        g_nativeNormalId);

                    if (colorIds.size() >= 4)
                    {
                        const auto found =
                            g_textures.find(
                                colorIds[3]);

                        if (found !=
                                g_textures.end() &&
                            found->second.desc.Width ==
                                g_swapWidth &&
                            found->second.desc.Height ==
                                g_swapHeight)
                        {
                            g_nativeAuxId =
                                colorIds[3];
                        }
                    }

                    if (g_nativeMotionId != 0 &&
                        g_nativeNormalId != 0)
                    {
                        g_nativeSurfaceFrame =
                            frame;
                    }
                }
            }

            if (!g_stageTraceEnabled ||
                g_swapWidth == 0 ||
                g_swapHeight == 0)
            {
                return StageSignal::None;
            }

            std::uintptr_t selectedDepthId = 0;
            TextureInfo* selectedDepth =
                SelectDepth(
                    g_swapWidth,
                    g_swapHeight,
                    selectedDepthId);

            if (selectedDepth == nullptr)
            {
                return StageSignal::None;
            }

            std::uintptr_t selectedColorId = 0;
            std::uint64_t pairCount = 0;

            TextureInfo* selectedColor =
                SelectColor(
                    g_swapWidth,
                    g_swapHeight,
                    selectedDepthId,
                    selectedColorId,
                    pairCount);

            if (selectedColor == nullptr)
            {
                return StageSignal::None;
            }

            const bool depthBound =
                depthId == selectedDepthId;

            const bool colorBound =
                std::find(
                    colorIds.begin(),
                    colorIds.end(),
                    selectedColorId) !=
                colorIds.end();

            const bool previousColorBound =
                g_lastStageColorBound;

            const bool previousDepthBound =
                g_lastStageDepthBound;

            if (colorBound && depthBound)
            {
                ++g_scenePairBindEvents;
                g_lastScenePairDraw = drawOrdinal;
            }

            bool preUiSignal = false;

            if (!g_preUiSignalIssued &&
                g_scenePairBindEvents >= 8 &&
                previousColorBound &&
                !previousDepthBound &&
                !colorBound &&
                drawOrdinal >= g_lastScenePairDraw)
            {
                const std::uint64_t drawsAfterPair =
                    drawOrdinal - g_lastScenePairDraw;

                // GZFX(6) gameplay trace: terminal HDR-color unbind
                // followed the last paired HDR+depth scene use by
                // 25-27 draws across normal frames. Keep a wider
                // fail-soft admission window for scene complexity
                // without relying on an absolute draw ordinal.
                if (drawsAfterPair >= 16 &&
                    drawsAfterPair <= 64)
                {
                    preUiSignal = true;
                    g_preUiSignalIssued = true;
                }
            }

            const bool stateChanged =
                depthBound != previousDepthBound ||
                colorBound != previousColorBound;

            if ((stateChanged ||
                 depthBound ||
                 colorBound ||
                 preUiSignal) &&
                g_stageTraceEvents < 2048)
            {
                std::ostringstream row;

                if (g_stageTraceEvents == 0)
                {
                    row
                        << "event,frame,draw_ordinal,"
                           "color_bound,depth_bound,rt_count,"
                           "selected_color,selected_depth,"
                           "pair_count,pre_ui_signal\n";
                }

                row
                    << g_stageTraceEvents
                    << "," << frame
                    << "," << drawOrdinal
                    << "," << (colorBound ? 1 : 0)
                    << "," << (depthBound ? 1 : 0)
                    << "," << count
                    << ",0x" << std::hex
                    << selectedColorId
                    << ",0x" << selectedDepthId
                    << std::dec
                    << "," << pairCount
                    << "," << (preUiSignal ? 1 : 0)
                    << "\n";

                AppendText(
                    L"stage_trace.csv",
                    row.str());

                ++g_stageTraceEvents;
            }

            g_lastStageDepthBound = depthBound;
            g_lastStageColorBound = colorBound;

            return preUiSignal
                ? StageSignal::PreUiSceneReady
                : StageSignal::None;
        }
        catch (...)
        {
            return StageSignal::None;
        }
    }

    void ObserveConstantBuffers(
        const std::uint64_t frame,
        const char stage,
        const UINT startSlot,
        const UINT count,
        ID3D11Buffer* const* buffers) noexcept
    {
        try
        {
            if (buffers == nullptr)
            {
                return;
            }

            std::scoped_lock lock(g_mutex);

            for (UINT i = 0; i < count; ++i)
            {
                TrackBuffer(
                    frame,
                    stage,
                    startSlot + i,
                    buffers[i]);
            }
        }
        catch (...)
        {
        }
    }

    void OnPresent(
        const std::uint64_t frame,
        IDXGISwapChain* swapChain,
        ID3D11Device* device,
        ID3D11DeviceContext* context) noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);

            if (swapChain != nullptr)
            {
                DXGI_SWAP_CHAIN_DESC description{};
                if (SUCCEEDED(swapChain->GetDesc(&description)))
                {
                    g_swapWidth = description.BufferDesc.Width;
                    g_swapHeight = description.BufferDesc.Height;
                }
            }

            g_lastStageColorBound = false;
            g_lastStageDepthBound = false;
            g_scenePairBindEvents = 0;
            g_lastScenePairDraw = 0;
            g_preUiSignalIssued = false;

            if (IsSnapshotFrame(frame) &&
                g_lastSnapshotFrame != frame)
            {
                SnapshotConstantBuffers(
                    frame,
                    device,
                    context);
            }

            CaptureResourcePreviews(
                frame,
                swapChain,
                device,
                context);

            if (g_nativeMotionId != 0 ||
                g_nativeNormalId != 0)
            {
                std::ostringstream native;

                native
                    << "GZFX native surface inputs\n"
                    << "frame=" << frame << "\n"
                    << "native_surface_frame="
                    << g_nativeSurfaceFrame << "\n"
                    << "motion=0x"
                    << std::hex
                    << std::uppercase
                    << g_nativeMotionId
                    << "\n"
                    << "normal=0x"
                    << g_nativeNormalId
                    << "\n"
                    << "aux=0x"
                    << g_nativeAuxId
                    << std::dec
                    << "\n";

                WriteText(
                    L"native_surface_inputs.txt",
                    native.str());
            }
        }
        catch (...)
        {
        }
    }
}
