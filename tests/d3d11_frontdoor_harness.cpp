#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
    namespace fs = std::filesystem;

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
                std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>());
    }
}

int wmain(
    int argc,
    wchar_t** argv)
{
    if (argc != 2)
    {
        return 2;
    }

    const fs::path outputRoot =
        argv[1];

    std::error_code error;
    fs::remove_all(
        outputRoot,
        error);

    SetEnvironmentVariableW(
        L"GZFX_TEST_OUTPUT_ROOT",
        outputRoot.c_str());

    const wchar_t* className =
        L"GZFX.FrontDoor.Harness";

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
            L"GZFX Front Door",
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
        return 3;
    }

    DXGI_SWAP_CHAIN_DESC swapDesc{};
    swapDesc.BufferDesc.Width = 640;
    swapDesc.BufferDesc.Height = 360;
    swapDesc.BufferDesc.Format =
        DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.BufferUsage =
        DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = 2;
    swapDesc.OutputWindow = window;
    swapDesc.Windowed = TRUE;
    swapDesc.SwapEffect =
        DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* swapChain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL featureLevel{};

    const D3D_FEATURE_LEVEL requested =
        D3D_FEATURE_LEVEL_11_0;

    const HRESULT createResult =
        D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            0,
            &requested,
            1,
            D3D11_SDK_VERSION,
            &swapDesc,
            &swapChain,
            &device,
            &featureLevel,
            &context);

    if (FAILED(createResult) ||
        swapChain == nullptr ||
        device == nullptr ||
        context == nullptr)
    {
        DestroyWindow(window);
        return 4;
    }

    const HMODULE gzfx =
        GetModuleHandleW(
            L"GZFX.dll");

    if (gzfx == nullptr)
    {
        context->Release();
        device->Release();
        swapChain->Release();
        DestroyWindow(window);
        return 5;
    }

    using VersionFn =
        const char*(*)() noexcept;

    using ShutdownFn =
        void(*)() noexcept;

    const auto version =
        reinterpret_cast<VersionFn>(
            GetProcAddress(
                gzfx,
                "GZFX_GetVersion"));

    const auto shutdown =
        reinterpret_cast<ShutdownFn>(
            GetProcAddress(
                gzfx,
                "GZFX_Shutdown"));

    if (version == nullptr ||
        shutdown == nullptr ||
        std::string(version()).find(
            "0.0.27") ==
            std::string::npos)
    {
        context->Release();
        device->Release();
        swapChain->Release();
        DestroyWindow(window);
        return 6;
    }

    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = 64;
    textureDesc.Height = 64;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format =
        DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage =
        D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags =
        D3D11_BIND_RENDER_TARGET |
        D3D11_BIND_SHADER_RESOURCE;

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
        swapChain->Release();
        DestroyWindow(window);
        return 7;
    }

    swapChain->Present(
        0,
        0);

    texture->Release();

    shutdown();

    const fs::path diagnostics =
        outputRoot /
        L"GZFX" /
        L"Diagnostics";

    const std::string startup =
        ReadAll(
            diagnostics /
            L"renderer_startup.csv");

    const auto processAttach =
        startup.find(
            "d3d11_proxy_process_attach");

    const auto firstExport =
        startup.find(
            "first_d3d11_export_enter");

    const auto deviceCreated =
        startup.find(
            "real_device_creation_complete");

    const auto loadBegin =
        startup.find(
            "gzfx_load_begin");

    const auto initialized =
        startup.find(
            "gzfx_initialize_complete");

    const auto attached =
        startup.find(
            "primary_attach_complete");

    const auto firstResource =
        startup.find(
            "first_resource_creation_observed");

    const auto firstPresent =
        startup.find(
            "first_present");

    const bool startupComplete =
        processAttach != std::string::npos &&
        firstExport != std::string::npos &&
        deviceCreated != std::string::npos &&
        loadBegin != std::string::npos &&
        initialized != std::string::npos &&
        attached != std::string::npos &&
        firstResource != std::string::npos &&
        firstPresent != std::string::npos;

    const fs::path telemetryRoot =
        outputRoot /
        L"GZFX" /
        L"Telemetry";

    bool closeComplete = false;
    bool sessionComplete = false;

    if (fs::exists(
            telemetryRoot))
    {
        for (const auto& entry :
             fs::directory_iterator(
                 telemetryRoot))
        {
            if (!entry.is_directory())
            {
                continue;
            }

            const std::string closeState =
                ReadAll(
                    entry.path() /
                    L"session_close_state.txt");

            const std::string complete =
                ReadAll(
                    entry.path() /
                    L"telemetry_session_complete.txt");

            closeComplete =
                closeComplete ||
                closeState.find(
                    "state=complete") !=
                    std::string::npos;

            sessionComplete =
                sessionComplete ||
                complete.find(
                    "status=complete") !=
                    std::string::npos;
        }
    }

    context->Release();
    device->Release();
    swapChain->Release();
    DestroyWindow(window);

    if (!startupComplete)
    {
        std::cerr
            << "Graphics-front-door startup evidence incomplete.\n"
            << startup
            << "\n";

        return 8;
    }

    if (!closeComplete ||
        !sessionComplete)
    {
        std::cerr
            << "Telemetry shutdown proof incomplete.\n";

        return 9;
    }

    std::wcout
        << L"D3D11 graphics front-door bootstrap passed.\n";

    return 0;
}
