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
    LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
    {
        return DefWindowProcW(hwnd, msg, w, l);
    }

    bool ExistsNonEmpty(const std::filesystem::path& path)
    {
        std::error_code error;
        return std::filesystem::exists(path, error) &&
               !error &&
               std::filesystem::file_size(path, error) > 0 &&
               !error;
    }

    std::string ReadAll(const std::filesystem::path& path)
    {
        std::ifstream in(path);
        return std::string(
            std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>());
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

    std::error_code error;
    std::filesystem::remove_all(outputRoot, error);
    error.clear();
    std::filesystem::create_directories(outputRoot, error);

    if (error ||
        !SetEnvironmentVariableW(L"GZFX_TEST_OUTPUT_ROOT", outputRoot.c_str()) ||
        !SetEnvironmentVariableW(L"GZFX_TEST_FAST_CAPTURE", L"1"))
    {
        return 3;
    }

    const HMODULE gzfx = LoadLibraryW(dllPath.c_str());
    if (gzfx == nullptr)
    {
        return 4;
    }

    const auto initialize = reinterpret_cast<InitializeFn>(
        GetProcAddress(gzfx, "GZFX_Initialize"));

    if (initialize == nullptr || !initialize())
    {
        return 5;
    }

    const wchar_t* className = L"GZFX.ResourceIntelligenceHarness";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = className;

    if (!RegisterClassExW(&wc) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return 6;
    }

    const HWND hwnd = CreateWindowExW(
        0,
        className,
        L"GZFX resource intelligence",
        WS_OVERLAPPEDWINDOW,
        0,
        0,
        640,
        360,
        nullptr,
        nullptr,
        wc.hInstance,
        nullptr);

    if (hwnd == nullptr)
    {
        return 7;
    }

    ShowWindow(hwnd, SW_SHOW);

    DXGI_SWAP_CHAIN_DESC swapDesc{};
    swapDesc.BufferDesc.Width = 640;
    swapDesc.BufferDesc.Height = 360;
    swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = 2;
    swapDesc.OutputWindow = hwnd;
    swapDesc.Windowed = TRUE;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* swap = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL featureLevel{};

    if (FAILED(D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            0,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &swapDesc,
            &swap,
            &device,
            &featureLevel,
            &context)))
    {
        return 8;
    }

    D3D11_TEXTURE2D_DESC colorDesc{};
    colorDesc.Width = 640;
    colorDesc.Height = 360;
    colorDesc.MipLevels = 1;
    colorDesc.ArraySize = 1;
    colorDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    colorDesc.SampleDesc.Count = 1;
    colorDesc.Usage = D3D11_USAGE_DEFAULT;
    colorDesc.BindFlags =
        D3D11_BIND_RENDER_TARGET |
        D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* color = nullptr;
    ID3D11RenderTargetView* colorRtv = nullptr;

    if (FAILED(device->CreateTexture2D(
            &colorDesc,
            nullptr,
            &color)) ||
        FAILED(device->CreateRenderTargetView(
            color,
            nullptr,
            &colorRtv)))
    {
        return 9;
    }

    D3D11_TEXTURE2D_DESC depthDesc{};
    depthDesc.Width = 640;
    depthDesc.Height = 360;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags =
        D3D11_BIND_DEPTH_STENCIL |
        D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* depth = nullptr;
    if (FAILED(device->CreateTexture2D(
            &depthDesc,
            nullptr,
            &depth)))
    {
        return 10;
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;

    ID3D11DepthStencilView* depthDsv = nullptr;
    if (FAILED(device->CreateDepthStencilView(
            depth,
            &dsvDesc,
            &depthDsv)))
    {
        return 11;
    }

    float constants[64]{};
    for (std::size_t i = 0; i < std::size(constants); ++i)
    {
        constants[i] = static_cast<float>(i + 1) * 0.25f;
    }

    D3D11_BUFFER_DESC bufferDesc{};
    bufferDesc.ByteWidth = sizeof(constants);
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = constants;

    ID3D11Buffer* constantBuffer = nullptr;
    if (FAILED(device->CreateBuffer(
            &bufferDesc,
            &initial,
            &constantBuffer)))
    {
        return 12;
    }

    // First Present lets GZFX acquire the real swap chain/context.
    swap->Present(0, 0);

    D3D11_VIEWPORT viewport{};
    viewport.Width = 640.0f;
    viewport.Height = 360.0f;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    const FLOAT clearColor[4] = {
        4.0f,
        1.0f,
        0.25f,
        1.0f
    };

    for (int frame = 0; frame < 8; ++frame)
    {
        context->RSSetViewports(1, &viewport);
        context->OMSetRenderTargets(
            1,
            &colorRtv,
            depthDsv);

        context->ClearRenderTargetView(
            colorRtv,
            clearColor);

        context->ClearDepthStencilView(
            depthDsv,
            D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
            0.35f,
            0);

        context->VSSetConstantBuffers(
            2,
            1,
            &constantBuffer);

        context->PSSetConstantBuffers(
            3,
            1,
            &constantBuffer);

        context->Draw(3, 0);
        swap->Present(0, 0);
    }

    const std::filesystem::path diagnostics =
        outputRoot / L"GZFX" / L"Diagnostics";

    const auto candidates =
        diagnostics / L"resource_candidates.txt";
    const auto depthRaw =
        diagnostics / L"depth_candidate_raw.bmp";
    const auto depthInverse =
        diagnostics / L"depth_candidate_inverse.bmp";
    const auto colorPreview =
        diagnostics / L"scene_color_candidate.bmp";
    const auto buffers =
        diagnostics / L"constant_buffers_frame_000004.csv";

    if (!ExistsNonEmpty(candidates) ||
        !ExistsNonEmpty(depthRaw) ||
        !ExistsNonEmpty(depthInverse) ||
        !ExistsNonEmpty(colorPreview) ||
        !ExistsNonEmpty(buffers))
    {
        return 13;
    }

    const std::string report = ReadAll(candidates);
    const std::string bufferText = ReadAll(buffers);

    if (report.find("depth_candidate=0x") == std::string::npos ||
        report.find("color_candidate=0x") == std::string::npos ||
        report.find("depth_raw_preview=ok") == std::string::npos ||
        report.find("depth_inverse_preview=ok") == std::string::npos ||
        report.find("scene_color_preview=ok") == std::string::npos ||
        bufferText.find(",256,") == std::string::npos)
    {
        return 14;
    }

    if (constantBuffer) constantBuffer->Release();
    if (depthDsv) depthDsv->Release();
    if (depth) depth->Release();
    if (colorRtv) colorRtv->Release();
    if (color) color->Release();
    if (context) context->Release();
    if (device) device->Release();
    if (swap) swap->Release();

    DestroyWindow(hwnd);
    FreeLibrary(gzfx);

    std::wcout << L"Resource intelligence harness passed.\n";
    return 0;
}
