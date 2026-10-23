#include "gzfx/diagnostics/renderer_forensics.hpp"
#include "gzfx/diagnostics/resource_intelligence.hpp"
#include "gzfx/diagnostics/semantic_probe.hpp"
#include "gzfx/diagnostics/pipeline_cartography.hpp"
#include "gzfx/render/scene_effects.hpp"
#include "gzfx/render/visible_pass.hpp"

#include "gzfx/paths.hpp"
#include "gzfx/version.hpp"

#include <Windows.h>
#include <TlHelp32.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <MinHook.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace gzfx::diagnostics
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr std::size_t kContextVtableCount = 115;
        constexpr std::uint64_t kSummaryEvery = 120;
        constexpr std::uint64_t kMaxFrames = 7200;

        constexpr std::size_t kVSSetConstantBuffers = 7;
        constexpr std::size_t kPSSetShaderResources = 8;
        constexpr std::size_t kPSSetShader = 9;
        constexpr std::size_t kPSSetSamplers = 10;
        constexpr std::size_t kVSSetShader = 11;
        constexpr std::size_t kDrawIndexed = 12;
        constexpr std::size_t kDraw = 13;
        constexpr std::size_t kPSSetConstantBuffers = 16;
        constexpr std::size_t kDrawIndexedInstanced = 20;
        constexpr std::size_t kDrawInstanced = 21;
        constexpr std::size_t kGSSetConstantBuffers = 22;
        constexpr std::size_t kGSSetShader = 23;
        constexpr std::size_t kIASetPrimitiveTopology = 24;
        constexpr std::size_t kVSSetShaderResources = 25;
        constexpr std::size_t kVSSetSamplers = 26;
        constexpr std::size_t kGSSetShaderResources = 31;
        constexpr std::size_t kGSSetSamplers = 32;
        constexpr std::size_t kOMSetRenderTargets = 33;
        constexpr std::size_t kOMSetRenderTargetsAndUavs = 34;
        constexpr std::size_t kOMSetBlendState = 35;
        constexpr std::size_t kOMSetDepthStencilState = 36;
        constexpr std::size_t kDrawIndexedInstancedIndirect = 39;
        constexpr std::size_t kDrawInstancedIndirect = 40;
        constexpr std::size_t kDispatch = 41;
        constexpr std::size_t kDispatchIndirect = 42;
        constexpr std::size_t kRSSetState = 43;
        constexpr std::size_t kRSSetViewports = 44;
        constexpr std::size_t kCopySubresourceRegion = 46;
        constexpr std::size_t kCopyResource = 47;
        constexpr std::size_t kUpdateSubresource = 48;
        constexpr std::size_t kCopyStructureCount = 49;
        constexpr std::size_t kClearRenderTargetView = 50;
        constexpr std::size_t kClearUnorderedAccessViewUint = 51;
        constexpr std::size_t kClearUnorderedAccessViewFloat = 52;
        constexpr std::size_t kClearDepthStencilView = 53;
        constexpr std::size_t kGenerateMips = 54;
        constexpr std::size_t kResolveSubresource = 57;
        constexpr std::size_t kExecuteCommandList = 58;
        constexpr std::size_t kHSSetShaderResources = 59;
        constexpr std::size_t kHSSetShader = 60;
        constexpr std::size_t kHSSetSamplers = 61;
        constexpr std::size_t kHSSetConstantBuffers = 62;
        constexpr std::size_t kDSSetShaderResources = 63;
        constexpr std::size_t kDSSetShader = 64;
        constexpr std::size_t kDSSetSamplers = 65;
        constexpr std::size_t kDSSetConstantBuffers = 66;
        constexpr std::size_t kCSSetShaderResources = 67;
        constexpr std::size_t kCSSetUnorderedAccessViews = 68;
        constexpr std::size_t kCSSetShader = 69;
        constexpr std::size_t kCSSetSamplers = 70;
        constexpr std::size_t kCSSetConstantBuffers = 71;
        constexpr std::size_t kClearState = 110;

        using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
        using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
        using ResizeBuffersFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

        using SetConstantBuffersFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11Buffer* const*);
        using SetShaderResourcesFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11ShaderResourceView* const*);
        using SetSamplersFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, ID3D11SamplerState* const*);

        using VSSetShaderFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11VertexShader*, ID3D11ClassInstance* const*, UINT);
        using HSSetShaderFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11HullShader*, ID3D11ClassInstance* const*, UINT);
        using DSSetShaderFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11DomainShader*, ID3D11ClassInstance* const*, UINT);
        using GSSetShaderFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11GeometryShader*, ID3D11ClassInstance* const*, UINT);
        using PSSetShaderFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11PixelShader*, ID3D11ClassInstance* const*, UINT);
        using CSSetShaderFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11ComputeShader*, ID3D11ClassInstance* const*, UINT);

        using DrawIndexedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, INT);
        using DrawFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT);
        using DrawIndexedInstancedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);
        using DrawInstancedFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT, UINT);
        using DrawIndexedInstancedIndirectFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT);
        using DrawInstancedIndirectFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT);

        using IASetPrimitiveTopologyFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, D3D11_PRIMITIVE_TOPOLOGY);
        using OMSetRenderTargetsFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*);
        using OMSetRenderTargetsAndUavsFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, ID3D11RenderTargetView* const*, ID3D11DepthStencilView*, UINT, UINT, ID3D11UnorderedAccessView* const*, const UINT*);
        using OMSetBlendStateFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11BlendState*, const FLOAT[4], UINT);
        using OMSetDepthStencilStateFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11DepthStencilState*, UINT);

        using DispatchFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, UINT, UINT);
        using DispatchIndirectFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT);

        using RSSetStateFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11RasterizerState*);
        using RSSetViewportsFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, UINT, const D3D11_VIEWPORT*);

        using CopySubresourceRegionFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, UINT, UINT, UINT, ID3D11Resource*, UINT, const D3D11_BOX*);
        using CopyResourceFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, ID3D11Resource*);
        using UpdateSubresourceFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, const D3D11_BOX*, const void*, UINT, UINT);
        using CopyStructureCountFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Buffer*, UINT, ID3D11UnorderedAccessView*);
        using ClearRenderTargetViewFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11RenderTargetView*, const FLOAT[4]);
        using ClearUnorderedAccessViewUintFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11UnorderedAccessView*, const UINT[4]);
        using ClearUnorderedAccessViewFloatFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11UnorderedAccessView*, const FLOAT[4]);
        using ClearDepthStencilViewFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11DepthStencilView*, UINT, FLOAT, UINT8);
        using GenerateMipsFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11ShaderResourceView*);
        using ResolveSubresourceFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, ID3D11Resource*, UINT, DXGI_FORMAT);
        using ExecuteCommandListFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11CommandList*, BOOL);
        using ClearStateFn = void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*);

        PresentFn g_present = nullptr;
        Present1Fn g_present1 = nullptr;
        ResizeBuffersFn g_resizeBuffers = nullptr;

        SetConstantBuffersFn g_vsSetCB = nullptr;
        SetConstantBuffersFn g_hsSetCB = nullptr;
        SetConstantBuffersFn g_dsSetCB = nullptr;
        SetConstantBuffersFn g_gsSetCB = nullptr;
        SetConstantBuffersFn g_psSetCB = nullptr;
        SetConstantBuffersFn g_csSetCB = nullptr;

        SetShaderResourcesFn g_vsSetResources = nullptr;
        SetShaderResourcesFn g_hsSetResources = nullptr;
        SetShaderResourcesFn g_dsSetResources = nullptr;
        SetShaderResourcesFn g_gsSetResources = nullptr;
        SetShaderResourcesFn g_psSetResources = nullptr;
        SetShaderResourcesFn g_csSetResources = nullptr;

        SetSamplersFn g_vsSetSamplers = nullptr;
        SetSamplersFn g_hsSetSamplers = nullptr;
        SetSamplersFn g_dsSetSamplers = nullptr;
        SetSamplersFn g_gsSetSamplers = nullptr;
        SetSamplersFn g_psSetSamplers = nullptr;
        SetSamplersFn g_csSetSamplers = nullptr;

        VSSetShaderFn g_vsSetShader = nullptr;
        HSSetShaderFn g_hsSetShader = nullptr;
        DSSetShaderFn g_dsSetShader = nullptr;
        GSSetShaderFn g_gsSetShader = nullptr;
        PSSetShaderFn g_psSetShader = nullptr;
        CSSetShaderFn g_csSetShader = nullptr;

        DrawIndexedFn g_drawIndexed = nullptr;
        DrawFn g_draw = nullptr;
        DrawIndexedInstancedFn g_drawIndexedInstanced = nullptr;
        DrawInstancedFn g_drawInstanced = nullptr;
        DrawIndexedInstancedIndirectFn g_drawIndexedInstancedIndirect = nullptr;
        DrawInstancedIndirectFn g_drawInstancedIndirect = nullptr;

        IASetPrimitiveTopologyFn g_setTopology = nullptr;
        OMSetRenderTargetsFn g_omSetRT = nullptr;
        OMSetRenderTargetsAndUavsFn g_omSetRTUavs = nullptr;
        OMSetBlendStateFn g_omSetBlend = nullptr;
        OMSetDepthStencilStateFn g_omSetDepth = nullptr;

        DispatchFn g_dispatch = nullptr;
        DispatchIndirectFn g_dispatchIndirect = nullptr;
        RSSetStateFn g_setRaster = nullptr;
        RSSetViewportsFn g_setViewports = nullptr;

        CopySubresourceRegionFn g_copySubresource = nullptr;
        CopyResourceFn g_copyResource = nullptr;
        UpdateSubresourceFn g_updateSubresource = nullptr;
        CopyStructureCountFn g_copyStructureCount = nullptr;
        ClearRenderTargetViewFn g_clearRTV = nullptr;
        ClearUnorderedAccessViewUintFn g_clearUavUint = nullptr;
        ClearUnorderedAccessViewFloatFn g_clearUavFloat = nullptr;
        ClearDepthStencilViewFn g_clearDSV = nullptr;
        GenerateMipsFn g_generateMips = nullptr;
        ResolveSubresourceFn g_resolve = nullptr;
        ExecuteCommandListFn g_executeCommandList = nullptr;

        CSSetUnorderedAccessViewsFn g_csSetUavs = nullptr;
        ClearStateFn g_clearState = nullptr;

        std::once_flag g_once;
        bool g_armed = false;
        std::mutex g_mutex;

        IDXGISwapChain* g_primarySwap = nullptr;
        ID3D11Device* g_primaryDevice = nullptr;
        ID3D11DeviceContext* g_primaryContext = nullptr;
        std::uintptr_t* g_originalContextVtable = nullptr;
        std::uintptr_t* g_contextVtable = nullptr;

        std::uint64_t g_frame = 0;
        bool g_contextArmed = false;

        std::atomic<std::uint64_t> g_drawOrdinalThisFrame{0};
        std::atomic<std::uint64_t> g_draws{0};
        std::atomic<std::uint64_t> g_drawIndexedCalls{0};
        std::atomic<std::uint64_t> g_drawIndexedActivityThisFrame{0};
        std::atomic<std::uint64_t> g_drawInstancedCalls{0};
        std::atomic<std::uint64_t> g_drawIndexedInstancedCalls{0};
        std::atomic<std::uint64_t> g_dispatchCalls{0};
        std::atomic<std::uint64_t> g_rtSets{0};
        std::atomic<std::uint64_t> g_rtvClears{0};
        std::atomic<std::uint64_t> g_dsvClears{0};
        std::atomic<std::uint64_t> g_copies{0};
        std::atomic<std::uint64_t> g_resolves{0};
        std::atomic<std::uint64_t> g_viewportSets{0};
        std::atomic<std::uint64_t> g_vsChanges{0};
        std::atomic<std::uint64_t> g_psChanges{0};
        std::atomic<std::uint64_t> g_csChanges{0};
        std::atomic<std::uint64_t> g_vsCBBinds{0};
        std::atomic<std::uint64_t> g_psCBBinds{0};
        std::atomic<std::uint64_t> g_csCBBinds{0};

        struct TextureStats
        {
            std::uintptr_t id = 0;
            UINT width = 0;
            UINT height = 0;
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
            UINT bindFlags = 0;
            UINT samples = 0;
            std::uint64_t rtvBinds = 0;
            std::uint64_t dsvBinds = 0;
            std::uint64_t rtvClears = 0;
            std::uint64_t dsvClears = 0;
            std::uint64_t firstFrame = 0;
            std::uint64_t lastFrame = 0;
        };

        struct BufferStats
        {
            std::uintptr_t id = 0;
            UINT bytes = 0;
            UINT bindFlags = 0;
            D3D11_USAGE usage = D3D11_USAGE_DEFAULT;
            std::uint64_t vs = 0;
            std::uint64_t ps = 0;
            std::uint64_t cs = 0;
            std::uint64_t vsSlots = 0;
            std::uint64_t psSlots = 0;
            std::uint64_t csSlots = 0;
        };

        std::unordered_map<std::uintptr_t, TextureStats> g_textures;
        std::unordered_map<std::uintptr_t, BufferStats> g_buffers;
        std::map<std::pair<UINT, UINT>, std::uint64_t> g_viewports;

        std::string Ptr(const void* p)
        {
            std::ostringstream out;
            out << "0x" << std::hex << std::uppercase
                << reinterpret_cast<std::uintptr_t>(p);
            return out.str();
        }

        void Write(const wchar_t* name, const std::string& text, bool append = false)
        {
            const fs::path root = paths::DiagnosticsRoot();
            if (root.empty())
            {
                return;
            }

            std::error_code ec;
            fs::create_directories(root, ec);
            if (ec)
            {
                return;
            }

            std::ofstream stream(root / name, std::ios::out | (append ? std::ios::app : std::ios::trunc));
            if (stream)
            {
                stream << text;
            }
        }

        template <class F>
        void Safe(F&& f) noexcept
        {
            try { f(); } catch (...) {}
        }

        void WriteModuleInventory()
        {
            std::ostringstream out;
            out << "GZFX loaded modules\n";

            const HANDLE snap = CreateToolhelp32Snapshot(
                TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                GetCurrentProcessId());

            if (snap != INVALID_HANDLE_VALUE)
            {
                MODULEENTRY32W e{};
                e.dwSize = sizeof(e);

                if (Module32FirstW(snap, &e))
                {
                    do
                    {
                        char utf8[2048]{};
                        WideCharToMultiByte(CP_UTF8, 0, e.szExePath, -1, utf8, sizeof(utf8), nullptr, nullptr);
                        out << "base=" << Ptr(e.modBaseAddr)
                            << " size=" << e.modBaseSize
                            << " path=" << utf8 << "\n";
                    } while (Module32NextW(snap, &e));
                }
                CloseHandle(snap);
            }

            Write(L"loaded_modules.txt", out.str());
        }

        TextureStats* TrackView(ID3D11View* view, bool rtvBind, bool dsvBind, bool rtvClear, bool dsvClear)
        {
            if (!view)
            {
                return nullptr;
            }

            ID3D11Resource* resource = nullptr;
            view->GetResource(&resource);
            if (!resource)
            {
                return nullptr;
            }

            ID3D11Texture2D* texture = nullptr;
            if (FAILED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture))))
            {
                resource->Release();
                return nullptr;
            }

            D3D11_TEXTURE2D_DESC d{};
            texture->GetDesc(&d);
            const auto id = reinterpret_cast<std::uintptr_t>(resource);
            auto& s = g_textures[id];

            if (!s.id)
            {
                s.id = id;
                s.width = d.Width;
                s.height = d.Height;
                s.format = d.Format;
                s.bindFlags = d.BindFlags;
                s.samples = d.SampleDesc.Count;
                s.firstFrame = g_frame;
            }

            s.lastFrame = g_frame;
            s.rtvBinds += rtvBind ? 1 : 0;
            s.dsvBinds += dsvBind ? 1 : 0;
            s.rtvClears += rtvClear ? 1 : 0;
            s.dsvClears += dsvClear ? 1 : 0;

            texture->Release();
            resource->Release();
            return &s;
        }

        void TrackBuffers(char stage, UINT startSlot, UINT count, ID3D11Buffer* const* buffers)
        {
            if (!buffers)
            {
                return;
            }

            for (UINT i = 0; i < count; ++i)
            {
                if (!buffers[i])
                {
                    continue;
                }

                D3D11_BUFFER_DESC d{};
                buffers[i]->GetDesc(&d);
                const auto id = reinterpret_cast<std::uintptr_t>(buffers[i]);
                auto& s = g_buffers[id];

                if (!s.id)
                {
                    s.id = id;
                    s.bytes = d.ByteWidth;
                    s.bindFlags = d.BindFlags;
                    s.usage = d.Usage;
                }

                const UINT slot = startSlot + i;
                const std::uint64_t bit = slot < 64 ? (1ull << slot) : 0;

                if (stage == 'V') { ++s.vs; s.vsSlots |= bit; }
                if (stage == 'P') { ++s.ps; s.psSlots |= bit; }
                if (stage == 'C') { ++s.cs; s.csSlots |= bit; }
            }
        }

        void WriteRendererSummary()
        {
            std::ostringstream out;
            out << "GZFX renderer forensics\n"
                << "version=" << gzfx::kVersion << "\n"
                << "frame=" << g_frame << "\n"
                << "swap_chain=" << Ptr(g_primarySwap) << "\n"
                << "device=" << Ptr(g_primaryDevice) << "\n"
                << "context=" << Ptr(g_primaryContext) << "\n"
                << "tracked_textures=" << g_textures.size() << "\n"
                << "tracked_buffers=" << g_buffers.size() << "\n\n";

            if (g_primarySwap)
            {
                DXGI_SWAP_CHAIN_DESC sd{};
                if (SUCCEEDED(g_primarySwap->GetDesc(&sd)))
                {
                    RECT rc{};
                    GetClientRect(sd.OutputWindow, &rc);
                    out << "swap_desc=" << sd.BufferDesc.Width << "x" << sd.BufferDesc.Height
                        << " client=" << (rc.right - rc.left) << "x" << (rc.bottom - rc.top)
                        << " format=" << static_cast<unsigned>(sd.BufferDesc.Format)
                        << " buffers=" << sd.BufferCount
                        << " windowed=" << (sd.Windowed ? "true" : "false")
                        << " swap_effect=" << static_cast<unsigned>(sd.SwapEffect)
                        << " hwnd=" << Ptr(sd.OutputWindow) << "\n";
                }

                ID3D11Texture2D* back = nullptr;
                if (SUCCEEDED(g_primarySwap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) && back)
                {
                    D3D11_TEXTURE2D_DESC d{};
                    back->GetDesc(&d);
                    out << "backbuffer=" << d.Width << "x" << d.Height
                        << " format=" << static_cast<unsigned>(d.Format)
                        << " samples=" << d.SampleDesc.Count << "\n";
                    back->Release();
                }
            }

            if (g_primaryDevice)
            {
                out << "feature_level=0x" << std::hex << std::uppercase
                    << static_cast<unsigned>(g_primaryDevice->GetFeatureLevel()) << std::dec << "\n";

                IDXGIDevice* dxgiDevice = nullptr;
                if (SUCCEEDED(g_primaryDevice->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice))) && dxgiDevice)
                {
                    IDXGIAdapter* adapter = nullptr;
                    if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) && adapter)
                    {
                        DXGI_ADAPTER_DESC ad{};
                        if (SUCCEEDED(adapter->GetDesc(&ad)))
                        {
                            char name[512]{};
                            WideCharToMultiByte(CP_UTF8, 0, ad.Description, -1, name, sizeof(name), nullptr, nullptr);
                            out << "adapter=" << name
                                << " vendor=0x" << std::hex << ad.VendorId
                                << " device=0x" << ad.DeviceId << std::dec
                                << " dedicated_vram=" << ad.DedicatedVideoMemory << "\n";
                        }
                        adapter->Release();
                    }
                    dxgiDevice->Release();
                }
            }

            std::vector<TextureStats> textures;
            for (const auto& [id, value] : g_textures) { (void)id; textures.push_back(value); }
            std::sort(textures.begin(), textures.end(), [](const auto& a, const auto& b)
            {
                return (a.dsvBinds * 1000 + a.rtvBinds) > (b.dsvBinds * 1000 + b.rtvBinds);
            });

            out << "\n--- render/depth candidates ---\n";
            for (std::size_t i = 0; i < std::min<std::size_t>(textures.size(), 64); ++i)
            {
                const auto& t = textures[i];
                out << "tex=0x" << std::hex << t.id << std::dec
                    << " size=" << t.width << "x" << t.height
                    << " format=" << static_cast<unsigned>(t.format)
                    << " samples=" << t.samples
                    << " bind_flags=0x" << std::hex << t.bindFlags << std::dec
                    << " rtv_binds=" << t.rtvBinds
                    << " dsv_binds=" << t.dsvBinds
                    << " rtv_clears=" << t.rtvClears
                    << " dsv_clears=" << t.dsvClears
                    << " frames=" << t.firstFrame << "-" << t.lastFrame << "\n";
            }

            std::vector<BufferStats> buffers;
            for (const auto& [id, value] : g_buffers) { (void)id; buffers.push_back(value); }
            std::sort(buffers.begin(), buffers.end(), [](const auto& a, const auto& b)
            {
                return (a.vs + a.ps + a.cs) > (b.vs + b.ps + b.cs);
            });

            out << "\n--- constant buffers ---\n";
            for (std::size_t i = 0; i < std::min<std::size_t>(buffers.size(), 64); ++i)
            {
                const auto& b = buffers[i];
                out << "buffer=0x" << std::hex << b.id << std::dec
                    << " bytes=" << b.bytes
                    << " vs_binds=" << b.vs
                    << " ps_binds=" << b.ps
                    << " cs_binds=" << b.cs
                    << " vs_slots=0x" << std::hex << b.vsSlots
                    << " ps_slots=0x" << b.psSlots
                    << " cs_slots=0x" << b.csSlots << std::dec << "\n";
            }

            out << "\n--- viewports ---\n";
            for (const auto& [size, count] : g_viewports)
            {
                out << size.first << "x" << size.second << " count=" << count << "\n";
            }

            Write(L"renderer_summary.txt", out.str());
        }

        void WriteFrameSummary()
        {
            if (!g_contextArmed || g_frame == 0 || g_frame > kMaxFrames)
            {
                return;
            }

            const fs::path path = paths::DiagnosticsRoot() / L"frame_summary.csv";
            std::error_code ec;
            const bool exists = fs::exists(path, ec) && !ec;

            std::ostringstream out;
            if (!exists)
            {
                out << "frame,draw,draw_indexed,draw_instanced,draw_indexed_instanced,dispatch,rt_sets,rtv_clears,dsv_clears,copies,resolves,viewport_sets,vs_changes,ps_changes,cs_changes,vs_cb_binds,ps_cb_binds,cs_cb_binds\n";
            }

            out << g_frame << ","
                << g_draws.exchange(0) << ","
                << g_drawIndexedCalls.exchange(0) << ","
                << g_drawInstancedCalls.exchange(0) << ","
                << g_drawIndexedInstancedCalls.exchange(0) << ","
                << g_dispatchCalls.exchange(0) << ","
                << g_rtSets.exchange(0) << ","
                << g_rtvClears.exchange(0) << ","
                << g_dsvClears.exchange(0) << ","
                << g_copies.exchange(0) << ","
                << g_resolves.exchange(0) << ","
                << g_viewportSets.exchange(0) << ","
                << g_vsChanges.exchange(0) << ","
                << g_psChanges.exchange(0) << ","
                << g_csChanges.exchange(0) << ","
                << g_vsCBBinds.exchange(0) << ","
                << g_psCBBinds.exchange(0) << ","
                << g_csCBBinds.exchange(0) << "\n";

            Write(L"frame_summary.csv", out.str(), true);
        }

        template <class T>
        T Original(std::size_t index)
        {
            return reinterpret_cast<T>(g_originalContextVtable[index]);
        }

        void STDMETHODCALLTYPE HookVSSetCB(ID3D11DeviceContext* c, UINT slot, UINT count, ID3D11Buffer* const* b)
        {
            if (c == g_primaryContext) Safe([&]
            {
                g_vsCBBinds += count;
                {
                    std::scoped_lock l(g_mutex);
                    TrackBuffers('V', slot, count, b);
                }
                resource_intelligence::ObserveConstantBuffers(
                    g_frame, 'V', slot, count, b);
            });
            g_vsSetCB(c, slot, count, b);
        }

        void STDMETHODCALLTYPE HookPSSetCB(ID3D11DeviceContext* c, UINT slot, UINT count, ID3D11Buffer* const* b)
        {
            if (c == g_primaryContext) Safe([&]
            {
                g_psCBBinds += count;
                {
                    std::scoped_lock l(g_mutex);
                    TrackBuffers('P', slot, count, b);
                }
                resource_intelligence::ObserveConstantBuffers(
                    g_frame, 'P', slot, count, b);
            });
            g_psSetCB(c, slot, count, b);
        }

        void STDMETHODCALLTYPE HookCSSetCB(ID3D11DeviceContext* c, UINT slot, UINT count, ID3D11Buffer* const* b)
        {
            if (c == g_primaryContext) Safe([&]
            {
                g_csCBBinds += count;
                {
                    std::scoped_lock l(g_mutex);
                    TrackBuffers('C', slot, count, b);
                }
                resource_intelligence::ObserveConstantBuffers(
                    g_frame, 'C', slot, count, b);
            });
            g_csSetCB(c, slot, count, b);
        }

        void STDMETHODCALLTYPE HookPSSetResources(
            ID3D11DeviceContext* context,
            UINT startSlot,
            UINT count,
            ID3D11ShaderResourceView* const* resources)
        {
            if (context == g_primaryContext)
            {
                Safe([&]
                {
                    semantic_probe::ObservePixelResources(
                        g_frame,
                        g_drawOrdinalThisFrame.load(),
                        startSlot,
                        count,
                        resources);
                });
            }

            g_psSetResources(
                context,
                startSlot,
                count,
                resources);
        }

        void STDMETHODCALLTYPE HookCSSetResources(
            ID3D11DeviceContext* context,
            UINT startSlot,
            UINT count,
            ID3D11ShaderResourceView* const* resources)
        {
            if (context == g_primaryContext)
            {
                Safe([&]
                {
                    semantic_probe::ObserveComputeResources(
                        g_frame,
                        g_drawOrdinalThisFrame.load(),
                        startSlot,
                        count,
                        resources);
                });
            }

            g_csSetResources(
                context,
                startSlot,
                count,
                resources);
        }

        void STDMETHODCALLTYPE HookCSSetUavs(
            ID3D11DeviceContext* context,
            UINT startSlot,
            UINT count,
            ID3D11UnorderedAccessView* const* resources,
            const UINT* initialCounts)
        {
            if (context == g_primaryContext)
            {
                Safe([&]
                {
                    semantic_probe::ObserveComputeUavs(
                        g_frame,
                        g_drawOrdinalThisFrame.load(),
                        startSlot,
                        count,
                        resources);
                });
            }

            g_csSetUavs(
                context,
                startSlot,
                count,
                resources,
                initialCounts);
        }

        void STDMETHODCALLTYPE HookPSShader(ID3D11DeviceContext* c, ID3D11PixelShader* s, ID3D11ClassInstance* const* i, UINT n)
        {
            if (c == g_primaryContext)
            {
                ++g_psChanges;
                semantic_probe::SetPixelShader(s);
            }

            g_psSetShader(c, s, i, n);
        }

        void STDMETHODCALLTYPE HookVSShader(ID3D11DeviceContext* c, ID3D11VertexShader* s, ID3D11ClassInstance* const* i, UINT n)
        { if (c == g_primaryContext) ++g_vsChanges; g_vsSetShader(c, s, i, n); }

        void STDMETHODCALLTYPE HookCSShader(ID3D11DeviceContext* c, ID3D11ComputeShader* s, ID3D11ClassInstance* const* i, UINT n)
        {
            if (c == g_primaryContext)
            {
                ++g_csChanges;
                semantic_probe::SetComputeShader(s);
            }

            g_csSetShader(c, s, i, n);
        }

        void STDMETHODCALLTYPE HookDraw(ID3D11DeviceContext* c, UINT a, UINT b)
        {
            if (c == g_primaryContext)
            {
                ++g_drawOrdinalThisFrame;
                ++g_draws;
            }
            g_draw(c, a, b);
        }

        void STDMETHODCALLTYPE HookDrawIndexed(ID3D11DeviceContext* c, UINT a, UINT b, INT d)
        {
            if (c == g_primaryContext)
            {
                ++g_drawOrdinalThisFrame;
                ++g_drawIndexedCalls;
                ++g_drawIndexedActivityThisFrame;
            }
            g_drawIndexed(c, a, b, d);
        }

        void STDMETHODCALLTYPE HookDrawInstanced(ID3D11DeviceContext* c, UINT a, UINT b, UINT d, UINT e)
        {
            if (c == g_primaryContext)
            {
                ++g_drawOrdinalThisFrame;
                ++g_drawInstancedCalls;
            }
            g_drawInstanced(c, a, b, d, e);
        }

        void STDMETHODCALLTYPE HookDrawIndexedInstanced(ID3D11DeviceContext* c, UINT a, UINT b, UINT d, INT e, UINT f)
        {
            if (c == g_primaryContext)
            {
                ++g_drawOrdinalThisFrame;
                ++g_drawIndexedInstancedCalls;
            }
            g_drawIndexedInstanced(c, a, b, d, e, f);
        }

        void STDMETHODCALLTYPE HookDispatch(ID3D11DeviceContext* c, UINT a, UINT b, UINT d)
        { if (c == g_primaryContext) ++g_dispatchCalls; g_dispatch(c, a, b, d); }

        void STDMETHODCALLTYPE HookOMSetRT(
            ID3D11DeviceContext* c,
            UINT count,
            ID3D11RenderTargetView* const* rtvs,
            ID3D11DepthStencilView* dsv)
        {
            resource_intelligence::StageSignal stageSignal =
                resource_intelligence::StageSignal::None;

            if (c == g_primaryContext)
            {
                Safe([&]
                {
                    ++g_rtSets;

                    {
                        std::scoped_lock l(g_mutex);

                        for (UINT i = 0; i < count; ++i)
                        {
                            TrackView(
                                rtvs ? rtvs[i] : nullptr,
                                true,
                                false,
                                false,
                                false);
                        }

                        TrackView(
                            dsv,
                            false,
                            true,
                            false,
                            false);
                    }

                    stageSignal =
                        resource_intelligence::
                            ObserveRenderTargets(
                                g_frame,
                                g_drawOrdinalThisFrame.load(),
                                count,
                                rtvs,
                                dsv);

                    semantic_probe::ObserveRenderTargets(
                        g_frame,
                        g_drawOrdinalThisFrame.load(),
                        count,
                        rtvs,
                        dsv);
                });
            }

            // Let Skyrim establish the next render stage first.
            // If the previous stage was the evidence-backed terminal
            // HDR-color phase, the scene resource is now unbound as an
            // output and can be sampled/mutated safely before the next draw.
            g_omSetRT(
                c,
                count,
                rtvs,
                dsv);

            if (c == g_primaryContext &&
                !semantic_probe::SuppressSceneEffects() &&
                stageSignal ==
                    resource_intelligence::
                        StageSignal::PreUiSceneReady)
            {
                Safe([&]
                {
                    resource_intelligence::LiveInputs inputs{};

                    if (resource_intelligence::
                            AcquireLiveInputs(
                                g_primarySwap,
                                inputs))
                    {
                        gzfx::render::scene_effects::
                            ApplyPreUi(
                                g_frame,
                                g_drawOrdinalThisFrame.load(),
                                g_primaryDevice,
                                g_primaryContext,
                                inputs);

                        resource_intelligence::
                            ReleaseLiveInputs(
                                inputs);
                    }
                });
            }
        }

        void STDMETHODCALLTYPE HookViewports(ID3D11DeviceContext* c, UINT count, const D3D11_VIEWPORT* v)
        {
            if (c == g_primaryContext) Safe([&]
            {
                ++g_viewportSets;

                semantic_probe::SetViewport(
                    count,
                    v);

                if (count && v)
                {
                    std::scoped_lock l(g_mutex);
                    ++g_viewports[{static_cast<UINT>(v[0].Width), static_cast<UINT>(v[0].Height)}];
                }
            });
            g_setViewports(c, count, v);
        }

        void STDMETHODCALLTYPE HookClearRTV(ID3D11DeviceContext* c, ID3D11RenderTargetView* v, const FLOAT color[4])
        {
            if (c == g_primaryContext) Safe([&]{ ++g_rtvClears; std::scoped_lock l(g_mutex); TrackView(v, false, false, true, false); });
            g_clearRTV(c, v, color);
        }

        void STDMETHODCALLTYPE HookClearDSV(ID3D11DeviceContext* c, ID3D11DepthStencilView* v, UINT f, FLOAT d, UINT8 s)
        {
            if (c == g_primaryContext) Safe([&]{ ++g_dsvClears; std::scoped_lock l(g_mutex); TrackView(v, false, false, false, true); });
            g_clearDSV(c, v, f, d, s);
        }

        void STDMETHODCALLTYPE HookCopy(ID3D11DeviceContext* c, ID3D11Resource* d, ID3D11Resource* s)
        { if (c == g_primaryContext) ++g_copies; g_copyResource(c, d, s); }

        void STDMETHODCALLTYPE HookResolve(ID3D11DeviceContext* c, ID3D11Resource* d, UINT ds, ID3D11Resource* s, UINT ss, DXGI_FORMAT f)
        { if (c == g_primaryContext) ++g_resolves; g_resolve(c, d, ds, s, ss, f); }

        bool InstallContextHooks(ID3D11DeviceContext* context)
        {
            if (!context || g_contextArmed)
            {
                return g_contextArmed;
            }

            g_originalContextVtable = *reinterpret_cast<std::uintptr_t**>(context);
            g_contextVtable = static_cast<std::uintptr_t*>(VirtualAlloc(
                nullptr,
                sizeof(std::uintptr_t) * kContextVtableCount,
                MEM_COMMIT | MEM_RESERVE,
                PAGE_READWRITE));

            if (!g_originalContextVtable || !g_contextVtable)
            {
                return false;
            }

            std::memcpy(g_contextVtable, g_originalContextVtable, sizeof(std::uintptr_t) * kContextVtableCount);

            g_vsSetCB = Original<VSSetConstantBuffersFn>(kVSSetConstantBuffers);
            g_psSetResources = Original<PSSetShaderResourcesFn>(kPSSetShaderResources);
            g_psSetShader = Original<PSSetShaderFn>(kPSSetShader);
            g_vsSetShader = Original<VSSetShaderFn>(kVSSetShader);
            g_drawIndexed = Original<DrawIndexedFn>(kDrawIndexed);
            g_draw = Original<DrawFn>(kDraw);
            g_psSetCB = Original<PSSetConstantBuffersFn>(kPSSetConstantBuffers);
            g_drawIndexedInstanced = Original<DrawIndexedInstancedFn>(kDrawIndexedInstanced);
            g_drawInstanced = Original<DrawInstancedFn>(kDrawInstanced);
            g_omSetRT = Original<OMSetRenderTargetsFn>(kOMSetRenderTargets);
            g_dispatch = Original<DispatchFn>(kDispatch);
            g_setViewports = Original<RSSetViewportsFn>(kRSSetViewports);
            g_copyResource = Original<CopyResourceFn>(kCopyResource);
            g_clearRTV = Original<ClearRenderTargetViewFn>(kClearRenderTargetView);
            g_clearDSV = Original<ClearDepthStencilViewFn>(kClearDepthStencilView);
            g_resolve = Original<ResolveSubresourceFn>(kResolveSubresource);
            g_csSetResources = Original<CSSetShaderResourcesFn>(kCSSetShaderResources);
            g_csSetUavs = Original<CSSetUnorderedAccessViewsFn>(kCSSetUnorderedAccessViews);
            g_csSetShader = Original<CSSetShaderFn>(kCSSetShader);
            g_csSetCB = Original<CSSetConstantBuffersFn>(kCSSetConstantBuffers);

            g_contextVtable[kVSSetConstantBuffers] = reinterpret_cast<std::uintptr_t>(&HookVSSetCB);
            g_contextVtable[kPSSetShaderResources] = reinterpret_cast<std::uintptr_t>(&HookPSSetResources);
            g_contextVtable[kPSSetShader] = reinterpret_cast<std::uintptr_t>(&HookPSShader);
            g_contextVtable[kVSSetShader] = reinterpret_cast<std::uintptr_t>(&HookVSShader);
            g_contextVtable[kDrawIndexed] = reinterpret_cast<std::uintptr_t>(&HookDrawIndexed);
            g_contextVtable[kDraw] = reinterpret_cast<std::uintptr_t>(&HookDraw);
            g_contextVtable[kPSSetConstantBuffers] = reinterpret_cast<std::uintptr_t>(&HookPSSetCB);
            g_contextVtable[kDrawIndexedInstanced] = reinterpret_cast<std::uintptr_t>(&HookDrawIndexedInstanced);
            g_contextVtable[kDrawInstanced] = reinterpret_cast<std::uintptr_t>(&HookDrawInstanced);
            g_contextVtable[kOMSetRenderTargets] = reinterpret_cast<std::uintptr_t>(&HookOMSetRT);
            g_contextVtable[kDispatch] = reinterpret_cast<std::uintptr_t>(&HookDispatch);
            g_contextVtable[kRSSetViewports] = reinterpret_cast<std::uintptr_t>(&HookViewports);
            g_contextVtable[kCopyResource] = reinterpret_cast<std::uintptr_t>(&HookCopy);
            g_contextVtable[kClearRenderTargetView] = reinterpret_cast<std::uintptr_t>(&HookClearRTV);
            g_contextVtable[kClearDepthStencilView] = reinterpret_cast<std::uintptr_t>(&HookClearDSV);
            g_contextVtable[kResolveSubresource] = reinterpret_cast<std::uintptr_t>(&HookResolve);
            g_contextVtable[kCSSetShaderResources] = reinterpret_cast<std::uintptr_t>(&HookCSSetResources);
            g_contextVtable[kCSSetUnorderedAccessViews] = reinterpret_cast<std::uintptr_t>(&HookCSSetUavs);
            g_contextVtable[kCSSetShader] = reinterpret_cast<std::uintptr_t>(&HookCSShader);
            g_contextVtable[kCSSetConstantBuffers] = reinterpret_cast<std::uintptr_t>(&HookCSSetCB);

            InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(context), g_contextVtable);
            g_contextArmed = true;
            g_frame = 1;
            return true;
        }

        bool IsProcessWindow(HWND window)
        {
            if (!window)
            {
                return false;
            }

            DWORD pid = 0;
            GetWindowThreadProcessId(window, &pid);
            return pid == GetCurrentProcessId();
        }

        void AcquireSwapChain(IDXGISwapChain* swap)
        {
            if (!swap || g_primarySwap)
            {
                return;
            }

            DXGI_SWAP_CHAIN_DESC d{};
            if (FAILED(swap->GetDesc(&d)) || !IsProcessWindow(d.OutputWindow))
            {
                return;
            }

            ID3D11Device* device = nullptr;
            if (FAILED(swap->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device))) || !device)
            {
                return;
            }

            ID3D11DeviceContext* context = nullptr;
            device->GetImmediateContext(&context);
            if (!context)
            {
                device->Release();
                return;
            }

            g_primarySwap = swap;
            g_primarySwap->AddRef();
            g_primaryDevice = device;
            g_primaryContext = context;
            InstallContextHooks(context);

            WriteRendererSummary();
        }

        void OnPresent(IDXGISwapChain* swap)
        {
            std::scoped_lock lock(g_mutex);

            AcquireSwapChain(swap);

            if (swap == g_primarySwap && g_contextArmed)
            {
                const std::uint64_t drawIndexedActivity =
                    g_drawIndexedActivityThisFrame.exchange(0);

                WriteFrameSummary();

                if (g_frame <= 3 || (g_frame % kSummaryEvery) == 0)
                {
                    WriteRendererSummary();
                }

                semantic_probe::OnPresent(
                    g_frame,
                    drawIndexedActivity,
                    g_primarySwap,
                    g_primaryDevice,
                    g_primaryContext);

                resource_intelligence::OnPresent(
                    g_frame,
                    g_primarySwap,
                    g_primaryDevice,
                    g_primaryContext);

                if (!semantic_probe::SuppressSceneEffects())
                {
                    gzfx::render::RenderVisiblePass(
                        g_frame,
                        drawIndexedActivity,
                        g_primarySwap,
                        g_primaryDevice,
                        g_primaryContext);
                }

                g_drawOrdinalThisFrame.store(0);
                ++g_frame;
            }
        }

        HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain* swap, UINT sync, UINT flags)
        {
            Safe([&]{ OnPresent(swap); });
            return g_present(swap, sync, flags);
        }

        HRESULT STDMETHODCALLTYPE HookPresent1(
            IDXGISwapChain1* swap,
            UINT sync,
            UINT flags,
            const DXGI_PRESENT_PARAMETERS* params)
        {
            Safe([&]{ OnPresent(swap); });
            return g_present1(swap, sync, flags, params);
        }

        HRESULT STDMETHODCALLTYPE HookResizeBuffers(
            IDXGISwapChain* swap,
            UINT buffers,
            UINT width,
            UINT height,
            DXGI_FORMAT format,
            UINT flags)
        {
            Safe([&]
            {
                std::ostringstream out;
                out << "frame=" << g_frame
                    << " swap=" << Ptr(swap)
                    << " buffers=" << buffers
                    << " size=" << width << "x" << height
                    << " format=" << static_cast<unsigned>(format)
                    << " flags=0x" << std::hex << flags << std::dec << "\n";
                Write(L"resize_events.txt", out.str(), true);
            });

            return g_resizeBuffers(swap, buffers, width, height, format, flags);
        }

        LRESULT CALLBACK DummyProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
        {
            return DefWindowProcW(hwnd, msg, w, l);
        }

        struct Targets
        {
            void* present = nullptr;
            void* present1 = nullptr;
            void* resize = nullptr;
        };

        Targets ResolveTargets()
        {
            Targets result{};
            const wchar_t* className = L"GZFX.Forensics.Dummy";

            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = DummyProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = className;

            const ATOM atom = RegisterClassExW(&wc);
            if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            {
                return result;
            }

            const HWND hwnd = CreateWindowExW(
                0, className, L"GZFX", WS_OVERLAPPED,
                0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);

            if (!hwnd)
            {
                return result;
            }

            HMODULE d3d = GetModuleHandleW(L"d3d11.dll");
            if (!d3d)
            {
                d3d = LoadLibraryW(L"d3d11.dll");
            }

            using CreateFn = decltype(&D3D11CreateDeviceAndSwapChain);
            const auto create = d3d
                ? reinterpret_cast<CreateFn>(GetProcAddress(d3d, "D3D11CreateDeviceAndSwapChain"))
                : nullptr;

            if (!create)
            {
                DestroyWindow(hwnd);
                return result;
            }

            DXGI_SWAP_CHAIN_DESC sd{};
            sd.BufferDesc.Width = 64;
            sd.BufferDesc.Height = 64;
            sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.SampleDesc.Count = 1;
            sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.BufferCount = 2;
            sd.OutputWindow = hwnd;
            sd.Windowed = TRUE;
            sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

            IDXGISwapChain* swap = nullptr;
            ID3D11Device* device = nullptr;
            ID3D11DeviceContext* context = nullptr;
            D3D_FEATURE_LEVEL level{};

            if (SUCCEEDED(create(
                nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                nullptr, 0, D3D11_SDK_VERSION,
                &sd, &swap, &device, &level, &context)) && swap)
            {
                auto** vtable = *reinterpret_cast<void***>(swap);
                result.present = vtable[8];
                result.resize = vtable[13];

                IDXGISwapChain1* swap1 = nullptr;
                if (SUCCEEDED(swap->QueryInterface(__uuidof(IDXGISwapChain1), reinterpret_cast<void**>(&swap1))) && swap1)
                {
                    auto** vtable1 = *reinterpret_cast<void***>(swap1);
                    result.present1 = vtable1[22];
                    swap1->Release();
                }
            }

            if (context) context->Release();
            if (device) device->Release();
            if (swap) swap->Release();
            DestroyWindow(hwnd);
            return result;
        }

        template <class F>
        void* Address(F f)
        {
            return reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(f));
        }

        bool HookOne(void* target, void* detour, void** original)
        {
            return target &&
                   MH_CreateHook(target, detour, original) == MH_OK &&
                   MH_QueueEnableHook(target) == MH_OK;
        }

        void Arm()
        {
            WriteModuleInventory();

            const Targets targets = ResolveTargets();
            std::ostringstream acquisition;
            acquisition << "present_target=" << Ptr(targets.present) << "\n"
                        << "present1_target=" << Ptr(targets.present1) << "\n"
                        << "resize_target=" << Ptr(targets.resize) << "\n";

            if (!targets.present)
            {
                acquisition << "status=no_present_target\n";
                Write(L"renderer_acquisition.txt", acquisition.str());
                return;
            }

            const MH_STATUS init = MH_Initialize();
            if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED)
            {
                acquisition << "status=minhook_init_failed code=" << static_cast<int>(init) << "\n";
                Write(L"renderer_acquisition.txt", acquisition.str());
                return;
            }

            void* originalPresent = nullptr;
            void* originalPresent1 = nullptr;
            void* originalResize = nullptr;

            bool ok = HookOne(targets.present, Address(&HookPresent), &originalPresent);
            g_present = reinterpret_cast<PresentFn>(originalPresent);

            if (targets.present1 && targets.present1 != targets.present)
            {
                ok = HookOne(targets.present1, Address(&HookPresent1), &originalPresent1) && ok;
                g_present1 = reinterpret_cast<Present1Fn>(originalPresent1);
            }

            if (targets.resize)
            {
                ok = HookOne(targets.resize, Address(&HookResizeBuffers), &originalResize) && ok;
                g_resizeBuffers = reinterpret_cast<ResizeBuffersFn>(originalResize);
            }

            if (!ok || MH_ApplyQueued() != MH_OK)
            {
                acquisition << "status=hook_install_failed\n";
                Write(L"renderer_acquisition.txt", acquisition.str());
                return;
            }

            acquisition << "status=armed\n";
            Write(L"renderer_acquisition.txt", acquisition.str());
            g_armed = true;
        }
    }

    bool ArmRendererForensics() noexcept
    {
        try
        {
            std::call_once(g_once, Arm);
            return g_armed;
        }
        catch (...)
        {
            return false;
        }
    }
}
