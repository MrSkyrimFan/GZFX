#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <iterator>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>

namespace
{
    namespace fs = std::filesystem;

    using CoreCreateDeviceFn = int(WINAPI*)(
        std::int32_t, int, int, LPCSTR,
        int, int, int, int, int, int);

    using CoreCreateLayeredDeviceFn = HRESULT(WINAPI*)(
        const void*, DWORD, const void*, REFIID, void**);

    using CoreGetLayeredDeviceSizeFn = SIZE_T(WINAPI*)(
        const void*, DWORD);

    using CoreRegisterLayersFn = HRESULT(WINAPI*)(
        const void*, DWORD);

    using GzfxInitializeFn = bool(*)() noexcept;
    using GzfxAttachPrimaryFn = bool(*)(
        IDXGISwapChain*,
        ID3D11Device*,
        ID3D11DeviceContext*) noexcept;

    using GzfxStartupEventFn = void(*)(
        const char*,
        std::uint64_t,
        std::uint32_t,
        std::int32_t,
        std::uintptr_t,
        std::uintptr_t) noexcept;

    struct PendingEvent
    {
        const char* name = nullptr;
        std::uint64_t qpc = 0;
        std::uint32_t thread = 0;
        std::int32_t result = 0;
        std::uintptr_t pointerA = 0;
        std::uintptr_t pointerB = 0;
    };

    HMODULE g_proxyModule = nullptr;
    HMODULE g_realD3D11 = nullptr;
    HMODULE g_gzfxModule = nullptr;

    PFN_D3D11_CREATE_DEVICE g_createDevice = nullptr;
    PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN
        g_createDeviceAndSwapChain = nullptr;

    CoreCreateDeviceFn g_coreCreateDevice = nullptr;
    CoreCreateLayeredDeviceFn g_coreCreateLayeredDevice = nullptr;
    CoreGetLayeredDeviceSizeFn g_coreGetLayeredDeviceSize = nullptr;
    CoreRegisterLayersFn g_coreRegisterLayers = nullptr;

    GzfxInitializeFn g_gzfxInitialize = nullptr;
    GzfxAttachPrimaryFn g_gzfxAttachPrimary = nullptr;
    GzfxStartupEventFn g_gzfxStartupEvent = nullptr;

    std::once_flag g_realOnce;
    std::once_flag g_gzfxOnce;

    std::array<PendingEvent, 16> g_pending{};
    std::atomic<std::uint32_t> g_pendingCount{0};
    std::atomic<bool> g_firstExportLogged{false};

    thread_local bool g_insideGzfxInitialization = false;

    std::uint64_t QpcNow() noexcept
    {
        LARGE_INTEGER value{};
        QueryPerformanceCounter(&value);
        return static_cast<std::uint64_t>(
            value.QuadPart);
    }

    void BufferEvent(
        const char* const name,
        const std::int32_t result = 0,
        const std::uintptr_t pointerA = 0,
        const std::uintptr_t pointerB = 0) noexcept
    {
        const std::uint32_t index =
            g_pendingCount.fetch_add(
                1,
                std::memory_order_relaxed);

        if (index >= g_pending.size())
        {
            return;
        }

        PendingEvent& event =
            g_pending[index];

        event.name = name;
        event.qpc = QpcNow();
        event.thread = GetCurrentThreadId();
        event.result = result;
        event.pointerA = pointerA;
        event.pointerB = pointerB;
    }

    void EmitEvent(
        const char* const name,
        const std::int32_t result = 0,
        const std::uintptr_t pointerA = 0,
        const std::uintptr_t pointerB = 0) noexcept
    {
        if (g_gzfxStartupEvent != nullptr)
        {
            g_gzfxStartupEvent(
                name,
                QpcNow(),
                GetCurrentThreadId(),
                result,
                pointerA,
                pointerB);
            return;
        }

        BufferEvent(
            name,
            result,
            pointerA,
            pointerB);
    }

    void ReplayPendingEvents() noexcept
    {
        if (g_gzfxStartupEvent == nullptr)
        {
            return;
        }

        const std::uint32_t count =
            (std::min)(
                g_pendingCount.load(
                    std::memory_order_relaxed),
                static_cast<std::uint32_t>(
                    g_pending.size()));

        for (std::uint32_t i = 0;
             i < count;
             ++i)
        {
            const PendingEvent& event =
                g_pending[i];

            if (event.name == nullptr)
            {
                continue;
            }

            g_gzfxStartupEvent(
                event.name,
                event.qpc,
                event.thread,
                event.result,
                event.pointerA,
                event.pointerB);
        }

        g_pendingCount.store(
            0,
            std::memory_order_relaxed);
    }

    fs::path ModulePath(
        HMODULE const module)
    {
        std::wstring value(
            32768,
            L'\0');

        const DWORD written =
            GetModuleFileNameW(
                module,
                value.data(),
                static_cast<DWORD>(
                    value.size()));

        if (written == 0 ||
            static_cast<std::size_t>(written) >=
                value.size())
        {
            return {};
        }

        value.resize(written);
        return fs::path(value);
    }

    void ResolveRealD3D11() noexcept
    {
        wchar_t systemDirectory[32768]{};

        const UINT written =
            GetSystemDirectoryW(
                systemDirectory,
                static_cast<UINT>(
                    std::size(systemDirectory)));

        if (written == 0 ||
            written >= std::size(systemDirectory))
        {
            return;
        }

        const fs::path realPath =
            fs::path(systemDirectory) /
            L"d3d11.dll";

        g_realD3D11 =
            LoadLibraryW(
                realPath.c_str());

        if (g_realD3D11 == nullptr)
        {
            return;
        }

        g_createDevice =
            reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(
                GetProcAddress(
                    g_realD3D11,
                    "D3D11CreateDevice"));

        g_createDeviceAndSwapChain =
            reinterpret_cast<
                PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN>(
                    GetProcAddress(
                        g_realD3D11,
                        "D3D11CreateDeviceAndSwapChain"));

        g_coreCreateDevice =
            reinterpret_cast<CoreCreateDeviceFn>(
                GetProcAddress(
                    g_realD3D11,
                    "D3D11CoreCreateDevice"));

        g_coreCreateLayeredDevice =
            reinterpret_cast<CoreCreateLayeredDeviceFn>(
                GetProcAddress(
                    g_realD3D11,
                    "D3D11CoreCreateLayeredDevice"));

        g_coreGetLayeredDeviceSize =
            reinterpret_cast<CoreGetLayeredDeviceSizeFn>(
                GetProcAddress(
                    g_realD3D11,
                    "D3D11CoreGetLayeredDeviceSize"));

        g_coreRegisterLayers =
            reinterpret_cast<CoreRegisterLayersFn>(
                GetProcAddress(
                    g_realD3D11,
                    "D3D11CoreRegisterLayers"));
    }

    bool EnsureRealD3D11() noexcept
    {
        try
        {
            std::call_once(
                g_realOnce,
                ResolveRealD3D11);

            return
                g_realD3D11 != nullptr &&
                g_createDevice != nullptr &&
                g_createDeviceAndSwapChain != nullptr &&
                g_coreCreateDevice != nullptr &&
                g_coreCreateLayeredDevice != nullptr &&
                g_coreGetLayeredDeviceSize != nullptr &&
                g_coreRegisterLayers != nullptr;
        }
        catch (...)
        {
            return false;
        }
    }

    void ResolveGzfxExports() noexcept
    {
        const fs::path proxyPath =
            ModulePath(
                g_proxyModule);

        if (proxyPath.empty())
        {
            return;
        }

        EmitEvent(
            "gzfx_load_begin");

        const fs::path gzfxPath =
            proxyPath.parent_path() /
            L"GZFX.dll";

        g_gzfxModule =
            GetModuleHandleW(
                gzfxPath.filename().c_str());

        if (g_gzfxModule == nullptr)
        {
            g_gzfxModule =
                LoadLibraryW(
                    gzfxPath.c_str());
        }
        else
        {
            g_gzfxModule =
                LoadLibraryW(
                    gzfxPath.c_str());
        }

        if (g_gzfxModule == nullptr)
        {
            return;
        }

        g_gzfxInitialize =
            reinterpret_cast<GzfxInitializeFn>(
                GetProcAddress(
                    g_gzfxModule,
                    "GZFX_Initialize"));

        g_gzfxAttachPrimary =
            reinterpret_cast<GzfxAttachPrimaryFn>(
                GetProcAddress(
                    g_gzfxModule,
                    "GZFX_AttachPrimaryD3D11"));

        g_gzfxStartupEvent =
            reinterpret_cast<GzfxStartupEventFn>(
                GetProcAddress(
                    g_gzfxModule,
                    "GZFX_RecordBootstrapEvent"));

        ReplayPendingEvents();

        if (g_gzfxInitialize != nullptr)
        {
            g_insideGzfxInitialization = true;

            const bool initialized =
                g_gzfxInitialize();

            g_insideGzfxInitialization = false;

            EmitEvent(
                "gzfx_initialize_complete",
                initialized ? 0 : 1);
        }
    }

    void EnsureGzfxLoaded() noexcept
    {
        if (g_insideGzfxInitialization)
        {
            return;
        }

        try
        {
            std::call_once(
                g_gzfxOnce,
                ResolveGzfxExports);
        }
        catch (...)
        {
        }
    }

    void CompletePrimaryAttach(
        IDXGISwapChain* const swapChain,
        ID3D11Device* const device,
        ID3D11DeviceContext* const context) noexcept
    {
        if (g_insideGzfxInitialization)
        {
            return;
        }

        EnsureGzfxLoaded();

        if (g_gzfxAttachPrimary == nullptr ||
            device == nullptr ||
            context == nullptr)
        {
            return;
        }

        const bool attached =
            g_gzfxAttachPrimary(
                swapChain,
                device,
                context);

        EmitEvent(
            "primary_attach_complete",
            attached ? 0 : 1,
            reinterpret_cast<std::uintptr_t>(
                device),
            reinterpret_cast<std::uintptr_t>(
                context));
    }
}

extern "C" HRESULT WINAPI D3D11CreateDevice(
    IDXGIAdapter* const adapter,
    const D3D_DRIVER_TYPE driverType,
    const HMODULE software,
    const UINT flags,
    const D3D_FEATURE_LEVEL* const featureLevels,
    const UINT featureLevelCount,
    const UINT sdkVersion,
    ID3D11Device** const device,
    D3D_FEATURE_LEVEL* const featureLevel,
    ID3D11DeviceContext** const immediateContext)
{
    if (!g_firstExportLogged.exchange(
            true,
            std::memory_order_acq_rel))
    {
        EmitEvent(
            "first_d3d11_export_enter");
    }

    if (!EnsureRealD3D11() ||
        g_createDevice == nullptr)
    {
        return E_FAIL;
    }

    const HRESULT result =
        g_createDevice(
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

    EmitEvent(
        "real_device_creation_complete",
        result,
        reinterpret_cast<std::uintptr_t>(
            device != nullptr
                ? *device
                : nullptr),
        reinterpret_cast<std::uintptr_t>(
            immediateContext != nullptr
                ? *immediateContext
                : nullptr));

    if (SUCCEEDED(result) &&
        !g_insideGzfxInitialization)
    {
        CompletePrimaryAttach(
            nullptr,
            device != nullptr
                ? *device
                : nullptr,
            immediateContext != nullptr
                ? *immediateContext
                : nullptr);
    }

    return result;
}

extern "C" HRESULT WINAPI D3D11CreateDeviceAndSwapChain(
    IDXGIAdapter* const adapter,
    const D3D_DRIVER_TYPE driverType,
    const HMODULE software,
    const UINT flags,
    const D3D_FEATURE_LEVEL* const featureLevels,
    const UINT featureLevelCount,
    const UINT sdkVersion,
    const DXGI_SWAP_CHAIN_DESC* const swapChainDescription,
    IDXGISwapChain** const swapChain,
    ID3D11Device** const device,
    D3D_FEATURE_LEVEL* const featureLevel,
    ID3D11DeviceContext** const immediateContext)
{
    if (!g_firstExportLogged.exchange(
            true,
            std::memory_order_acq_rel))
    {
        EmitEvent(
            "first_d3d11_export_enter");
    }

    if (!EnsureRealD3D11() ||
        g_createDeviceAndSwapChain == nullptr)
    {
        return E_FAIL;
    }

    const HRESULT result =
        g_createDeviceAndSwapChain(
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

    EmitEvent(
        "real_device_creation_complete",
        result,
        reinterpret_cast<std::uintptr_t>(
            device != nullptr
                ? *device
                : nullptr),
        reinterpret_cast<std::uintptr_t>(
            swapChain != nullptr
                ? *swapChain
                : nullptr));

    if (SUCCEEDED(result) &&
        !g_insideGzfxInitialization)
    {
        CompletePrimaryAttach(
            swapChain != nullptr
                ? *swapChain
                : nullptr,
            device != nullptr
                ? *device
                : nullptr,
            immediateContext != nullptr
                ? *immediateContext
                : nullptr);
    }

    return result;
}

extern "C" int WINAPI D3D11CoreCreateDevice(
    const std::int32_t a,
    const int b,
    const int c,
    const LPCSTR moduleName,
    const int e,
    const int f,
    const int g,
    const int h,
    const int i,
    const int j)
{
    if (!EnsureRealD3D11() ||
        g_coreCreateDevice == nullptr)
    {
        return -1;
    }

    return g_coreCreateDevice(
        a, b, c, moduleName,
        e, f, g, h, i, j);
}

extern "C" HRESULT WINAPI D3D11CoreCreateLayeredDevice(
    const void* const unknown0,
    const DWORD unknown1,
    const void* const unknown2,
    REFIID riid,
    void** const object)
{
    if (!EnsureRealD3D11() ||
        g_coreCreateLayeredDevice == nullptr)
    {
        return E_FAIL;
    }

    return g_coreCreateLayeredDevice(
        unknown0,
        unknown1,
        unknown2,
        riid,
        object);
}

extern "C" SIZE_T WINAPI D3D11CoreGetLayeredDeviceSize(
    const void* const unknown0,
    const DWORD unknown1)
{
    if (!EnsureRealD3D11() ||
        g_coreGetLayeredDeviceSize == nullptr)
    {
        return 0;
    }

    return g_coreGetLayeredDeviceSize(
        unknown0,
        unknown1);
}

extern "C" HRESULT WINAPI D3D11CoreRegisterLayers(
    const void* const unknown0,
    const DWORD unknown1)
{
    if (!EnsureRealD3D11() ||
        g_coreRegisterLayers == nullptr)
    {
        return E_FAIL;
    }

    return g_coreRegisterLayers(
        unknown0,
        unknown1);
}

BOOL APIENTRY DllMain(
    HMODULE const module,
    const DWORD reason,
    LPVOID)
{
    if (reason ==
        DLL_PROCESS_ATTACH)
    {
        g_proxyModule = module;

        DisableThreadLibraryCalls(
            module);

        BufferEvent(
            "d3d11_proxy_process_attach");
    }

    return TRUE;
}
