#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

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

    bool ExistsNonEmpty(
        const std::filesystem::path& path)
    {
        std::error_code error;

        return
            std::filesystem::exists(
                path,
                error) &&
            !error &&
            std::filesystem::file_size(
                path,
                error) > 0 &&
            !error;
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
            L"GZFX_TEST_SEMANTIC_CAPTURE",
            L"1") ||
        !SetEnvironmentVariableW(
            L"GZFX_TEST_SEMANTIC_MANUAL_CAPTURE",
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
        L"GZFX.SemanticProbeHarness";

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
            L"GZFX semantic probe",
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

    // Present once so GZFX acquires the live swap chain/context.
    swapChain->Present(0, 0);

    D3D11_TEXTURE2D_DESC normalDesc{};
    normalDesc.Width = 640;
    normalDesc.Height = 360;
    normalDesc.MipLevels = 1;
    normalDesc.ArraySize = 1;
    normalDesc.Format =
        DXGI_FORMAT_R8G8B8A8_UNORM;
    normalDesc.SampleDesc.Count = 1;
    normalDesc.Usage =
        D3D11_USAGE_DEFAULT;
    normalDesc.BindFlags =
        D3D11_BIND_RENDER_TARGET |
        D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* normal = nullptr;
    ID3D11RenderTargetView* normalRtv = nullptr;
    ID3D11ShaderResourceView* normalSrv = nullptr;

    if (FAILED(
            device->CreateTexture2D(
                &normalDesc,
                nullptr,
                &normal)) ||
        FAILED(
            device->CreateRenderTargetView(
                normal,
                nullptr,
                &normalRtv)) ||
        FAILED(
            device->CreateShaderResourceView(
                normal,
                nullptr,
                &normalSrv)))
    {
        return 9;
    }

    D3D11_TEXTURE2D_DESC motionDesc =
        normalDesc;

    motionDesc.Format =
        DXGI_FORMAT_R16G16_FLOAT;

    ID3D11Texture2D* motion = nullptr;
    ID3D11RenderTargetView* motionRtv = nullptr;
    ID3D11ShaderResourceView* motionSrv = nullptr;

    if (FAILED(
            device->CreateTexture2D(
                &motionDesc,
                nullptr,
                &motion)) ||
        FAILED(
            device->CreateRenderTargetView(
                motion,
                nullptr,
                &motionRtv)) ||
        FAILED(
            device->CreateShaderResourceView(
                motion,
                nullptr,
                &motionSrv)))
    {
        return 10;
    }

    D3D11_TEXTURE2D_DESC scalarDesc =
        normalDesc;

    scalarDesc.Format =
        DXGI_FORMAT_R32_FLOAT;

    ID3D11Texture2D* scalar = nullptr;
    ID3D11RenderTargetView* scalarRtv = nullptr;
    ID3D11ShaderResourceView* scalarSrv = nullptr;

    if (FAILED(
            device->CreateTexture2D(
                &scalarDesc,
                nullptr,
                &scalar)) ||
        FAILED(
            device->CreateRenderTargetView(
                scalar,
                nullptr,
                &scalarRtv)) ||
        FAILED(
            device->CreateShaderResourceView(
                scalar,
                nullptr,
                &scalarSrv)))
    {
        return 11;
    }

    ID3D11Texture2D* scalarCopy = nullptr;
    ID3D11ShaderResourceView* scalarCopySrv = nullptr;

    if (FAILED(
            device->CreateTexture2D(
                &scalarDesc,
                nullptr,
                &scalarCopy)) ||
        FAILED(
            device->CreateShaderResourceView(
                scalarCopy,
                nullptr,
                &scalarCopySrv)))
    {
        return 18;
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

    D3D11_SHADER_RESOURCE_VIEW_DESC
        depthSrvDesc{};

    depthSrvDesc.Format =
        DXGI_FORMAT_R24_UNORM_X8_TYPELESS;

    depthSrvDesc.ViewDimension =
        D3D11_SRV_DIMENSION_TEXTURE2D;

    depthSrvDesc.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* depthSrv = nullptr;

    if (FAILED(
            device->CreateShaderResourceView(
                depth,
                &depthSrvDesc,
                &depthSrv)))
    {
        return 14;
    }

    D3D11_VIEWPORT viewport{};
    viewport.Width = 640.0f;
    viewport.Height = 360.0f;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    for (int frame = 0;
         frame < 11;
         ++frame)
    {
        context->RSSetViewports(
            1,
            &viewport);

        ID3D11RenderTargetView*
            targets[3] =
        {
            normalRtv,
            motionRtv,
            scalarRtv
        };

        context->OMSetRenderTargets(
            3,
            targets,
            depthView);

        for (int draw = 0;
             draw < 4;
             ++draw)
        {
            context->DrawIndexed(
                3,
                0,
                0);
        }

        context->OMSetRenderTargets(
            0,
            nullptr,
            nullptr);

        context->CopyResource(
            scalarCopy,
            scalar);

        ID3D11ShaderResourceView*
            pixelResources[4] =
        {
            depthSrv,
            normalSrv,
            motionSrv,
            scalarCopySrv
        };

        context->PSSetShaderResources(
            0,
            4,
            pixelResources);

        context->Draw(
            3,
            0);

        ID3D11ShaderResourceView*
            nullResources[4] =
        {
            nullptr,
            nullptr,
            nullptr,
            nullptr
        };

        context->PSSetShaderResources(
            0,
            4,
            nullResources);

        swapChain->Present(
            0,
            0);
    }

    const std::filesystem::path diagnostics =
        outputRoot /
        L"GZFX" /
        L"Diagnostics";

    const auto semantics =
        diagnostics /
        L"renderer_semantics.txt";

    const auto bindings =
        diagnostics /
        L"semantic_bindings.csv";

    const auto manualSemantics =
        diagnostics /
        L"renderer_semantics_manual_0001.txt";

    const auto manualBindings =
        diagnostics /
        L"semantic_bindings_manual_0001.csv";

    const auto pipelineSummary =
        diagnostics /
        L"pipeline_summary.txt";

    const auto pipelinePasses =
        diagnostics /
        L"pipeline_passes.csv";

    const auto pipelineAccesses =
        diagnostics /
        L"pipeline_accesses.csv";

    const auto pipelineResources =
        diagnostics /
        L"pipeline_resources.csv";

    const auto pipelineShaders =
        diagnostics /
        L"pipeline_shaders.csv";

    const auto pipelineSignatures =
        diagnostics /
        L"pipeline_signatures.csv";

    const auto pipelineEdges =
        diagnostics /
        L"pipeline_edges.csv";

    const auto manualPipelineSummary =
        diagnostics /
        L"pipeline_summary_manual_0001.txt";

    if (!ExistsNonEmpty(semantics) ||
        !ExistsNonEmpty(bindings) ||
        !ExistsNonEmpty(manualSemantics) ||
        !ExistsNonEmpty(manualBindings) ||
        !ExistsNonEmpty(pipelineSummary) ||
        !ExistsNonEmpty(pipelinePasses) ||
        !ExistsNonEmpty(pipelineAccesses) ||
        !ExistsNonEmpty(pipelineResources) ||
        !ExistsNonEmpty(pipelineShaders) ||
        !ExistsNonEmpty(pipelineSignatures) ||
        !ExistsNonEmpty(pipelineEdges) ||
        !ExistsNonEmpty(manualPipelineSummary))
    {
        return 15;
    }

    const std::string semanticText =
        ReadAll(semantics);

    const std::string manualSemanticText =
        ReadAll(manualSemantics);

    const std::string pipelineSummaryText =
        ReadAll(pipelineSummary);

    const std::string pipelinePassesText =
        ReadAll(pipelinePasses);

    const std::string pipelineResourcesText =
        ReadAll(pipelineResources);

    const std::string pipelineShadersText =
        ReadAll(pipelineShaders);

    const std::string pipelineSignaturesText =
        ReadAll(pipelineSignatures);

    const std::string pipelineEdgesText =
        ReadAll(pipelineEdges);

    if (semanticText.find(
            "format=28") ==
            std::string::npos ||
        semanticText.find(
            "format=34") ==
            std::string::npos ||
        semanticText.find(
            "format=41") ==
            std::string::npos ||
        manualSemanticText.find(
            "trace_frames=") ==
            std::string::npos)
    {
        std::cerr
            << semanticText
            << "\n";

        return 16;
    }

    if (pipelineSummaryText.find(
            "passes=") ==
            std::string::npos ||
        pipelineSummaryText.find(
            "copy_resolve_mip_passes=") ==
            std::string::npos ||
        pipelinePassesText.find(
            "copy_resource") ==
            std::string::npos ||
        pipelineResourcesText.find(
            "texture2d") ==
            std::string::npos ||
        pipelineSummaryText.find(
            "creation_interception_enabled=false") ==
            std::string::npos ||
        pipelineShadersText.find(
            "shader,birth,stage") ==
            std::string::npos ||
        pipelineSignaturesText.find(
            "signature,kind,count") ==
            std::string::npos ||
        pipelineEdgesText.find(
            "resource_lineage") ==
            std::string::npos)
    {
        std::cerr
            << pipelineSummaryText
            << "\n"
            << pipelinePassesText
            << "\n"
            << pipelineResourcesText
            << "\n"
            << pipelineShadersText
            << "\n"
            << pipelineSignaturesText
            << "\n"
            << pipelineEdgesText
            << "\n";

        return 19;
    }

    std::size_t previewCount = 0;

    for (const auto& entry :
         std::filesystem::directory_iterator(
             diagnostics))
    {
        if (entry.is_regular_file() &&
            entry.path().filename().
                wstring().find(
                    L"semantic_candidate_") ==
                0 &&
            entry.path().extension() ==
                L".bmp")
        {
            ++previewCount;
        }
    }

    if (previewCount < 3)
    {
        return 17;
    }

    if (depthSrv) depthSrv->Release();
    if (depthView) depthView->Release();
    if (depth) depth->Release();

    if (scalarCopySrv) scalarCopySrv->Release();
    if (scalarCopy) scalarCopy->Release();

    if (scalarSrv) scalarSrv->Release();
    if (scalarRtv) scalarRtv->Release();
    if (scalar) scalar->Release();

    if (motionSrv) motionSrv->Release();
    if (motionRtv) motionRtv->Release();
    if (motion) motion->Release();

    if (normalSrv) normalSrv->Release();
    if (normalRtv) normalRtv->Release();
    if (normal) normal->Release();

    if (context) context->Release();
    if (device) device->Release();
    if (swapChain) swapChain->Release();

    DestroyWindow(window);
    FreeLibrary(gzfx);

    std::wcout
        << L"Renderer semantic probe harness passed.\n";

    return 0;
}
