#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

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
        return std::string(
            std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>());
    }

    void SetIdentity(float* matrix)
    {
        for (int i = 0; i < 16; ++i) matrix[i] = 0.0f;
        matrix[0] = 1.0f;
        matrix[5] = 1.0f;
        matrix[10] = 1.0f;
        matrix[15] = 1.0f;
    }

    struct Target
    {
        ID3D11Texture2D* texture = nullptr;
        ID3D11RenderTargetView* rtv = nullptr;
    };

    Target CreateTarget(
        ID3D11Device* device,
        UINT width,
        UINT height,
        DXGI_FORMAT format,
        UINT mipLevels = 1)
    {
        Target result{};
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = mipLevels;
        desc.ArraySize = 1;
        desc.Format = format;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags =
            D3D11_BIND_RENDER_TARGET |
            D3D11_BIND_SHADER_RESOURCE;

        if (FAILED(device->CreateTexture2D(&desc, nullptr, &result.texture)))
        {
            return {};
        }

        D3D11_RENDER_TARGET_VIEW_DESC rtv{};
        rtv.Format = format;
        rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        rtv.Texture2D.MipSlice = 0;

        if (FAILED(device->CreateRenderTargetView(result.texture, &rtv, &result.rtv)))
        {
            result.texture->Release();
            return {};
        }

        return result;
    }

    void Release(Target& target)
    {
        if (target.rtv) target.rtv->Release();
        if (target.texture) target.texture->Release();
        target = {};
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3) return 2;

    const std::filesystem::path dllPath(argv[1]);
    const std::filesystem::path outputRoot(argv[2]);

    std::error_code ec;
    std::filesystem::remove_all(outputRoot, ec);
    ec.clear();
    std::filesystem::create_directories(outputRoot, ec);

    if (ec ||
        !SetEnvironmentVariableW(L"GZFX_TEST_OUTPUT_ROOT", outputRoot.c_str()))
    {
        return 3;
    }

    const HMODULE gzfx = LoadLibraryW(dllPath.c_str());
    if (!gzfx) return 4;

    const auto initialize = reinterpret_cast<InitializeFn>(
        GetProcAddress(gzfx, "GZFX_Initialize"));
    if (!initialize || !initialize()) return 5;

    const wchar_t* className = L"GZFX.Wave3PipelineHarness";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = className;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return 6;
    }

    const HWND window = CreateWindowExW(
        0, className, L"GZFX Wave3", WS_OVERLAPPEDWINDOW,
        0, 0, 320, 180, nullptr, nullptr, wc.hInstance, nullptr);
    if (!window) return 7;

    constexpr UINT width = 320;
    constexpr UINT height = 180;

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferDesc.Width = width;
    sd.BufferDesc.Height = height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.OutputWindow = window;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* swap = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL level{};

    if (FAILED(D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION,
            &sd, &swap, &device, &level, &context)))
    {
        return 8;
    }

    // Present once so renderer_forensics installs the primary-context vtable hooks.
    swap->Present(0, 0);

    Target hdr = CreateTarget(device, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    Target motion = CreateTarget(device, width, height, DXGI_FORMAT_R16G16_FLOAT);
    Target normal = CreateTarget(device, width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
    Target aux = CreateTarget(device, width, height, DXGI_FORMAT_R8G8_UNORM);

    if (!hdr.texture || !motion.texture || !normal.texture || !aux.texture)
    {
        return 9;
    }

    D3D11_TEXTURE2D_DESC depthDesc{};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags =
        D3D11_BIND_DEPTH_STENCIL |
        D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* depth = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    if (FAILED(device->CreateTexture2D(&depthDesc, nullptr, &depth))) return 10;

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    if (FAILED(device->CreateDepthStencilView(depth, &dsvDesc, &dsv))) return 11;

    std::array<float, 180> camera{};
    for (int matrix = 0; matrix < 10; ++matrix)
    {
        SetIdentity(camera.data() + matrix * 16);
    }

    // FrameParams.
    camera[168] = 1.0f;
    // DynamicResolutionParams1 current.xy + previous.zw.
    camera[172] = 1.0f;
    camera[173] = 1.0f;
    camera[174] = 1.0f;
    camera[175] = 1.0f;
    // DynamicResolutionParams2.
    camera[176] = 1.0f;
    camera[177] = 1.0f;
    camera[178] = 1.0f;
    camera[179] = 1.0f;

    D3D11_BUFFER_DESC cbDesc{};
    cbDesc.ByteWidth = static_cast<UINT>(camera.size() * sizeof(float));
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    D3D11_SUBRESOURCE_DATA cbData{};
    cbData.pSysMem = camera.data();

    ID3D11Buffer* cameraBuffer = nullptr;
    if (FAILED(device->CreateBuffer(&cbDesc, &cbData, &cameraBuffer))) return 12;

    context->VSSetConstantBuffers(12, 1, &cameraBuffer);
    context->PSSetConstantBuffers(12, 1, &cameraBuffer);

    const FLOAT hdrClear[4] = {1.0f, 0.5f, 0.25f, 1.0f};
    const FLOAT motionClear[4] = {0, 0, 0, 0};
    const FLOAT normalClear[4] = {0.5f, 0.5f, 0, 1};
    const FLOAT auxClear[4] = {0.25f, 0.75f, 0, 0};

    context->ClearRenderTargetView(hdr.rtv, hdrClear);
    context->ClearRenderTargetView(motion.rtv, motionClear);
    context->ClearRenderTargetView(normal.rtv, normalClear);
    context->ClearRenderTargetView(aux.rtv, auxClear);
    context->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 0.45f, 0);

    ID3D11RenderTargetView* mrt[4] = {hdr.rtv, motion.rtv, normal.rtv, aux.rtv};
    context->OMSetRenderTargets(4, mrt, dsv);
    context->Draw(3, 0);
    context->Draw(3, 0);

    constexpr UINT cameraMipLevels = 9;
    D3D11_TEXTURE2D_DESC cameraZDesc{};
    cameraZDesc.Width = width;
    cameraZDesc.Height = height;
    cameraZDesc.MipLevels = cameraMipLevels;
    cameraZDesc.ArraySize = 1;
    cameraZDesc.Format = DXGI_FORMAT_R32_FLOAT;
    cameraZDesc.SampleDesc.Count = 1;
    cameraZDesc.Usage = D3D11_USAGE_DEFAULT;
    cameraZDesc.BindFlags =
        D3D11_BIND_RENDER_TARGET |
        D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* cameraZ = nullptr;
    if (FAILED(device->CreateTexture2D(&cameraZDesc, nullptr, &cameraZ))) return 13;

    std::array<ID3D11RenderTargetView*, cameraMipLevels> cameraRtvs{};

    for (UINT mip = 0; mip < cameraMipLevels; ++mip)
    {
        D3D11_RENDER_TARGET_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_R32_FLOAT;
        view.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        view.Texture2D.MipSlice = mip;
        if (FAILED(device->CreateRenderTargetView(cameraZ, &view, &cameraRtvs[mip])))
        {
            return 14;
        }
    }

    // Binding CameraZ mip 0 makes the semantic authority signal
    // AfterOpaqueWorldMRT before this draw executes. Wave 3 must run there.
    for (UINT mip = 0; mip < cameraMipLevels; ++mip)
    {
        context->OMSetRenderTargets(1, &cameraRtvs[mip], nullptr);
        context->Draw(3, 0);
    }

    context->OMSetRenderTargets(0, nullptr, nullptr);
    swap->Present(0, 0);

    const auto diagnostics = outputRoot / L"GZFX" / L"Diagnostics" / L"Wave3";
    const std::string status = ReadAll(diagnostics / L"wave3_status.txt");

    const bool executed = status.find("status=executed") != std::string::npos;
    const bool hierarchy = status.find("hierarchy_valid=true") != std::string::npos;
    const bool temporal = status.find("temporal_valid=true") != std::string::npos;
    const bool convention = status.find("history_generation=") != std::string::npos;

    if (!executed || !hierarchy || !temporal || !convention)
    {
        std::cerr << "Wave 3 integration failed\n" << status << "\n";
        return 15;
    }

    for (auto*& rtv : cameraRtvs) if (rtv) rtv->Release();
    if (cameraZ) cameraZ->Release();
    if (cameraBuffer) cameraBuffer->Release();
    if (dsv) dsv->Release();
    if (depth) depth->Release();
    Release(aux);
    Release(normal);
    Release(motion);
    Release(hdr);
    if (context) context->Release();
    if (device) device->Release();
    if (swap) swap->Release();
    DestroyWindow(window);
    FreeLibrary(gzfx);

    std::wcout << L"Wave 3 integration harness passed.\n";
    return 0;
}