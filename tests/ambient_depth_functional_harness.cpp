#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using InitializeFn = bool (*)();

namespace
{
    constexpr UINT kWidth = 640;
    constexpr UINT kHeight = 360;

    LRESULT CALLBACK WindowProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam)
    {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void SetIdentity(float* matrix)
    {
        std::fill(matrix, matrix + 16, 0.0f);
        matrix[0] = 1.0f;
        matrix[5] = 1.0f;
        matrix[10] = 1.0f;
        matrix[15] = 1.0f;
    }

    bool WriteSettings(
        const std::filesystem::path& outputRoot,
        const bool aoScenario)
    {
        const std::filesystem::path root = outputRoot / L"GZFX";
        std::error_code error;
        std::filesystem::create_directories(root, error);

        if (error)
        {
            return false;
        }

        std::ofstream out(
            root / L"Settings.ini",
            std::ios::out | std::ios::trunc);

        if (!out)
        {
            return false;
        }

        out
            << "[AmbientContact]\n"
            << "Strength=0\n"
            << "RadiusPixels=4\n"
            << "[HorizonOcclusion]\n"
            << "Enabled=" << (aoScenario ? 1 : 0) << "\n"
            << "Strength=1.0\n"
            << "RadiusPixels=64\n"
            << "Quality=3\n"
            << "[IndirectBounce]\n"
            << "Enabled=" << (aoScenario ? 0 : 1) << "\n"
            << "Strength=0.60\n"
            << "RadiusPixels=64\n"
            << "Saturation=1.0\n"
            << "[LocalGlow]\n"
            << "Enabled=0\n"
            << "Strength=0\n"
            << "Threshold=4\n"
            << "RadiusPixels=1\n"
            << "[Development]\n"
            << "DefaultMode=2\n"
            << "ActivationDrawIndexedThreshold=0\n"
            << "AccessibleDiagnostics=0\n"
            << "CaptureOnModeChange=0\n";

        return static_cast<bool>(out);
    }

    bool ReadTexture(
        ID3D11DeviceContext* context,
        ID3D11Texture2D* staging,
        std::vector<float>& values)
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};

        if (FAILED(context->Map(
                staging,
                0,
                D3D11_MAP_READ,
                0,
                &mapped)))
        {
            return false;
        }

        values.resize(
            static_cast<std::size_t>(kWidth) * kHeight * 4);

        for (UINT y = 0; y < kHeight; ++y)
        {
            const auto* source =
                reinterpret_cast<const float*>(
                    static_cast<const std::uint8_t*>(mapped.pData) +
                    static_cast<std::size_t>(y) * mapped.RowPitch);

            std::copy(
                source,
                source + static_cast<std::size_t>(kWidth) * 4,
                values.begin() +
                    static_cast<std::size_t>(y) * kWidth * 4);
        }

        context->Unmap(staging, 0);
        return true;
    }

    double RegionLuminance(
        const std::vector<float>& values,
        const UINT x0,
        const UINT x1,
        const UINT y0,
        const UINT y1)
    {
        double sum = 0.0;
        std::uint64_t count = 0;

        for (UINT y = y0; y < y1; ++y)
        {
            for (UINT x = x0; x < x1; ++x)
            {
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * kWidth + x) * 4;

                sum +=
                    values[offset + 0] * 0.2126 +
                    values[offset + 1] * 0.7152 +
                    values[offset + 2] * 0.0722;

                ++count;
            }
        }

        return count ? sum / static_cast<double>(count) : 0.0;
    }

    double RegionChannel(
        const std::vector<float>& values,
        const UINT channel,
        const UINT x0,
        const UINT x1,
        const UINT y0,
        const UINT y1)
    {
        double sum = 0.0;
        std::uint64_t count = 0;

        for (UINT y = y0; y < y1; ++y)
        {
            for (UINT x = x0; x < x1; ++x)
            {
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * kWidth + x) * 4;

                sum += values[offset + channel];
                ++count;
            }
        }

        return count ? sum / static_cast<double>(count) : 0.0;
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
    if (argc != 4)
    {
        std::wcerr
            << L"Usage: GZFXAmbientDepthFunctionalHarness "
               L"<GZFX.dll> <output-root> <ao|ssil>\n";
        return 2;
    }

    const std::filesystem::path dllPath(argv[1]);
    const std::filesystem::path outputRoot(argv[2]);
    const std::wstring scenario(argv[3]);
    const bool aoScenario = scenario == L"ao";
    const bool ssilScenario = scenario == L"ssil";

    if (!aoScenario && !ssilScenario)
    {
        return 3;
    }

    std::error_code error;
    std::filesystem::remove_all(outputRoot, error);
    error.clear();
    std::filesystem::create_directories(outputRoot, error);

    if (error ||
        !WriteSettings(outputRoot, aoScenario) ||
        !SetEnvironmentVariableW(
            L"GZFX_TEST_OUTPUT_ROOT",
            outputRoot.c_str()) ||
        !SetEnvironmentVariableW(
            L"GZFX_TEST_FAST_CAPTURE",
            L"1"))
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

    const wchar_t* className = L"GZFX.AmbientDepthFunctionalHarness";
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = className;

    if (!RegisterClassExW(&windowClass) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return 7;
    }

    const HWND window = CreateWindowExW(
        0,
        className,
        L"GZFX ambient-depth functional harness",
        WS_OVERLAPPEDWINDOW,
        0,
        0,
        static_cast<int>(kWidth),
        static_cast<int>(kHeight),
        nullptr,
        nullptr,
        windowClass.hInstance,
        nullptr);

    if (!window)
    {
        return 8;
    }

    DXGI_SWAP_CHAIN_DESC swapDescription{};
    swapDescription.BufferDesc.Width = kWidth;
    swapDescription.BufferDesc.Height = kHeight;
    swapDescription.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDescription.SampleDesc.Count = 1;
    swapDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDescription.BufferCount = 2;
    swapDescription.OutputWindow = window;
    swapDescription.Windowed = TRUE;
    swapDescription.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* swapChain = nullptr;
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
            &swapDescription,
            &swapChain,
            &device,
            &featureLevel,
            &context)))
    {
        return 9;
    }

    ID3D11Texture2D* backbuffer = nullptr;
    ID3D11RenderTargetView* backbufferRtv = nullptr;

    if (FAILED(swapChain->GetBuffer(
            0,
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(&backbuffer))) ||
        FAILED(device->CreateRenderTargetView(
            backbuffer,
            nullptr,
            &backbufferRtv)))
    {
        return 10;
    }

    std::vector<float> sceneData(
        static_cast<std::size_t>(kWidth) * kHeight * 4);
    std::vector<float> depthData(
        static_cast<std::size_t>(kWidth) * kHeight,
        0.72f);

    for (UINT y = 0; y < kHeight; ++y)
    {
        for (UINT x = 0; x < kWidth; ++x)
        {
            const std::size_t colorOffset =
                (static_cast<std::size_t>(y) * kWidth + x) * 4;

            sceneData[colorOffset + 0] = 0.15f;
            sceneData[colorOffset + 1] = 0.15f;
            sceneData[colorOffset + 2] = 0.15f;
            sceneData[colorOffset + 3] = 1.0f;

            if (x >= 340 && x < 430 && y >= 40 && y < 320)
            {
                const std::size_t depthOffset =
                    static_cast<std::size_t>(y) * kWidth + x;

                depthData[depthOffset] = 0.30f;
                sceneData[colorOffset + 0] = 4.0f;
                sceneData[colorOffset + 1] = 0.45f;
                sceneData[colorOffset + 2] = 0.08f;
            }
        }
    }

    D3D11_TEXTURE2D_DESC colorDesc{};
    colorDesc.Width = kWidth;
    colorDesc.Height = kHeight;
    colorDesc.MipLevels = 1;
    colorDesc.ArraySize = 1;
    colorDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    colorDesc.SampleDesc.Count = 1;
    colorDesc.Usage = D3D11_USAGE_DEFAULT;
    colorDesc.BindFlags =
        D3D11_BIND_RENDER_TARGET |
        D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA colorInitial{};
    colorInitial.pSysMem = sceneData.data();
    colorInitial.SysMemPitch = kWidth * sizeof(float) * 4;

    ID3D11Texture2D* sceneColor = nullptr;
    ID3D11RenderTargetView* sceneRtv = nullptr;

    if (FAILED(device->CreateTexture2D(
            &colorDesc,
            &colorInitial,
            &sceneColor)) ||
        FAILED(device->CreateRenderTargetView(
            sceneColor,
            nullptr,
            &sceneRtv)))
    {
        return 11;
    }

    D3D11_TEXTURE2D_DESC colorStagingDesc = colorDesc;
    colorStagingDesc.Usage = D3D11_USAGE_STAGING;
    colorStagingDesc.BindFlags = 0;
    colorStagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ID3D11Texture2D* stagingBefore = nullptr;
    ID3D11Texture2D* stagingAfter = nullptr;

    if (FAILED(device->CreateTexture2D(
            &colorStagingDesc,
            nullptr,
            &stagingBefore)) ||
        FAILED(device->CreateTexture2D(
            &colorStagingDesc,
            nullptr,
            &stagingAfter)))
    {
        return 12;
    }

    D3D11_TEXTURE2D_DESC depthDesc{};
    depthDesc.Width = kWidth;
    depthDesc.Height = kHeight;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags =
        D3D11_BIND_DEPTH_STENCIL |
        D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA depthInitial{};
    depthInitial.pSysMem = depthData.data();
    depthInitial.SysMemPitch = kWidth * sizeof(float);

    ID3D11Texture2D* depth = nullptr;

    if (FAILED(device->CreateTexture2D(
            &depthDesc,
            &depthInitial,
            &depth)))
    {
        return 13;
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC depthViewDesc{};
    depthViewDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;

    ID3D11DepthStencilView* depthView = nullptr;

    if (FAILED(device->CreateDepthStencilView(
            depth,
            &depthViewDesc,
            &depthView)))
    {
        return 14;
    }

    std::array<float, 180> camera{};

    for (int matrix = 0; matrix < 10; ++matrix)
    {
        SetIdentity(camera.data() + matrix * 16);
    }

    camera[168] = 1.0f;
    camera[169] = 1.0f;
    camera[170] = 1.0f;
    camera[171] = 1.0f;

    for (std::size_t i = 172; i < camera.size(); ++i)
    {
        camera[i] = 1.0f;
    }

    D3D11_BUFFER_DESC cameraDesc{};
    cameraDesc.ByteWidth =
        static_cast<UINT>(camera.size() * sizeof(float));
    cameraDesc.Usage = D3D11_USAGE_DEFAULT;
    cameraDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    D3D11_SUBRESOURCE_DATA cameraInitial{};
    cameraInitial.pSysMem = camera.data();

    ID3D11Buffer* cameraBuffer = nullptr;

    if (FAILED(device->CreateBuffer(
            &cameraDesc,
            &cameraInitial,
            &cameraBuffer)))
    {
        return 15;
    }

    swapChain->Present(0, 0);

    context->VSSetConstantBuffers(12, 1, &cameraBuffer);
    context->PSSetConstantBuffers(12, 1, &cameraBuffer);

    for (int i = 0; i < 10; ++i)
    {
        context->OMSetRenderTargets(1, &sceneRtv, depthView);
        context->DrawIndexed(3, 0, 0);
    }

    for (int i = 0; i < 25; ++i)
    {
        context->DrawIndexed(3, 0, 0);
    }

    context->OMSetRenderTargets(1, &sceneRtv, nullptr);
    context->DrawIndexed(3, 0, 0);
    context->CopyResource(stagingBefore, sceneColor);

    context->OMSetRenderTargets(1, &backbufferRtv, nullptr);
    context->CopyResource(stagingAfter, sceneColor);

    std::vector<float> before;
    std::vector<float> after;

    if (!ReadTexture(context, stagingBefore, before) ||
        !ReadTexture(context, stagingAfter, after))
    {
        return 16;
    }

    const double beforeLuma = RegionLuminance(
        before,
        300,
        338,
        80,
        280);
    const double afterLuma = RegionLuminance(
        after,
        300,
        338,
        80,
        280);

    const double beforeRed = RegionChannel(
        before,
        0,
        300,
        338,
        80,
        280);
    const double afterRed = RegionChannel(
        after,
        0,
        300,
        338,
        80,
        280);

    const std::filesystem::path diagnostics =
        outputRoot / L"GZFX" / L"Diagnostics";

    const std::string stackLog = ReadAll(
        diagnostics / L"ambient_depth_stack.txt");
    const std::string fieldLog = ReadAll(
        diagnostics / L"ambient_depth_fields.txt");

    const bool applied =
        stackLog.find("status=applied") != std::string::npos;
    const bool fieldCaptured =
        fieldLog.find("GZFX ambient depth fields") != std::string::npos;

    const bool effectChanged = aoScenario
        ? afterLuma < beforeLuma - 0.0015
        : afterRed > beforeRed + 0.0015;

    if (!applied || !fieldCaptured || !effectChanged)
    {
        std::cerr
            << "ambient-depth functional scenario failed\n"
            << "scenario=" << (aoScenario ? "ao" : "ssil") << "\n"
            << "applied=" << applied << "\n"
            << "field_captured=" << fieldCaptured << "\n"
            << "before_luma=" << beforeLuma << "\n"
            << "after_luma=" << afterLuma << "\n"
            << "before_red=" << beforeRed << "\n"
            << "after_red=" << afterRed << "\n"
            << stackLog << "\n"
            << fieldLog << "\n";

        return 17;
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
        << L"Ambient-depth functional scenario passed: "
        << scenario
        << L"\n";

    return 0;
}
