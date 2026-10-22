#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using InitializeFn = bool (*)();

namespace
{
    LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
    {
        return DefWindowProcW(hwnd, msg, w, l);
    }

    std::string ReadAll(const std::filesystem::path& path)
    {
        std::ifstream in(path);
        std::ostringstream out;
        out << in.rdbuf();
        return out.str();
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3)
    {
        return 2;
    }

    const std::filesystem::path dllPath(argv[1]);
    const std::filesystem::path outputRoot(argv[2]);

    std::error_code ec;
    std::filesystem::remove_all(outputRoot, ec);
    ec.clear();
    std::filesystem::create_directories(outputRoot, ec);
    if (ec)
    {
        return 3;
    }

    if (!SetEnvironmentVariableW(L"GZFX_TEST_OUTPUT_ROOT", outputRoot.c_str()))
    {
        return 4;
    }

    const HMODULE gzfx = LoadLibraryW(dllPath.c_str());
    if (!gzfx)
    {
        return 5;
    }

    const auto initialize = reinterpret_cast<InitializeFn>(
        GetProcAddress(gzfx, "GZFX_Initialize"));

    if (!initialize || !initialize())
    {
        return 6;
    }

    const wchar_t* className = L"GZFX.RendererForensicsHarness";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = className;

    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return 7;
    }

    const HWND hwnd = CreateWindowExW(
        0,
        className,
        L"GZFX renderer forensics harness",
        WS_OVERLAPPEDWINDOW,
        0,
        0,
        640,
        360,
        nullptr,
        nullptr,
        wc.hInstance,
        nullptr);

    if (!hwnd)
    {
        return 8;
    }

    ShowWindow(hwnd, SW_SHOW);

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferDesc.Width = 640;
    sd.BufferDesc.Height = 360;
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

    const HRESULT created = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_WARP,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &sd,
        &swap,
        &device,
        &level,
        &context);

    if (FAILED(created))
    {
        return 9;
    }

    ID3D11Texture2D* back = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    if (FAILED(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) ||
        FAILED(device->CreateRenderTargetView(back, nullptr, &rtv)))
    {
        return 10;
    }

    D3D11_TEXTURE2D_DESC depthDesc{};
    depthDesc.Width = 640;
    depthDesc.Height = 360;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ID3D11Texture2D* depth = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    if (FAILED(device->CreateTexture2D(&depthDesc, nullptr, &depth)) ||
        FAILED(device->CreateDepthStencilView(depth, nullptr, &dsv)))
    {
        return 11;
    }

    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.ByteWidth = 256;
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    ID3D11Buffer* cb = nullptr;
    if (FAILED(device->CreateBuffer(&cbDesc, nullptr, &cb)))
    {
        return 12;
    }

    // Acquire the real swap chain and immediate context.
    swap->Present(0, 0);

    D3D11_VIEWPORT viewport{};
    viewport.Width = 640.0f;
    viewport.Height = 360.0f;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    context->RSSetViewports(1, &viewport);
    context->OMSetRenderTargets(1, &rtv, dsv);

    const FLOAT clear[4] = { 0.05f, 0.10f, 0.15f, 1.0f };
    context->ClearRenderTargetView(rtv, clear);
    context->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    context->VSSetConstantBuffers(0, 1, &cb);
    context->PSSetConstantBuffers(1, 1, &cb);
    context->Draw(3, 0);

    // Finalize the instrumented frame, then give summaries one more Present.
    swap->Present(0, 0);
    swap->Present(0, 0);

    const std::filesystem::path diag = outputRoot / L"GZFX" / L"Diagnostics";
    const auto acquisitionPath = diag / L"renderer_acquisition.txt";
    const auto summaryPath = diag / L"renderer_summary.txt";
    const auto framesPath = diag / L"frame_summary.csv";
    const auto modulesPath = diag / L"loaded_modules.txt";

    if (!std::filesystem::exists(acquisitionPath) ||
        !std::filesystem::exists(summaryPath) ||
        !std::filesystem::exists(framesPath) ||
        !std::filesystem::exists(modulesPath))
    {
        return 13;
    }

    const std::string acquisition = ReadAll(acquisitionPath);
    const std::string summary = ReadAll(summaryPath);
    const std::string frames = ReadAll(framesPath);

    if (acquisition.find("status=armed") == std::string::npos ||
        summary.find("swap_chain=0x0") != std::string::npos ||
        summary.find("640x360") == std::string::npos ||
        summary.find("bytes=256") == std::string::npos ||
        summary.find("dsv_binds=") == std::string::npos ||
        frames.find("2,1,") == std::string::npos)
    {
        return 14;
    }

    if (cb) cb->Release();
    if (dsv) dsv->Release();
    if (depth) depth->Release();
    if (rtv) rtv->Release();
    if (back) back->Release();
    if (context) context->Release();
    if (device) device->Release();
    if (swap) swap->Release();

    DestroyWindow(hwnd);
    FreeLibrary(gzfx);

    std::wcout << L"Renderer forensics harness passed.\n";
    return 0;
}
