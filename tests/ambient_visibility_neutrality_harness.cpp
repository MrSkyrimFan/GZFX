#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using InitializeFn = bool (*)();

namespace
{
    constexpr UINT kWidth = 640;
    constexpr UINT kHeight = 360;

    enum class Scenario
    {
        FlatAo,
        UniformSsil
    };

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
        const Scenario scenario)
    {
        const std::filesystem::path root = outputRoot / L"GZFX";
        std::error_code error;
        std::filesystem::create_directories(root, error);

        if (error)
        {
            return false;
        }

        const bool ao = scenario == Scenario::FlatAo;
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
            << "Enabled=" << (ao ? 1 : 0) << "\n"
            << "Strength=1.0\n"
            << "RadiusPixels=64\n"
            << "Quality=3\n"
            << "[IndirectBounce]\n"
            << "Enabled=" << (ao ? 0 : 1) << "\n"
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

    double MaximumRgbDifference(
        const std::vector<float>& before,
        const std::vector<float>& after)
    {
        double maximum = 0.0;

        for (std::size_t pixel = 0;
             pixel < static_cast<std::size_t>(kWidth) * kHeight;
             ++pixel)
        {
            const std::size_t offset = pixel * 4;

            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                maximum = std::max(
                    maximum,
                    static_cast<double>(
                        std::abs(
                            after[offset + channel] -
                            before[offset + channel])));
            }
        }

        return maximum;
    }

    std::string ReadAll(const std::filesystem::path& path)
    {
        std::ifstream in(path);
        return std::string(
            std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>());
    }

    double ReadMetric(
        const std::string& text,
        const std::string& key)
    {
        const std::string prefix = key + "=";
        const std::size_t start = text.find(prefix);

        if (start == std::string::npos)
        {
            return std::numeric_limits<double>::quiet_NaN();
        }

        const std::size_t valueStart = start + prefix.size();
        const std::size_t end = text.find_first_of("\r\n", valueStart);

        try
        {
            return std::stod(text.substr(valueStart, end - valueStart));
        }
        catch (...)
        {
            return std::numeric_limits<double>::quiet_NaN();
        }
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4)
    {
        std::wcerr
            << L"Usage: GZFXAmbientVisibilityNeutralityHarness "
               L"<GZFX.dll> <output-root> <flat_ao|uniform_ssil>\n";
        return 2;
    }

    const std::filesystem::path dllPath(argv[1]);
    const std::filesystem::path outputRoot(argv[2]);
    const std::wstring scenarioName(argv[3]);

    const Scenario scenario = scenarioName == L"flat_ao"
        ? Scenario::FlatAo
        : Scenario::UniformSsil;

    if (scenarioName != L"flat_ao" &&
        scenarioName != L"uniform_ssil")
    {
        return 3;
    }

    std::error_code error;
    std::filesystem::remove_all(outputRoot, error);
    error.clear();
    std::filesystem::create_directories(outputRoot, error);

    if (error ||
        !WriteSettings(outputRoot, scenario) ||
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

    const wchar_t* className = L"GZFX.AmbientVisibilityNeutralityHarness";
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
        L"GZFX ambient visibility neutrality harness",
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

    ComPtr<IDXGISwapChain> swapChain;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
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

    ComPtr<ID3D11Texture2D> backbuffer;
    ComPtr<ID3D11RenderTargetView> backbufferRtv;

    if (FAILED(swapChain->GetBuffer(
            0,
            IID_PPV_ARGS(&backbuffer))) ||
        FAILED(device->CreateRenderTargetView(
            backbuffer.Get(),
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

    for (std::size_t pixel = 0;
         pixel < static_cast<std::size_t>(kWidth) * kHeight;
         ++pixel)
    {
        const std::size_t offset = pixel * 4;
        sceneData[offset + 0] = 0.35f;
        sceneData[offset + 1] = 0.35f;
        sceneData[offset + 2] = 0.35f;
        sceneData[offset + 3] = 1.0f;
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

    ComPtr<ID3D11Texture2D> sceneColor;
    ComPtr<ID3D11RenderTargetView> sceneRtv;

    if (FAILED(device->CreateTexture2D(
            &colorDesc,
            &colorInitial,
            &sceneColor)) ||
        FAILED(device->CreateRenderTargetView(
            sceneColor.Get(),
            nullptr,
            &sceneRtv)))
    {
        return 11;
    }

    D3D11_TEXTURE2D_DESC stagingDesc = colorDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ComPtr<ID3D11Texture2D> stagingBefore;
    ComPtr<ID3D11Texture2D> stagingAfter;

    if (FAILED(device->CreateTexture2D(
            &stagingDesc,
            nullptr,
            &stagingBefore)) ||
        FAILED(device->CreateTexture2D(
            &stagingDesc,
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

    ComPtr<ID3D11Texture2D> depth;
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

    ComPtr<ID3D11DepthStencilView> depthView;
    if (FAILED(device->CreateDepthStencilView(
            depth.Get(),
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

    for (std::size_t index = 172; index < camera.size(); ++index)
    {
        camera[index] = 1.0f;
    }

    D3D11_BUFFER_DESC cameraDesc{};
    cameraDesc.ByteWidth =
        static_cast<UINT>(camera.size() * sizeof(float));
    cameraDesc.Usage = D3D11_USAGE_DEFAULT;
    cameraDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    D3D11_SUBRESOURCE_DATA cameraInitial{};
    cameraInitial.pSysMem = camera.data();

    ComPtr<ID3D11Buffer> cameraBuffer;
    if (FAILED(device->CreateBuffer(
            &cameraDesc,
            &cameraInitial,
            &cameraBuffer)))
    {
        return 15;
    }

    swapChain->Present(0, 0);

    ID3D11Buffer* cameraRaw = cameraBuffer.Get();
    context->VSSetConstantBuffers(12, 1, &cameraRaw);
    context->PSSetConstantBuffers(12, 1, &cameraRaw);

    ID3D11RenderTargetView* sceneRaw = sceneRtv.Get();
    ID3D11DepthStencilView* depthRaw = depthView.Get();

    for (int index = 0; index < 10; ++index)
    {
        context->OMSetRenderTargets(1, &sceneRaw, depthRaw);
        context->DrawIndexed(3, 0, 0);
    }

    for (int index = 0; index < 25; ++index)
    {
        context->DrawIndexed(3, 0, 0);
    }

    context->OMSetRenderTargets(1, &sceneRaw, nullptr);
    context->DrawIndexed(3, 0, 0);
    context->CopyResource(stagingBefore.Get(), sceneColor.Get());

    ID3D11RenderTargetView* backbufferRaw = backbufferRtv.Get();
    context->OMSetRenderTargets(1, &backbufferRaw, nullptr);
    context->CopyResource(stagingAfter.Get(), sceneColor.Get());

    std::vector<float> before;
    std::vector<float> after;

    if (!ReadTexture(context.Get(), stagingBefore.Get(), before) ||
        !ReadTexture(context.Get(), stagingAfter.Get(), after))
    {
        return 16;
    }

    const std::filesystem::path diagnostics =
        outputRoot / L"GZFX" / L"Diagnostics";
    const std::string stackLog = ReadAll(
        diagnostics / L"ambient_depth_stack.txt");
    const std::string fieldLog = ReadAll(
        diagnostics / L"ambient_depth_fields.txt");

    const double maximumDifference = MaximumRgbDifference(before, after);
    const double aoMean = ReadMetric(fieldLog, "ao_mean");
    const double aoNonzero = ReadMetric(fieldLog, "ao_nonzero_fraction");
    const double ssilMean = ReadMetric(fieldLog, "ssil_luma_mean");
    const double ssilNonzero = ReadMetric(fieldLog, "ssil_nonzero_fraction");

    const bool applied =
        stackLog.find("status=applied") != std::string::npos;

    bool neutral = false;

    if (scenario == Scenario::FlatAo)
    {
        neutral =
            maximumDifference < 0.010 &&
            std::isfinite(aoMean) &&
            std::isfinite(aoNonzero) &&
            aoMean < 0.015 &&
            aoNonzero < 0.25;
    }
    else
    {
        neutral =
            maximumDifference < 0.010 &&
            std::isfinite(ssilMean) &&
            std::isfinite(ssilNonzero) &&
            ssilMean < 0.002 &&
            ssilNonzero < 0.10;
    }

    if (!applied || !neutral)
    {
        std::cerr
            << "ambient visibility neutrality failed\n"
            << "scenario="
            << (scenario == Scenario::FlatAo ? "flat_ao" : "uniform_ssil")
            << "\n"
            << "applied=" << applied << "\n"
            << "maximum_rgb_difference=" << maximumDifference << "\n"
            << "ao_mean=" << aoMean << "\n"
            << "ao_nonzero_fraction=" << aoNonzero << "\n"
            << "ssil_mean=" << ssilMean << "\n"
            << "ssil_nonzero_fraction=" << ssilNonzero << "\n"
            << stackLog << "\n"
            << fieldLog << "\n";

        return 17;
    }

    DestroyWindow(window);
    FreeLibrary(gzfx);

    std::wcout
        << L"Ambient visibility neutrality passed: "
        << scenarioName
        << L"\n";

    return 0;
}
