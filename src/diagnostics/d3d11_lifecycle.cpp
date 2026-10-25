#include "gzfx/diagnostics/d3d11_lifecycle.hpp"
#include "gzfx/diagnostics/pipeline_cartography.hpp"
#include "gzfx/diagnostics/renderer_forensics.hpp"
#include "gzfx/paths.hpp"
#include "gzfx/render/renderer_execution_service.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_3.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>

namespace gzfx::diagnostics
{
    namespace fs = std::filesystem;

    namespace
    {
        using D3D11CreateDeviceFn = HRESULT(WINAPI*)(
            IDXGIAdapter*,
            D3D_DRIVER_TYPE,
            HMODULE,
            UINT,
            const D3D_FEATURE_LEVEL*,
            UINT,
            UINT,
            ID3D11Device**,
            D3D_FEATURE_LEVEL*,
            ID3D11DeviceContext**);

        using D3D11CreateDeviceAndSwapChainFn = HRESULT(WINAPI*)(
            IDXGIAdapter*,
            D3D_DRIVER_TYPE,
            HMODULE,
            UINT,
            const D3D_FEATURE_LEVEL*,
            UINT,
            UINT,
            const DXGI_SWAP_CHAIN_DESC*,
            IDXGISwapChain**,
            ID3D11Device**,
            D3D_FEATURE_LEVEL*,
            ID3D11DeviceContext**);

        using CreateDXGIFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
        using CreateDXGIFactory1Fn = HRESULT(WINAPI*)(REFIID, void**);
        using CreateDXGIFactory2Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);

        D3D11CreateDeviceFn g_originalCreateDevice = nullptr;
        D3D11CreateDeviceAndSwapChainFn g_originalCreateDeviceAndSwapChain = nullptr;
        CreateDXGIFactoryFn g_originalCreateFactory = nullptr;
        CreateDXGIFactory1Fn g_originalCreateFactory1 = nullptr;
        CreateDXGIFactory2Fn g_originalCreateFactory2 = nullptr;

        std::once_flag g_armOnce;
        bool g_armResult = false;
        std::mutex g_logMutex;

        std::atomic<std::uint64_t> g_createDeviceCalls{ 0 };
        std::atomic<std::uint64_t> g_createDeviceAndSwapChainCalls{ 0 };
        std::atomic<std::uint64_t> g_createFactoryCalls{ 0 };
        std::atomic<std::uint64_t> g_createFactory1Calls{ 0 };
        std::atomic<std::uint64_t> g_createFactory2Calls{ 0 };

        std::string Timestamp()
        {
            const auto now = std::chrono::system_clock::now();
            const std::time_t time = std::chrono::system_clock::to_time_t(now);
            std::tm localTime{};
            localtime_s(&localTime, &time);

            std::ostringstream stream;
            stream << std::put_time(&localTime, "%Y-%m-%dT%H:%M:%S");
            return stream.str();
        }

        std::string PointerString(const void* pointer)
        {
            std::ostringstream stream;
            stream << "0x" << std::hex << std::uppercase
                   << reinterpret_cast<std::uintptr_t>(pointer);
            return stream.str();
        }

        std::string HResultString(const HRESULT value)
        {
            std::ostringstream stream;
            stream << "0x" << std::hex << std::uppercase
                   << static_cast<std::uint32_t>(value);
            return stream.str();
        }

        void WriteDiagnostic(const std::string& line)
        {
            std::scoped_lock lock(g_logMutex);

            const fs::path diagnosticsRoot = paths::DiagnosticsRoot();
            if (diagnosticsRoot.empty())
            {
                return;
            }

            std::error_code error;
            fs::create_directories(diagnosticsRoot, error);
            if (error)
            {
                return;
            }

            std::ofstream log(
                diagnosticsRoot / L"d3d11_lifecycle.txt",
                std::ios::out | std::ios::app);

            if (!log)
            {
                return;
            }

            log << line << "\n";
        }

        bool PatchImport(
            HMODULE imageModule,
            const char* importedModuleName,
            const char* functionName,
            std::uintptr_t replacement,
            std::uintptr_t* original)
        {
            if (imageModule == nullptr || importedModuleName == nullptr ||
                functionName == nullptr || replacement == 0 || original == nullptr)
            {
                return false;
            }

            auto* base = reinterpret_cast<std::uint8_t*>(imageModule);
            auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            {
                return false;
            }

            auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE ||
                nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
            {
                return false;
            }

            const IMAGE_DATA_DIRECTORY& importDirectory =
                nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

            if (importDirectory.VirtualAddress == 0 || importDirectory.Size == 0)
            {
                return false;
            }

            auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
                base + importDirectory.VirtualAddress);

            for (; descriptor->Name != 0; ++descriptor)
            {
                const char* moduleName =
                    reinterpret_cast<const char*>(base + descriptor->Name);

                if (_stricmp(moduleName, importedModuleName) != 0)
                {
                    continue;
                }

                if (descriptor->OriginalFirstThunk == 0 || descriptor->FirstThunk == 0)
                {
                    return false;
                }

                auto* names = reinterpret_cast<IMAGE_THUNK_DATA64*>(
                    base + descriptor->OriginalFirstThunk);
                auto* slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(
                    base + descriptor->FirstThunk);

                for (; names->u1.AddressOfData != 0; ++names, ++slots)
                {
                    if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal))
                    {
                        continue;
                    }

                    auto* importByName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                        base + names->u1.AddressOfData);

                    const char* importedName =
                        reinterpret_cast<const char*>(importByName->Name);

                    if (std::strcmp(importedName, functionName) != 0)
                    {
                        continue;
                    }

                    DWORD oldProtection = 0;
                    if (!VirtualProtect(
                            &slots->u1.Function,
                            sizeof(slots->u1.Function),
                            PAGE_READWRITE,
                            &oldProtection))
                    {
                        return false;
                    }

                    *original = static_cast<std::uintptr_t>(slots->u1.Function);
                    slots->u1.Function = static_cast<ULONGLONG>(replacement);

                    DWORD ignoredProtection = 0;
                    VirtualProtect(
                        &slots->u1.Function,
                        sizeof(slots->u1.Function),
                        oldProtection,
                        &ignoredProtection);

                    FlushInstructionCache(
                        GetCurrentProcess(),
                        &slots->u1.Function,
                        sizeof(slots->u1.Function));

                    return true;
                }

                return false;
            }

            return false;
        }

        bool SoftwarePrimaryAllowed() noexcept
        {
            if (paths::TestOverrideActive())
            {
                return true;
            }

            wchar_t value[8]{};
            return
                GetEnvironmentVariableW(
                    L"GZFX_TEST_ALLOW_SOFTWARE_PRIMARY",
                    value,
                    static_cast<DWORD>(std::size(value))) > 0;
        }

        bool IsQualifiedPrimary(
            const D3D_DRIVER_TYPE driverType,
            ID3D11Device* const device,
            const DXGI_SWAP_CHAIN_DESC* const swapDesc) noexcept
        {
            if (device == nullptr)
            {
                return false;
            }

            if (!SoftwarePrimaryAllowed() &&
                (
                    driverType == D3D_DRIVER_TYPE_WARP ||
                    driverType == D3D_DRIVER_TYPE_REFERENCE ||
                    driverType == D3D_DRIVER_TYPE_SOFTWARE ||
                    driverType == D3D_DRIVER_TYPE_NULL
                ))
            {
                return false;
            }

            IDXGIDevice* dxgiDevice = nullptr;
            IDXGIAdapter* adapter = nullptr;
            bool qualified = true;

            if (SUCCEEDED(device->QueryInterface(
                    __uuidof(IDXGIDevice),
                    reinterpret_cast<void**>(&dxgiDevice))) &&
                dxgiDevice != nullptr &&
                SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) &&
                adapter != nullptr)
            {
                DXGI_ADAPTER_DESC desc{};
                if (SUCCEEDED(adapter->GetDesc(&desc)) &&
                    !SoftwarePrimaryAllowed() &&
                    desc.VendorId == 0x1414 &&
                    desc.DeviceId == 0x008C)
                {
                    qualified = false;
                }
            }

            if (adapter != nullptr)
            {
                adapter->Release();
            }

            if (dxgiDevice != nullptr)
            {
                dxgiDevice->Release();
            }

            if (!qualified)
            {
                return false;
            }

            if (swapDesc != nullptr &&
                swapDesc->OutputWindow != nullptr)
            {
                DWORD pid = 0;
                GetWindowThreadProcessId(
                    swapDesc->OutputWindow,
                    &pid);

                if (pid != GetCurrentProcessId())
                {
                    return false;
                }
            }

            return true;
        }

        void LogSwapChainDescription(IDXGISwapChain* swapChain)
        {
            if (swapChain == nullptr)
            {
                return;
            }

            DXGI_SWAP_CHAIN_DESC description{};
            if (FAILED(swapChain->GetDesc(&description)))
            {
                WriteDiagnostic("swap_chain_desc=unavailable");
                return;
            }

            std::ostringstream stream;
            stream
                << "swap_chain_desc"
                << " width=" << description.BufferDesc.Width
                << " height=" << description.BufferDesc.Height
                << " format=" << static_cast<unsigned>(description.BufferDesc.Format)
                << " buffer_count=" << description.BufferCount
                << " windowed=" << (description.Windowed ? "true" : "false")
                << " swap_effect=" << static_cast<unsigned>(description.SwapEffect)
                << " flags=" << description.Flags
                << " output_window=" << PointerString(description.OutputWindow);

            WriteDiagnostic(stream.str());
        }

        HRESULT WINAPI HookD3D11CreateDevice(
            IDXGIAdapter* adapter,
            D3D_DRIVER_TYPE driverType,
            HMODULE software,
            UINT flags,
            const D3D_FEATURE_LEVEL* featureLevels,
            UINT featureLevelCount,
            UINT sdkVersion,
            ID3D11Device** device,
            D3D_FEATURE_LEVEL* featureLevel,
            ID3D11DeviceContext** immediateContext)
        {
            const std::uint64_t call = ++g_createDeviceCalls;

            if (g_originalCreateDevice == nullptr)
            {
                return E_FAIL;
            }

            const HRESULT result = g_originalCreateDevice(
                adapter,
                driverType,
                software,
                flags,
                featureLevels,
                featureLevelCount,
                sdkVersion,
                device,
                featureLevel,
                immediateContext);

            std::ostringstream stream;
            stream
                << "event=D3D11CreateDevice"
                << " call=" << call
                << " timestamp=" << Timestamp()
                << " thread=" << GetCurrentThreadId()
                << " result=" << HResultString(result)
                << " device=" << PointerString(device != nullptr ? *device : nullptr)
                << " context=" << PointerString(
                    immediateContext != nullptr ? *immediateContext : nullptr)
                << " feature_level=0x" << std::hex << std::uppercase
                << static_cast<unsigned>(
                    featureLevel != nullptr
                        ? *featureLevel
                        : (device != nullptr && *device != nullptr
                            ? (*device)->GetFeatureLevel()
                            : static_cast<D3D_FEATURE_LEVEL>(0)));

            WriteDiagnostic(stream.str());

            if (SUCCEEDED(result) &&
                device != nullptr &&
                *device != nullptr)
            {
                const bool cartography =
                    pipeline_cartography::AttachDevice(
                        *device);

                WriteDiagnostic(
                    std::string("pipeline_cartography_device=") +
                    (cartography ? "armed" : "failed"));

                if (immediateContext != nullptr &&
                    *immediateContext != nullptr &&
                    IsQualifiedPrimary(
                        driverType,
                        *device,
                        nullptr))
                {
                    const bool attached =
                        AttachPrimaryRendererObjects(
                            nullptr,
                            *device,
                            *immediateContext);

                    WriteDiagnostic(
                        std::string("renderer_primary_attach=") +
                        (attached ? "armed" : "failed"));
                }
            }

            return result;
        }

        HRESULT WINAPI HookD3D11CreateDeviceAndSwapChain(
            IDXGIAdapter* adapter,
            D3D_DRIVER_TYPE driverType,
            HMODULE software,
            UINT flags,
            const D3D_FEATURE_LEVEL* featureLevels,
            UINT featureLevelCount,
            UINT sdkVersion,
            const DXGI_SWAP_CHAIN_DESC* swapChainDescription,
            IDXGISwapChain** swapChain,
            ID3D11Device** device,
            D3D_FEATURE_LEVEL* featureLevel,
            ID3D11DeviceContext** immediateContext)
        {
            const std::uint64_t call = ++g_createDeviceAndSwapChainCalls;

            if (g_originalCreateDeviceAndSwapChain == nullptr)
            {
                return E_FAIL;
            }

            const HRESULT result = g_originalCreateDeviceAndSwapChain(
                adapter,
                driverType,
                software,
                flags,
                featureLevels,
                featureLevelCount,
                sdkVersion,
                swapChainDescription,
                swapChain,
                device,
                featureLevel,
                immediateContext);

            std::ostringstream stream;
            stream
                << "event=D3D11CreateDeviceAndSwapChain"
                << " call=" << call
                << " timestamp=" << Timestamp()
                << " thread=" << GetCurrentThreadId()
                << " result=" << HResultString(result)
                << " device=" << PointerString(device != nullptr ? *device : nullptr)
                << " context=" << PointerString(
                    immediateContext != nullptr ? *immediateContext : nullptr)
                << " swap_chain=" << PointerString(
                    swapChain != nullptr ? *swapChain : nullptr);

            WriteDiagnostic(stream.str());

            if (SUCCEEDED(result) && swapChain != nullptr)
            {
                LogSwapChainDescription(*swapChain);
            }

            if (SUCCEEDED(result) &&
                device != nullptr &&
                *device != nullptr)
            {
                const bool cartography =
                    pipeline_cartography::AttachDevice(
                        *device);

                WriteDiagnostic(
                    std::string("pipeline_cartography_device=") +
                    (cartography ? "armed" : "failed"));

                if (immediateContext != nullptr &&
                    *immediateContext != nullptr &&
                    IsQualifiedPrimary(
                        driverType,
                        *device,
                        swapChainDescription))
                {
                    const bool attached =
                        AttachPrimaryRendererObjects(
                            swapChain != nullptr ? *swapChain : nullptr,
                            *device,
                            *immediateContext);

                    WriteDiagnostic(
                        std::string("renderer_primary_attach=") +
                        (attached ? "armed" : "failed"));
                }
            }

            return result;
        }

        HRESULT WINAPI HookCreateDXGIFactory(REFIID interfaceId, void** factory)
        {
            const std::uint64_t call = ++g_createFactoryCalls;
            if (g_originalCreateFactory == nullptr)
            {
                return E_FAIL;
            }

            const HRESULT result = g_originalCreateFactory(interfaceId, factory);

            std::ostringstream stream;
            stream
                << "event=CreateDXGIFactory"
                << " call=" << call
                << " timestamp=" << Timestamp()
                << " thread=" << GetCurrentThreadId()
                << " result=" << HResultString(result)
                << " factory=" << PointerString(factory != nullptr ? *factory : nullptr);

            WriteDiagnostic(stream.str());
            return result;
        }

        HRESULT WINAPI HookCreateDXGIFactory1(REFIID interfaceId, void** factory)
        {
            const std::uint64_t call = ++g_createFactory1Calls;
            if (g_originalCreateFactory1 == nullptr)
            {
                return E_FAIL;
            }

            const HRESULT result = g_originalCreateFactory1(interfaceId, factory);

            std::ostringstream stream;
            stream
                << "event=CreateDXGIFactory1"
                << " call=" << call
                << " timestamp=" << Timestamp()
                << " thread=" << GetCurrentThreadId()
                << " result=" << HResultString(result)
                << " factory=" << PointerString(factory != nullptr ? *factory : nullptr);

            WriteDiagnostic(stream.str());
            return result;
        }

        HRESULT WINAPI HookCreateDXGIFactory2(UINT flags, REFIID interfaceId, void** factory)
        {
            const std::uint64_t call = ++g_createFactory2Calls;
            if (g_originalCreateFactory2 == nullptr)
            {
                return E_FAIL;
            }

            const HRESULT result = g_originalCreateFactory2(flags, interfaceId, factory);

            std::ostringstream stream;
            stream
                << "event=CreateDXGIFactory2"
                << " call=" << call
                << " timestamp=" << Timestamp()
                << " thread=" << GetCurrentThreadId()
                << " result=" << HResultString(result)
                << " factory=" << PointerString(factory != nullptr ? *factory : nullptr)
                << " flags=" << flags;

            WriteDiagnostic(stream.str());
            return result;
        }

        void ArmProbe()
        {
            const HMODULE executable = GetModuleHandleW(nullptr);

            WriteDiagnostic("=== GZFX D3D11 lifecycle probe ===");
            WriteDiagnostic("timestamp=" + Timestamp());
            WriteDiagnostic("executable_module=" + PointerString(executable));
            WriteDiagnostic(
                "d3d11_module=" + PointerString(GetModuleHandleW(L"d3d11.dll")));
            WriteDiagnostic(
                "dxgi_module=" + PointerString(GetModuleHandleW(L"dxgi.dll")));

            std::uintptr_t originalCreateDevice = 0;
            std::uintptr_t originalCreateDeviceAndSwapChain = 0;
            std::uintptr_t originalCreateFactory = 0;
            std::uintptr_t originalCreateFactory1 = 0;
            std::uintptr_t originalCreateFactory2 = 0;

            const bool createDevice = PatchImport(
                executable,
                "d3d11.dll",
                "D3D11CreateDevice",
                reinterpret_cast<std::uintptr_t>(&HookD3D11CreateDevice),
                &originalCreateDevice);

            const bool createDeviceAndSwapChain = PatchImport(
                executable,
                "d3d11.dll",
                "D3D11CreateDeviceAndSwapChain",
                reinterpret_cast<std::uintptr_t>(&HookD3D11CreateDeviceAndSwapChain),
                &originalCreateDeviceAndSwapChain);

            const bool createFactory = PatchImport(
                executable,
                "dxgi.dll",
                "CreateDXGIFactory",
                reinterpret_cast<std::uintptr_t>(&HookCreateDXGIFactory),
                &originalCreateFactory);

            const bool createFactory1 = PatchImport(
                executable,
                "dxgi.dll",
                "CreateDXGIFactory1",
                reinterpret_cast<std::uintptr_t>(&HookCreateDXGIFactory1),
                &originalCreateFactory1);

            const bool createFactory2 = PatchImport(
                executable,
                "dxgi.dll",
                "CreateDXGIFactory2",
                reinterpret_cast<std::uintptr_t>(&HookCreateDXGIFactory2),
                &originalCreateFactory2);

            if (createDevice)
            {
                g_originalCreateDevice =
                    reinterpret_cast<D3D11CreateDeviceFn>(originalCreateDevice);
            }

            if (createDeviceAndSwapChain)
            {
                g_originalCreateDeviceAndSwapChain =
                    reinterpret_cast<D3D11CreateDeviceAndSwapChainFn>(
                        originalCreateDeviceAndSwapChain);
            }

            if (createFactory)
            {
                g_originalCreateFactory =
                    reinterpret_cast<CreateDXGIFactoryFn>(originalCreateFactory);
            }

            if (createFactory1)
            {
                g_originalCreateFactory1 =
                    reinterpret_cast<CreateDXGIFactory1Fn>(originalCreateFactory1);
            }

            if (createFactory2)
            {
                g_originalCreateFactory2 =
                    reinterpret_cast<CreateDXGIFactory2Fn>(originalCreateFactory2);
            }

            WriteDiagnostic(
                std::string("import.D3D11CreateDevice=") +
                (createDevice ? "armed" : "not_found"));
            WriteDiagnostic(
                std::string("import.D3D11CreateDeviceAndSwapChain=") +
                (createDeviceAndSwapChain ? "armed" : "not_found"));
            WriteDiagnostic(
                std::string("import.CreateDXGIFactory=") +
                (createFactory ? "armed" : "not_found"));
            WriteDiagnostic(
                std::string("import.CreateDXGIFactory1=") +
                (createFactory1 ? "armed" : "not_found"));
            WriteDiagnostic(
                std::string("import.CreateDXGIFactory2=") +
                (createFactory2 ? "armed" : "not_found"));

            g_armResult =
                createDevice ||
                createDeviceAndSwapChain ||
                createFactory ||
                createFactory1 ||
                createFactory2;

            WriteDiagnostic(
                std::string("probe_status=") +
                (g_armResult ? "armed" : "no_supported_imports"));
            WriteDiagnostic("====================================");
        }
    }

    bool ArmD3D11LifecycleProbe() noexcept
    {
        try
        {
            std::call_once(g_armOnce, ArmProbe);
            return g_armResult;
        }
        catch (...)
        {
            return false;
        }
    }
}
