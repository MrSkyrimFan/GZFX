#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <MinHook.h>
#include <intrin.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>

namespace
{
    namespace fs = std::filesystem;

    using D3D11On12CreateDeviceFn = HRESULT(WINAPI*)(
        IUnknown*,
        UINT,
        const D3D_FEATURE_LEVEL*,
        UINT,
        IUnknown* const*,
        UINT,
        UINT,
        ID3D11Device**,
        ID3D11DeviceContext**,
        D3D_FEATURE_LEVEL*);

    using ExitProcessFn = VOID(WINAPI*)(UINT);

    using GzfxInitializeFn = bool(*)() noexcept;
    using GzfxGetInitializationStateFn =
        std::uint32_t(*)() noexcept;
    using GzfxAttachPrimaryFn = bool(*)(
        IDXGISwapChain*,
        ID3D11Device*,
        ID3D11DeviceContext*) noexcept;
    using GzfxShutdownFn = void(*)() noexcept;

    using GzfxStartupEventFn = void(*)(
        const char*,
        std::uint64_t,
        std::uint32_t,
        std::int32_t,
        std::uintptr_t,
        std::uintptr_t) noexcept;

    enum class PrimaryDecision : std::int32_t
    {
        Accepted = 0,
        CallerNotMainExecutable = 1,
        SoftwareDriver = 2,
        BasicRenderAdapter = 3,
        ForeignWindow = 4,
        MissingObjects = 5
    };

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
    D3D11On12CreateDeviceFn
        g_on12CreateDevice = nullptr;

    GzfxInitializeFn g_gzfxInitialize = nullptr;
    GzfxGetInitializationStateFn
        g_gzfxGetInitializationState = nullptr;
    GzfxAttachPrimaryFn g_gzfxAttachPrimary = nullptr;
    GzfxShutdownFn g_gzfxShutdown = nullptr;
    GzfxStartupEventFn g_gzfxStartupEvent = nullptr;

    ExitProcessFn g_exitProcess = nullptr;

    std::once_flag g_realOnce;
    std::once_flag g_gzfxOnce;
    std::once_flag g_exitHookOnce;

    std::array<PendingEvent, 32> g_pending{};
    std::atomic<std::uint32_t> g_pendingCount{0};
    std::atomic<bool> g_firstExportLogged{false};
    std::atomic<bool> g_exitShutdownStarted{false};

    thread_local bool g_insideGzfxInitialization = false;

    std::uint64_t QpcNow() noexcept
    {
        LARGE_INTEGER value{};
        QueryPerformanceCounter(&value);

        return
            static_cast<std::uint64_t>(
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
            static_cast<std::size_t>(
                written) >= value.size())
        {
            return {};
        }

        value.resize(written);

        return fs::path(value);
    }

    bool TestAllowsSoftwarePrimary() noexcept
    {
        wchar_t value[8]{};

        return
            GetEnvironmentVariableW(
                L"GZFX_TEST_ALLOW_SOFTWARE_PRIMARY",
                value,
                static_cast<DWORD>(
                    std::size(value))) > 0;
    }

    bool CallerIsMainExecutable(
        void* const returnAddress) noexcept
    {
        if (returnAddress == nullptr)
        {
            return false;
        }

        HMODULE callerModule = nullptr;

        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(
                    returnAddress),
                &callerModule) ||
            callerModule == nullptr)
        {
            return false;
        }

        return
            callerModule ==
            GetModuleHandleW(nullptr);
    }

    bool WindowBelongsToCurrentProcess(
        HWND const window) noexcept
    {
        if (window == nullptr)
        {
            return true;
        }

        DWORD pid = 0;

        GetWindowThreadProcessId(
            window,
            &pid);

        return
            pid ==
            GetCurrentProcessId();
    }

    bool IsMicrosoftBasicRenderDriver(
        ID3D11Device* const device) noexcept
    {
        if (device == nullptr)
        {
            return false;
        }

        IDXGIDevice* dxgiDevice = nullptr;
        IDXGIAdapter* adapter = nullptr;
        bool basic = false;

        if (SUCCEEDED(
                device->QueryInterface(
                    __uuidof(IDXGIDevice),
                    reinterpret_cast<void**>(
                        &dxgiDevice))) &&
            dxgiDevice != nullptr &&
            SUCCEEDED(
                dxgiDevice->GetAdapter(
                    &adapter)) &&
            adapter != nullptr)
        {
            DXGI_ADAPTER_DESC desc{};

            if (SUCCEEDED(
                    adapter->GetDesc(
                        &desc)))
            {
                basic =
                    desc.VendorId == 0x1414 &&
                    desc.DeviceId == 0x008C;
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

        return basic;
    }

    PrimaryDecision QualifyPrimary(
        const D3D_DRIVER_TYPE driverType,
        ID3D11Device* const device,
        ID3D11DeviceContext* const context,
        const DXGI_SWAP_CHAIN_DESC* const swapDesc,
        void* const caller) noexcept
    {
        if (device == nullptr ||
            context == nullptr)
        {
            return
                PrimaryDecision::
                    MissingObjects;
        }

        if (!CallerIsMainExecutable(
                caller))
        {
            return
                PrimaryDecision::
                    CallerNotMainExecutable;
        }

        if (!TestAllowsSoftwarePrimary() &&
            (
                driverType ==
                    D3D_DRIVER_TYPE_WARP ||
                driverType ==
                    D3D_DRIVER_TYPE_REFERENCE ||
                driverType ==
                    D3D_DRIVER_TYPE_SOFTWARE ||
                driverType ==
                    D3D_DRIVER_TYPE_NULL
            ))
        {
            return
                PrimaryDecision::
                    SoftwareDriver;
        }

        if (!TestAllowsSoftwarePrimary() &&
            IsMicrosoftBasicRenderDriver(
                device))
        {
            return
                PrimaryDecision::
                    BasicRenderAdapter;
        }

        if (swapDesc != nullptr &&
            !WindowBelongsToCurrentProcess(
                swapDesc->OutputWindow))
        {
            return
                PrimaryDecision::
                    ForeignWindow;
        }

        return
            PrimaryDecision::
                Accepted;
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
            written >=
                std::size(systemDirectory))
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
            reinterpret_cast<
                PFN_D3D11_CREATE_DEVICE>(
                    GetProcAddress(
                        g_realD3D11,
                        "D3D11CreateDevice"));

        g_createDeviceAndSwapChain =
            reinterpret_cast<
                PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN>(
                    GetProcAddress(
                        g_realD3D11,
                        "D3D11CreateDeviceAndSwapChain"));

        g_on12CreateDevice =
            reinterpret_cast<
                D3D11On12CreateDeviceFn>(
                    GetProcAddress(
                        g_realD3D11,
                        "D3D11On12CreateDevice"));
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
                g_createDeviceAndSwapChain !=
                    nullptr;
        }
        catch (...)
        {
            return false;
        }
    }

    void ResolveExportsFromLoadedGzfx(
        HMODULE const module) noexcept
    {
        if (module == nullptr)
        {
            return;
        }

        g_gzfxModule = module;

        g_gzfxInitialize =
            reinterpret_cast<
                GzfxInitializeFn>(
                    GetProcAddress(
                        g_gzfxModule,
                        "GZFX_Initialize"));

        g_gzfxGetInitializationState =
            reinterpret_cast<
                GzfxGetInitializationStateFn>(
                    GetProcAddress(
                        g_gzfxModule,
                        "GZFX_GetInitializationState"));

        g_gzfxAttachPrimary =
            reinterpret_cast<
                GzfxAttachPrimaryFn>(
                    GetProcAddress(
                        g_gzfxModule,
                        "GZFX_AttachPrimaryD3D11"));

        g_gzfxShutdown =
            reinterpret_cast<
                GzfxShutdownFn>(
                    GetProcAddress(
                        g_gzfxModule,
                        "GZFX_Shutdown"));

        g_gzfxStartupEvent =
            reinterpret_cast<
                GzfxStartupEventFn>(
                    GetProcAddress(
                        g_gzfxModule,
                        "GZFX_RecordBootstrapEvent"));

        ReplayPendingEvents();
    }

    void ObserveExistingGzfx() noexcept
    {
        if (g_gzfxModule != nullptr)
        {
            return;
        }

        const HMODULE existing =
            GetModuleHandleW(
                L"GZFX.dll");

        if (existing != nullptr)
        {
            const fs::path existingPath =
                ModulePath(
                    existing);

            // Own one graphics-bootstrap lifetime reference so cached GZFX
            // lifecycle exports remain valid through ExitProcess even if the
            // original loader releases its reference first.
            HMODULE owned =
                existingPath.empty()
                    ? nullptr
                    : LoadLibraryW(
                        existingPath.c_str());

            ResolveExportsFromLoadedGzfx(
                owned != nullptr
                    ? owned
                    : existing);
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

        // LoadLibrary owns one bootstrap lifetime reference through process exit.
        HMODULE const module =
            LoadLibraryW(
                gzfxPath.c_str());

        if (module == nullptr)
        {
            return;
        }

        ResolveExportsFromLoadedGzfx(
            module);

        const std::uint32_t state =
            g_gzfxGetInitializationState !=
                    nullptr
                ? g_gzfxGetInitializationState()
                : 0;

        if (g_gzfxInitialize != nullptr &&
            state == 0)
        {
            g_insideGzfxInitialization =
                true;

            const bool initialized =
                g_gzfxInitialize();

            g_insideGzfxInitialization =
                false;

            EmitEvent(
                "gzfx_initialize_complete",
                initialized ? 0 : 1);
        }
        else if (state == 1)
        {
            EmitEvent(
                "gzfx_initialization_already_in_progress");
        }
        else if (state == 2)
        {
            EmitEvent(
                "gzfx_already_initialized");
        }
        else if (state == 3)
        {
            EmitEvent(
                "gzfx_initialization_failed",
                1);
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

    VOID WINAPI HookExitProcess(
        const UINT exitCode)
    {
        if (!g_exitShutdownStarted.exchange(
                true,
                std::memory_order_acq_rel))
        {
            EmitEvent(
                "process_exit_intercepted",
                static_cast<std::int32_t>(
                    exitCode));

            const std::uint32_t state =
                g_gzfxGetInitializationState !=
                        nullptr
                    ? g_gzfxGetInitializationState()
                    : 0;

            if (state == 2 &&
                g_gzfxShutdown != nullptr)
            {
                g_gzfxShutdown();
            }
        }

        if (g_exitProcess != nullptr)
        {
            g_exitProcess(
                exitCode);
        }

        TerminateProcess(
            GetCurrentProcess(),
            exitCode);
    }

    void InstallExitHook() noexcept
    {
        const MH_STATUS init =
            MH_Initialize();

        if (init != MH_OK &&
            init !=
                MH_ERROR_ALREADY_INITIALIZED)
        {
            return;
        }

        const HMODULE kernel32 =
            GetModuleHandleW(
                L"kernel32.dll");

        if (kernel32 == nullptr)
        {
            return;
        }

        void* const target =
            reinterpret_cast<void*>(
                GetProcAddress(
                    kernel32,
                    "ExitProcess"));

        if (target == nullptr)
        {
            return;
        }

        void* original = nullptr;

        if (MH_CreateHook(
                target,
                reinterpret_cast<void*>(
                    &HookExitProcess),
                &original) != MH_OK)
        {
            return;
        }

        g_exitProcess =
            reinterpret_cast<
                ExitProcessFn>(
                    original);

        if (MH_EnableHook(
                target) != MH_OK)
        {
            g_exitProcess = nullptr;
            return;
        }

        EmitEvent(
            "exit_process_hook_armed");
    }

    void EnsureExitHook() noexcept
    {
        try
        {
            std::call_once(
                g_exitHookOnce,
                InstallExitHook);
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

        const std::uint32_t state =
            g_gzfxGetInitializationState !=
                    nullptr
                ? g_gzfxGetInitializationState()
                : 0;

        if (state != 2 ||
            g_gzfxAttachPrimary == nullptr ||
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
            reinterpret_cast<
                std::uintptr_t>(
                    device),
            reinterpret_cast<
                std::uintptr_t>(
                    context));
    }

    void LogFirstExport() noexcept
    {
        if (!g_firstExportLogged.exchange(
                true,
                std::memory_order_acq_rel))
        {
            EmitEvent(
                "first_d3d11_export_enter");
        }
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
    void* const caller =
        _ReturnAddress();

    LogFirstExport();
    EnsureExitHook();

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

    ObserveExistingGzfx();

    if (SUCCEEDED(result) &&
        !g_insideGzfxInitialization)
    {
        const PrimaryDecision decision =
            QualifyPrimary(
                driverType,
                device != nullptr
                    ? *device
                    : nullptr,
                immediateContext != nullptr
                    ? *immediateContext
                    : nullptr,
                nullptr,
                caller);

        EmitEvent(
            decision ==
                    PrimaryDecision::Accepted
                ? "primary_candidate_accepted"
                : "primary_candidate_rejected",
            static_cast<std::int32_t>(
                decision),
            reinterpret_cast<std::uintptr_t>(
                device != nullptr
                    ? *device
                    : nullptr),
            reinterpret_cast<std::uintptr_t>(
                immediateContext != nullptr
                    ? *immediateContext
                    : nullptr));

        if (decision ==
            PrimaryDecision::Accepted)
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
    void* const caller =
        _ReturnAddress();

    LogFirstExport();
    EnsureExitHook();

    if (!EnsureRealD3D11() ||
        g_createDeviceAndSwapChain ==
            nullptr)
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

    ObserveExistingGzfx();

    if (SUCCEEDED(result) &&
        !g_insideGzfxInitialization)
    {
        const PrimaryDecision decision =
            QualifyPrimary(
                driverType,
                device != nullptr
                    ? *device
                    : nullptr,
                immediateContext != nullptr
                    ? *immediateContext
                    : nullptr,
                swapChainDescription,
                caller);

        EmitEvent(
            decision ==
                    PrimaryDecision::Accepted
                ? "primary_candidate_accepted"
                : "primary_candidate_rejected",
            static_cast<std::int32_t>(
                decision),
            reinterpret_cast<std::uintptr_t>(
                device != nullptr
                    ? *device
                    : nullptr),
            reinterpret_cast<std::uintptr_t>(
                swapChain != nullptr
                    ? *swapChain
                    : nullptr));

        if (decision ==
            PrimaryDecision::Accepted)
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
    }

    return result;
}

extern "C" HRESULT WINAPI D3D11On12CreateDevice(
    IUnknown* const d3d12Device,
    const UINT flags,
    const D3D_FEATURE_LEVEL* const featureLevels,
    const UINT featureLevelCount,
    IUnknown* const* const commandQueues,
    const UINT queueCount,
    const UINT nodeMask,
    ID3D11Device** const device,
    ID3D11DeviceContext** const immediateContext,
    D3D_FEATURE_LEVEL* const chosenFeatureLevel)
{
    LogFirstExport();
    EnsureExitHook();

    if (!EnsureRealD3D11() ||
        g_on12CreateDevice == nullptr)
    {
        return E_NOINTERFACE;
    }

    EmitEvent(
        "d3d11on12_forward");

    return
        g_on12CreateDevice(
            d3d12Device,
            flags,
            featureLevels,
            featureLevelCount,
            commandQueues,
            queueCount,
            nodeMask,
            device,
            immediateContext,
            chosenFeatureLevel);
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
