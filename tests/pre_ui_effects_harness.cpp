#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <array>
#include <cstdint>
#include <cstring>
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

    bool ReadPixelBytes(
        ID3D11DeviceContext* context,
        ID3D11Texture2D* staging,
        const UINT x,
        const UINT y,
        std::array<std::uint8_t, 8>& pixel)
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};

        if (FAILED(
                context->Map(
                    staging,
                    0,
                    D3D11_MAP_READ,
                    0,
                    &mapped)))
        {
            return false;
        }

        const auto* row =
            static_cast<const std::uint8_t*>(
                mapped.pData) +
            static_cast<std::size_t>(y) *
                mapped.RowPitch;

        std::memcpy(
            pixel.data(),
            row +
                static_cast<std::size_t>(x) *
                    pixel.size(),
            pixel.size());

        context->Unmap(
            staging,
            0);

        return true;
    }

    void SetIdentity(float* matrix)
    {
        for (int i = 0; i < 16; ++i)
        {
            matrix[i] = 0.0f;
        }

        matrix[0] = 1.0f;
        matrix[5] = 1.0f;
        matrix[10] = 1.0f;
        matrix[15] = 1.0f;
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
            L"GZFX_TEST_FAST_CAPTURE",
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
        L"GZFX.PreUiHarness";

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
            L"GZFX pre-UI harness",
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

    D3D11_TEXTURE2D_DESC colorDesc{};
    colorDesc.Width = 640;
    colorDesc.Height = 360;
    colorDesc.MipLevels = 1;
    colorDesc.ArraySize = 1;
    colorDesc.Format =
        DXGI_FORMAT_R16G16B16A16_FLOAT;

    colorDesc.SampleDesc.Count = 1;
    colorDesc.Usage =
        D3D11_USAGE_DEFAULT;

    colorDesc.BindFlags =
        D3D11_BIND_RENDER_TARGET |
        D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* sceneColor = nullptr;
    ID3D11RenderTargetView* sceneRtv = nullptr;

    if (FAILED(
            device->CreateTexture2D(
                &colorDesc,
                nullptr,
                &sceneColor)) ||
        FAILED(
            device->CreateRenderTargetView(
                sceneColor,
                nullptr,
                &sceneRtv)))
    {
        return 10;
    }

    D3D11_TEXTURE2D_DESC stagingDesc =
        colorDesc;

    stagingDesc.Usage =
        D3D11_USAGE_STAGING;

    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags =
        D3D11_CPU_ACCESS_READ;

    ID3D11Texture2D* stagingBefore = nullptr;
    ID3D11Texture2D* stagingAfter = nullptr;

    if (FAILED(
            device->CreateTexture2D(
                &stagingDesc,
                nullptr,
                &stagingBefore)) ||
        FAILED(
            device->CreateTexture2D(
                &stagingDesc,
                nullptr,
                &stagingAfter)))
    {
        return 11;
    }

    D3D11_TEXTURE2D_DESC depthDesc{};
    depthDesc.Width = 640;
    depthDesc.Height = 360;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format =
        DXGI_FORMAT_R24G8_TYPELESS;

    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage =
        D3D11_USAGE_DEFAULT;

    depthDesc.BindFlags =
        D3D11_BIND_DEPTH_STENCIL |
        D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* depth = nullptr;

    if (FAILED(
            device->CreateTexture2D(
                &depthDesc,
                nullptr,
                &depth)))
    {
        return 12;
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC
        depthViewDesc{};

    depthViewDesc.Format =
        DXGI_FORMAT_D24_UNORM_S8_UINT;

    depthViewDesc.ViewDimension =
        D3D11_DSV_DIMENSION_TEXTURE2D;

    ID3D11DepthStencilView* depthView = nullptr;

    if (FAILED(
            device->CreateDepthStencilView(
                depth,
                &depthViewDesc,
                &depthView)))
    {
        return 13;
    }

    std::array<float, 180> camera{};

    for (int matrix = 0;
         matrix < 10;
         ++matrix)
    {
        SetIdentity(
            camera.data() +
            matrix * 16);
    }

    camera[168] = 1.0f;
    camera[169] = 1.0f;
    camera[170] = 1.0f;
    camera[171] = 1.0f;

    for (std::size_t i = 172;
         i < camera.size();
         ++i)
    {
        camera[i] = 1.0f;
    }

    D3D11_BUFFER_DESC cameraDesc{};
    cameraDesc.ByteWidth =
        static_cast<UINT>(
            camera.size() *
            sizeof(float));

    cameraDesc.Usage =
        D3D11_USAGE_DEFAULT;

    cameraDesc.BindFlags =
        D3D11_BIND_CONSTANT_BUFFER;

    D3D11_SUBRESOURCE_DATA cameraData{};
    cameraData.pSysMem =
        camera.data();

    ID3D11Buffer* cameraBuffer = nullptr;

    if (FAILED(
            device->CreateBuffer(
                &cameraDesc,
                &cameraData,
                &cameraBuffer)))
    {
        return 14;
    }

    // Acquire swap chain/context, dimensions, and arm gameplay stage trace.
    swapChain->Present(0, 0);

    const FLOAT brightHdr[4] =
    {
        4.0f,
        2.0f,
        0.5f,
        1.0f
    };

    context->ClearRenderTargetView(
        sceneRtv,
        brightHdr);

    context->ClearDepthStencilView(
        depthView,
        D3D11_CLEAR_DEPTH |
            D3D11_CLEAR_STENCIL,
        0.45f,
        0);

    context->VSSetConstantBuffers(
        12,
        1,
        &cameraBuffer);

    context->PSSetConstantBuffers(
        12,
        1,
        &cameraBuffer);

    // Build enough paired HDR+depth render-target evidence to admit
    // the terminal-stage classifier.
    for (int i = 0;
         i < 10;
         ++i)
    {
        context->OMSetRenderTargets(
            1,
            &sceneRtv,
            depthView);

        context->DrawIndexed(
            3,
            0,
            0);
    }

    // The real trace showed the terminal color-only phase 25-27 draws
    // after the last paired scene bind. Reproduce that relationship.
    for (int i = 0;
         i < 25;
         ++i)
    {
        context->DrawIndexed(
            3,
            0,
            0);
    }

    context->OMSetRenderTargets(
        1,
        &sceneRtv,
        nullptr);

    context->DrawIndexed(
        3,
        0,
        0);

    context->CopyResource(
        stagingBefore,
        sceneColor);

    // Leaving terminal color-only state should trigger the pre-UI stack.
    context->OMSetRenderTargets(
        1,
        &backbufferRtv,
        nullptr);

    context->CopyResource(
        stagingAfter,
        sceneColor);

    std::array<std::uint8_t, 8>
        before{};

    std::array<std::uint8_t, 8>
        after{};

    if (!ReadPixelBytes(
            context,
            stagingBefore,
            500,
            180,
            before) ||
        !ReadPixelBytes(
            context,
            stagingAfter,
            500,
            180,
            after))
    {
        return 15;
    }

    const std::filesystem::path diagnostics =
        outputRoot /
        L"GZFX" /
        L"Diagnostics";

    const std::string effectLog =
        ReadAll(
            diagnostics /
            L"pre_ui_effects.txt");

    const std::string stageTrace =
        ReadAll(
            diagnostics /
            L"stage_trace.csv");

    const std::string ambientDepthLog =
        ReadAll(
            diagnostics /
            L"ambient_depth_stack.txt");

    const std::string productControlLog =
        ReadAll(
            diagnostics /
            L"product_control_submission.txt");

    const bool injected =
        effectLog.find(
            "status=pre_ui_injected") !=
            std::string::npos;

    const bool signalRecorded =
        stageTrace.find(
            ",1\n") !=
            std::string::npos ||
        stageTrace.find(
            ",1\r\n") !=
            std::string::npos;

    const bool sceneChanged =
        before != after;

    const bool ambientDepthApplied =
        ambientDepthLog.find(
            "status=applied") !=
            std::string::npos;

    const bool wave4OwnsAo =
        productControlLog.find(
            "wave4_owns_ao=true") !=
            std::string::npos;

    const bool indirectEnabled =
        ambientDepthLog.find(
            "indirect_enabled=true") !=
            std::string::npos;

    const bool ownershipConsistent =
        wave4OwnsAo
            ? (
                ambientDepthLog.find(
                    "ao_allowed=false") !=
                        std::string::npos &&
                ambientDepthLog.find(
                    "ao_enabled=false") !=
                        std::string::npos
              )
            : (
                ambientDepthLog.find(
                    "ao_allowed=true") !=
                        std::string::npos
              );

    const bool productControlProof =
        productControlLog.find(
            "indirect_stack_applied=true") !=
                std::string::npos &&
        productControlLog.find(
            "scene_effects_injected=true") !=
                std::string::npos;

    if (!injected ||
        !signalRecorded ||
        !sceneChanged ||
        !ambientDepthApplied ||
        !indirectEnabled ||
        !ownershipConsistent ||
        !productControlProof)
    {
        std::cerr
            << "pre-UI stack failed acceptance\n"
            << "injected=" << injected
            << " signal=" << signalRecorded
            << " scene_changed=" << sceneChanged
            << " ambient_depth=" << ambientDepthApplied
            << " wave4_owns_ao=" << wave4OwnsAo
            << " indirect_enabled=" << indirectEnabled
            << " ownership_consistent=" << ownershipConsistent
            << " product_control=" << productControlProof
            << "\n"
            << ambientDepthLog
            << "\n"
            << productControlLog
            << "\n"
            << effectLog
            << "\n"
            << stageTrace
            << "\n";

        return 16;
    }

    if (cameraBuffer) cameraBuffer->Release();
    if (depthView) depthView->Release();
    if (depth) depth->Release();
    if (stagingAfter) stagingAfter->Release();
    if (stagingBefore) stagingBefore->Release();
    if (sceneRtv) sceneRtv->Release();
    if (sceneColor) sceneColor->Release();
    if (backbufferRtv) backbufferRtv->Release();
    if (backbuffer) backbuffer->Release();
    if (context) context->Release();
    if (device) device->Release();
    if (swapChain) swapChain->Release();

    DestroyWindow(window);
    FreeLibrary(gzfx);

    std::wcout
        << L"Pre-UI scene stack harness passed.\n";

    return 0;
}
