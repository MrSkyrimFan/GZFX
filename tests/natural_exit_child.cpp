#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <filesystem>

namespace
{
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
}

int wmain(
    int argc,
    wchar_t** argv)
{
    if (argc != 3)
    {
        return 2;
    }

    const std::filesystem::path dllPath =
        argv[1];

    const std::filesystem::path outputRoot =
        argv[2];

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

    const HMODULE gzfx =
        LoadLibraryW(
            dllPath.c_str());

    if (gzfx == nullptr)
    {
        return 4;
    }

    using InitializeFn =
        bool(*)() noexcept;

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
        L"GZFX.NaturalExit.Child";

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
            L"GZFX Natural Exit",
            WS_OVERLAPPEDWINDOW,
            0,
            0,
            320,
            180,
            nullptr,
            nullptr,
            windowClass.hInstance,
            nullptr);

    if (window == nullptr)
    {
        return 6;
    }

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = 320;
    desc.BufferDesc.Height = 180;
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
        return 7;
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
        return 8;
    }

    swap->Present(
        0,
        0);

    texture->Release();
    context->Release();
    device->Release();
    swap->Release();
    DestroyWindow(window);

    // Intentionally no GZFX_Shutdown call.
    // Returning from main must be enough for the core GZFX ExitProcess
    // lifecycle hook to close telemetry before Windows process teardown.
    return 0;
}
