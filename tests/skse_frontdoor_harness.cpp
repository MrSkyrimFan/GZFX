#include "gzfx/skse_api.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
    namespace fs = std::filesystem;

    using PreloadFn =
        bool(*)(
            const gzfx::skse::Interface*);

    using LoadFn =
        bool(*)(
            const gzfx::skse::Interface*);

    using ShutdownFn =
        void(*)() noexcept;

    using InitializationStateFn =
        std::uint32_t(*)() noexcept;

    gzfx::skse::MessagingInterface::EventCallback
        g_listener = nullptr;

    bool g_messagingQueried = false;
    bool g_trampolineQueried = false;

    alignas(16)
    std::array<std::byte, 4096> g_branchPool{};

    alignas(16)
    std::array<std::byte, 4096> g_localPool{};

    gzfx::skse::PluginHandle GetPluginHandle()
    {
        return 0x31u;
    }

    std::uint32_t GetReleaseIndex()
    {
        return
            gzfx::skse::
                kSkseReleaseIndex_2_3_1;
    }

    const gzfx::skse::PluginInfo*
    GetPluginInfo(
        const char*)
    {
        return nullptr;
    }

    bool RegisterListener(
        const gzfx::skse::PluginHandle listener,
        const char* sender,
        const gzfx::skse::MessagingInterface::
            EventCallback handler)
    {
        if (listener != 0x31u ||
            sender == nullptr ||
            std::string(sender) != "SKSE" ||
            handler == nullptr)
        {
            return false;
        }

        g_listener = handler;
        return true;
    }

    bool Dispatch(
        gzfx::skse::PluginHandle,
        std::uint32_t,
        void*,
        std::uint32_t,
        const char*)
    {
        return true;
    }

    void* GetEventDispatcher(
        std::uint32_t)
    {
        return nullptr;
    }

    void* AllocateBranch(
        gzfx::skse::PluginHandle plugin,
        std::size_t size)
    {
        return
            plugin == 0x31u &&
            size <= g_branchPool.size()
                ? g_branchPool.data()
                : nullptr;
    }

    void* AllocateLocal(
        gzfx::skse::PluginHandle plugin,
        std::size_t size)
    {
        return
            plugin == 0x31u &&
            size <= g_localPool.size()
                ? g_localPool.data()
                : nullptr;
    }

    gzfx::skse::MessagingInterface
        g_messaging =
    {
        2,
        &RegisterListener,
        &Dispatch,
        &GetEventDispatcher
    };

    gzfx::skse::TrampolineInterface
        g_trampoline =
    {
        1,
        &AllocateBranch,
        &AllocateLocal
    };

    void* QueryInterface(
        const std::uint32_t id)
    {
        if (id == gzfx::skse::Messaging)
        {
            g_messagingQueried = true;
            return &g_messaging;
        }

        if (id == gzfx::skse::Trampoline)
        {
            g_trampolineQueried = true;
            return &g_trampoline;
        }

        return nullptr;
    }

    LRESULT CALLBACK WindowProc(
        HWND window,
        UINT message,
        WPARAM wparam,
        LPARAM lparam)
    {
        return DefWindowProcW(
            window,
            message,
            wparam,
            lparam);
    }

    std::string ReadAll(
        const fs::path& path)
    {
        std::ifstream input(
            path,
            std::ios::binary);

        return
            std::string(
                std::istreambuf_iterator<char>(
                    input),
                std::istreambuf_iterator<char>());
    }
}

int wmain(
    int argc,
    wchar_t** argv)
{
    if (argc != 3)
    {
        return 2;
    }

    const fs::path dllPath = argv[1];
    const fs::path outputRoot = argv[2];

    std::error_code error;
    fs::remove_all(
        outputRoot,
        error);

    if (!SetEnvironmentVariableW(
            L"GZFX_TEST_OUTPUT_ROOT",
            outputRoot.c_str()) ||
        !SetEnvironmentVariableW(
            L"GZFX_TEST_TELEMETRY_ACTIVE",
            L"1") ||
        !SetEnvironmentVariableW(
            L"GZFX_TEST_ALLOW_SOFTWARE_PRIMARY",
            L"1"))
    {
        return 3;
    }

    const HMODULE module =
        LoadLibraryW(
            dllPath.c_str());

    if (module == nullptr)
    {
        return 4;
    }

    const auto* version =
        reinterpret_cast<
            const gzfx::skse::
                PluginVersionData*>(
            GetProcAddress(
                module,
                "SKSEPlugin_Version"));

    const auto preload =
        reinterpret_cast<PreloadFn>(
            GetProcAddress(
                module,
                "SKSEPlugin_Preload"));

    const auto load =
        reinterpret_cast<LoadFn>(
            GetProcAddress(
                module,
                "SKSEPlugin_Load"));

    const auto shutdown =
        reinterpret_cast<ShutdownFn>(
            GetProcAddress(
                module,
                "GZFX_Shutdown"));

    const auto initializationState =
        reinterpret_cast<InitializationStateFn>(
            GetProcAddress(
                module,
                "GZFX_GetInitializationState"));

    if (version == nullptr ||
        preload == nullptr ||
        load == nullptr ||
        shutdown == nullptr ||
        initializationState == nullptr ||
        version->dataVersion !=
            gzfx::skse::
                PluginVersionData::kVersion ||
        version->pluginVersion != 36 ||
        std::string(version->name) != "GZFX" ||
        version->compatibleVersions[0] !=
            gzfx::skse::kRuntime_1_7_104 ||
        version->compatibleVersions[1] != 0 ||
        version->seVersionRequired !=
            gzfx::skse::kSkse_2_3_1)
    {
        return 5;
    }

    gzfx::skse::Interface skse{};
    skse.skseVersion =
        gzfx::skse::kSkse_2_3_1;
    skse.runtimeVersion =
        gzfx::skse::kRuntime_1_7_104;
    skse.editorVersion = 0;
    skse.isEditor = 0;
    skse.QueryInterface =
        &QueryInterface;
    skse.GetPluginHandle =
        &GetPluginHandle;
    skse.GetReleaseIndex =
        &GetReleaseIndex;
    skse.GetPluginInfo =
        &GetPluginInfo;

    if (!preload(&skse))
    {
        return 6;
    }

    // Preload must remain inert in 0.0.33: no runtime initialization and no
    // filesystem-heavy D3D lifecycle probe before Skyrim's global initializers.
    if (initializationState() != 0)
    {
        return 61;
    }

    const fs::path preloadDiagnostics =
        outputRoot /
        L"GZFX" /
        L"Diagnostics" /
        L"d3d11_lifecycle.txt";

    if (fs::exists(
            preloadDiagnostics,
            error))
    {
        return 62;
    }

    if (!load(&skse) ||
        !g_messagingQueried ||
        !g_trampolineQueried ||
        g_listener == nullptr ||
        initializationState() != 2)
    {
        return 63;
    }

    gzfx::skse::MessagingInterface::Message
        postLoad{
            "SKSE",
            gzfx::skse::MessagingInterface::
                PostLoad,
            0,
            nullptr
        };

    gzfx::skse::MessagingInterface::Message
        inputLoaded{
            "SKSE",
            gzfx::skse::MessagingInterface::
                InputLoaded,
            0,
            nullptr
        };

    gzfx::skse::MessagingInterface::Message
        dataLoaded{
            "SKSE",
            gzfx::skse::MessagingInterface::
                DataLoaded,
            0,
            nullptr
        };

    g_listener(&postLoad);
    g_listener(&inputLoaded);
    g_listener(&dataLoaded);

    const wchar_t* className =
        L"GZFX.SKSE.FrontDoor";

    WNDCLASSEXW windowClass{};
    windowClass.cbSize =
        sizeof(windowClass);
    windowClass.lpfnWndProc =
        WindowProc;
    windowClass.hInstance =
        GetModuleHandleW(nullptr);
    windowClass.lpszClassName =
        className;

    RegisterClassExW(
        &windowClass);

    const HWND window =
        CreateWindowExW(
            0,
            className,
            L"GZFX SKSE Front Door",
            WS_OVERLAPPEDWINDOW,
            0,
            0,
            640,
            360,
            nullptr,
            nullptr,
            windowClass.hInstance,
            nullptr);

    if (window == nullptr)
    {
        return 7;
    }

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = 640;
    desc.BufferDesc.Height = 360;
    desc.BufferDesc.Format =
        DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage =
        DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.OutputWindow = window;
    desc.Windowed = TRUE;
    desc.SwapEffect =
        DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* swap = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL level{};

    const HRESULT result =
        D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            0,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &desc,
            &swap,
            &device,
            &level,
            &context);

    if (FAILED(result) ||
        swap == nullptr ||
        device == nullptr ||
        context == nullptr)
    {
        DestroyWindow(window);
        return 8;
    }

    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = 32;
    textureDesc.Height = 32;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format =
        DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage =
        D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags =
        D3D11_BIND_RENDER_TARGET;

    ID3D11Texture2D* texture = nullptr;

    if (FAILED(
            device->CreateTexture2D(
                &textureDesc,
                nullptr,
                &texture)) ||
        texture == nullptr)
    {
        context->Release();
        device->Release();
        swap->Release();
        DestroyWindow(window);
        return 9;
    }

    swap->Present(
        0,
        0);

    texture->Release();
    context->Release();
    device->Release();
    swap->Release();
    DestroyWindow(window);

    shutdown();

    const fs::path diagnostics =
        outputRoot /
        L"GZFX" /
        L"Diagnostics";

    const std::string bridge =
        ReadAll(
            diagnostics /
            L"skse_bridge.txt");

    const std::string lifecycle =
        ReadAll(
            diagnostics /
            L"d3d11_lifecycle.txt");

    const std::string startup =
        ReadAll(
            diagnostics /
            L"renderer_startup.csv");

    const std::string directInput =
        ReadAll(
            diagnostics /
            L"directinput_mouse.txt");

    const bool bridgeOk =
        bridge.find(
            "preload_entered=true") !=
                std::string::npos &&
        bridge.find(
            "graphics_arm_phase=load") !=
                std::string::npos &&
        bridge.find(
            "early_graphics_armed=true") !=
                std::string::npos &&
        bridge.find(
            "messaging_registered=true") !=
                std::string::npos &&
        bridge.find(
            "trampoline_available=true") !=
                std::string::npos &&
        bridge.find(
            "runtime_initialized=true") !=
                std::string::npos &&
        bridge.find(
            "directinput_mouse_armed=true") !=
                std::string::npos &&
        bridge.find(
            "commonlib_module_ready=true") !=
                std::string::npos &&
        bridge.find(
            "commonlib_skse_api_ready=true") !=
                std::string::npos &&
        bridge.find(
            "message=post_load") !=
                std::string::npos &&
        bridge.find(
            "message=input_loaded") !=
                std::string::npos &&
        bridge.find(
            "message=data_loaded") !=
                std::string::npos;

    const bool graphicsOk =
        lifecycle.find(
            "event=D3D11CreateDeviceAndSwapChain") !=
                std::string::npos &&
        lifecycle.find(
            "renderer_primary_attach=armed") !=
                std::string::npos &&
        startup.find(
            "primary_attach_complete") !=
                std::string::npos &&
        startup.find(
            "skse_plugin_load") !=
                std::string::npos;

    const bool directInputOk =
        directInput.find(
            "status=armed") !=
                std::string::npos &&
        directInput.find(
            "directinput8_create_hooked=true") !=
                std::string::npos;

    if (!bridgeOk ||
        !graphicsOk ||
        !directInputOk)
    {
        std::cerr
            << "SKSE bridge, graphics, or DirectInput front-door proof failed.\n"
            << "--- bridge ---\n"
            << bridge
            << "\n--- lifecycle ---\n"
            << lifecycle
            << "\n--- startup ---\n"
            << startup
            << "\n--- directinput ---\n"
            << directInput
            << "\n";

        return 10;
    }

    std::wcout
        << L"SKSE single-DLL + DirectInput front door passed.\n";

    // Do not FreeLibrary. Native hooks are intentionally process-lifetime.
    return 0;
}
