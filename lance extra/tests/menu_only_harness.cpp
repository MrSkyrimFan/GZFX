#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using InitializeFn = bool (*)();

namespace
{
    LRESULT CALLBACK WindowProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam)
    {
        return DefWindowProcW(
            hwnd,
            message,
            wParam,
            lParam);
    }

    std::string ReadAll(
        const std::filesystem::path& path)
    {
        std::ifstream in(path);

        return std::string(
            std::istreambuf_iterator<char>(in),
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

    const std::filesystem::path dllPath(argv[1]);
    const std::filesystem::path outputRoot(argv[2]);

    std::error_code error;
    std::filesystem::remove_all(
        outputRoot,
        error);

    error.clear();

    std::filesystem::create_directories(
        outputRoot,
        error);

    if (error ||
        !SetEnvironmentVariableW(
            L"GZFX_TEST_OUTPUT_ROOT",
            outputRoot.c_str()) ||
        !SetEnvironmentVariableW(
            L"GZFX_TEST_MENU_VISIBLE",
            L"1"))
    {
        return 3;
    }

    const HMODULE gzfx =
        LoadLibraryW(
            dllPath.c_str());

    if (gzfx == nullptr)
    {
        return 4;
    }

    const auto initialize =
        reinterpret_cast<InitializeFn>(
            GetProcAddress(
                gzfx,
                "GZFX_Initialize"));

    if (initialize == nullptr ||
        !initialize())
    {
        return 5;
    }

    const wchar_t* className =
        L"GZFX.MenuOnlyHarness";

    WNDCLASSEXW windowClass{};
    windowClass.cbSize =
        sizeof(windowClass);

    windowClass.lpfnWndProc =
        WindowProc;

    windowClass.hInstance =
        GetModuleHandleW(nullptr);

    windowClass.lpszClassName =
        className;

    if (!RegisterClassExW(
            &windowClass) &&
        GetLastError() !=
            ERROR_CLASS_ALREADY_EXISTS)
    {
        return 6;
    }

    const HWND window =
        CreateWindowExW(
            0,
            className,
            L"GZFX menu-only harness",
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

    ShowWindow(
        window,
        SW_SHOW);

    DXGI_SWAP_CHAIN_DESC swapDescription{};
    swapDescription.BufferDesc.Width = 640;
    swapDescription.BufferDesc.Height = 360;
    swapDescription.BufferDesc.Format =
        DXGI_FORMAT_R8G8B8A8_UNORM;

    swapDescription.SampleDesc.Count = 1;
    swapDescription.BufferUsage =
        DXGI_USAGE_RENDER_TARGET_OUTPUT;

    swapDescription.BufferCount = 2;
    swapDescription.OutputWindow = window;
    swapDescription.Windowed = TRUE;
    swapDescription.SwapEffect =
        DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* swapChain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL featureLevel{};

    if (FAILED(
            D3D11CreateDeviceAndSwapChain(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                0,
                nullptr,
                0,
                D3D11_SDK_VERSION,
                &swapDescription,
                &swapChain,
                &device,
                &featureLevel,
                &context)))
    {
        return 8;
    }

    ID3D11Texture2D* backbuffer = nullptr;
    ID3D11RenderTargetView* backbufferRtv = nullptr;

    if (FAILED(
            swapChain->GetBuffer(
                0,
                __uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(
                    &backbuffer))) ||
        FAILED(
            device->CreateRenderTargetView(
                backbuffer,
                nullptr,
                &backbufferRtv)))
    {
        return 9;
    }

    const FLOAT clearColor[4] =
    {
        0.08f,
        0.09f,
        0.10f,
        1.0f
    };

    // Deliberately issue no world DrawIndexed work.
    // After the normal 120-frame activation delay, GZFX must
    // still render the menu through its menu-only path.
    for (int frame = 0;
         frame < 140;
         ++frame)
    {
        context->OMSetRenderTargets(
            1,
            &backbufferRtv,
            nullptr);

        context->ClearRenderTargetView(
            backbufferRtv,
            clearColor);

        swapChain->Present(
            0,
            0);
    }

    const std::filesystem::path visiblePath =
        outputRoot /
        L"GZFX" /
        L"Diagnostics" /
        L"visible_pass.txt";

    const std::string visible =
        ReadAll(
            visiblePath);

    if (visible.find(
            "status=menu_only") ==
            std::string::npos ||
        visible.find(
            "world_active=false") ==
            std::string::npos)
    {
        std::cerr
            << "menu-only renderer did not stay alive outside world rendering\n"
            << visible
            << "\n";

        return 10;
    }

    if (backbufferRtv) backbufferRtv->Release();
    if (backbuffer) backbuffer->Release();
    if (context) context->Release();
    if (device) device->Release();
    if (swapChain) swapChain->Release();

    DestroyWindow(window);
    FreeLibrary(gzfx);

    std::wcout
        << L"Menu-only renderer harness passed.\n";

    return 0;
}
