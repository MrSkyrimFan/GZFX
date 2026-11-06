#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using InitializeFn = bool (*)();
using IsMenuOpenFn = bool (*)();
using SubmitMouseInputFn = void (*)(
    LONG,
    LONG,
    LONG,
    std::uint32_t);

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

    std::array<std::uint8_t, 4> ReadBmpPixel(
        const std::filesystem::path& path,
        const int x,
        const int yFromTop)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            return {};
        }

        BITMAPFILEHEADER fileHeader{};
        BITMAPINFOHEADER infoHeader{};

        in.read(
            reinterpret_cast<char*>(&fileHeader),
            sizeof(fileHeader));

        in.read(
            reinterpret_cast<char*>(&infoHeader),
            sizeof(infoHeader));

        if (!in ||
            fileHeader.bfType != 0x4D42 ||
            infoHeader.biWidth != 640 ||
            infoHeader.biHeight != 360 ||
            infoHeader.biBitCount != 32)
        {
            return {};
        }

        constexpr int width = 640;
        constexpr int height = 360;

        const int yFromBottom =
            height - 1 - yFromTop;

        const std::streamoff offset =
            fileHeader.bfOffBits +
            static_cast<std::streamoff>(
                (yFromBottom * width + x) * 4);

        in.seekg(offset);

        std::array<std::uint8_t, 4> pixel{};
        in.read(
            reinterpret_cast<char*>(pixel.data()),
            static_cast<std::streamsize>(pixel.size()));

        return in ? pixel : std::array<std::uint8_t, 4>{};
    }

    bool CenterSeparatorIsAccessiblePattern(
        const std::filesystem::path& path)
    {
        const auto white =
            ReadBmpPixel(path, 320, 336);

        const auto black =
            ReadBmpPixel(path, 320, 344);

        const bool firstIsWhite =
            white[0] > 240 &&
            white[1] > 240 &&
            white[2] > 240;

        const bool secondIsBlack =
            black[0] < 15 &&
            black[1] < 15 &&
            black[2] < 15;

        return firstIsWhite && secondIsBlack;
    }

    bool SettingsMenuLooksPresent(
        const std::filesystem::path& path)
    {
        const auto selectedRow =
            ReadBmpPixel(path, 20, 70);

        const auto panelBackground =
            ReadBmpPixel(path, 20, 48);

        const bool selectedIsLight =
            selectedRow[0] > 210 &&
            selectedRow[1] > 210 &&
            selectedRow[2] > 210;

        const bool panelIsDark =
            panelBackground[0] < 60 &&
            panelBackground[1] < 60 &&
            panelBackground[2] < 60;

        return selectedIsLight && panelIsDark;
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
        !SetEnvironmentVariableW(
            L"GZFX_TEST_OUTPUT_ROOT",
            outputRoot.c_str()) ||
        !SetEnvironmentVariableW(
            L"GZFX_TEST_FAST_CAPTURE",
            L"1") ||
        !SetEnvironmentVariableW(
            L"GZFX_TEST_MENU_VISIBLE",
            L"1") ||
        !SetEnvironmentVariableW(
            L"GZFX_TEST_MANUAL_CAPTURE",
            L"1"))
    {
        return 3;
    }

    const HMODULE gzfx =
        LoadLibraryW(dllPath.c_str());

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

    const auto isMenuOpen =
        reinterpret_cast<IsMenuOpenFn>(
            GetProcAddress(
                gzfx,
                "GZFX_IsSettingsMenuOpen"));

    const auto submitMouse =
        reinterpret_cast<SubmitMouseInputFn>(
            GetProcAddress(
                gzfx,
                "GZFX_SubmitMouseInput"));

    if (isMenuOpen == nullptr ||
        submitMouse == nullptr)
    {
        return 6;
    }

    const wchar_t* className =
        L"GZFX.VisiblePassHarness";

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

    const HWND hwnd =
        CreateWindowExW(
            0,
            className,
            L"GZFX visible pass",
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
    swapDesc.BufferDesc.Format =
        DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.BufferUsage =
        DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = 2;
    swapDesc.OutputWindow = hwnd;
    swapDesc.Windowed = TRUE;
    swapDesc.SwapEffect =
        DXGI_SWAP_EFFECT_DISCARD;

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

    ID3D11Texture2D* backbuffer = nullptr;
    ID3D11RenderTargetView* backRtv = nullptr;

    if (FAILED(swap->GetBuffer(
            0,
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(
                &backbuffer))) ||
        FAILED(device->CreateRenderTargetView(
            backbuffer,
            nullptr,
            &backRtv)))
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
    colorDesc.Usage = D3D11_USAGE_DEFAULT;
    colorDesc.BindFlags =
        D3D11_BIND_RENDER_TARGET |
        D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* sceneColor = nullptr;
    ID3D11RenderTargetView* sceneRtv = nullptr;

    if (FAILED(device->CreateTexture2D(
            &colorDesc,
            nullptr,
            &sceneColor)) ||
        FAILED(device->CreateRenderTargetView(
            sceneColor,
            nullptr,
            &sceneRtv)))
    {
        return 10;
    }

    D3D11_TEXTURE2D_DESC depthDesc{};
    depthDesc.Width = 640;
    depthDesc.Height = 360;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format =
        DXGI_FORMAT_R24G8_TYPELESS;
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
        return 11;
    }

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format =
        DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension =
        D3D11_DSV_DIMENSION_TEXTURE2D;

    ID3D11DepthStencilView* depthDsv = nullptr;

    if (FAILED(device->CreateDepthStencilView(
            depth,
            &dsvDesc,
            &depthDsv)))
    {
        return 12;
    }

    std::array<float, 180> camera{};

    for (int matrix = 0; matrix < 10; ++matrix)
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

    if (FAILED(device->CreateBuffer(
            &cameraDesc,
            &cameraData,
            &cameraBuffer)))
    {
        return 13;
    }

    // First Present lets GZFX acquire this real swap chain/context
    // and opens the forced test menu.
    swap->Present(0, 0);

    if (!isMenuOpen())
    {
        return 16;
    }

    // Drive the first current menu row through the same exported
    // relative-mouse bridge used by the DirectInput path. In 0.0.36 the
    // first row is PRODUCT MASTER, so this click must persist Enabled=0.
    submitMouse(
        200,
        -314,
        0,
        0x1u);

    submitMouse(
        0,
        0,
        0,
        0x0u);

    D3D11_VIEWPORT viewport{};
    viewport.Width = 640.0f;
    viewport.Height = 360.0f;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    const FLOAT hdrColor[4] =
    {
        2.5f,
        0.8f,
        0.15f,
        1.0f
    };

    const FLOAT backColor[4] =
    {
        0.18f,
        0.22f,
        0.26f,
        1.0f
    };

    for (int frame = 0;
         frame < 12;
         ++frame)
    {
        context->RSSetViewports(
            1,
            &viewport);

        context->OMSetRenderTargets(
            1,
            &sceneRtv,
            depthDsv);

        context->ClearRenderTargetView(
            sceneRtv,
            hdrColor);

        context->ClearDepthStencilView(
            depthDsv,
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

        context->OMSetRenderTargets(
            1,
            &backRtv,
            nullptr);

        context->ClearRenderTargetView(
            backRtv,
            backColor);

        swap->Present(0, 0);
    }

    const std::filesystem::path diagnostics =
        outputRoot /
        L"GZFX" /
        L"Diagnostics";

    const auto visible =
        diagnostics /
        L"visible_pass.txt";

    const auto validation =
        diagnostics /
        L"camera_validation.txt";

    const auto capture =
        diagnostics /
        L"visible_pass.bmp";

    const auto stageTrace =
        diagnostics /
        L"stage_trace.csv";

    const auto settingsFile =
        outputRoot /
        L"GZFX" /
        L"Settings.ini";

    const auto manualCapture =
        diagnostics /
        L"manual_capture_0001.bmp";

    const auto manualSettings =
        diagnostics /
        L"manual_capture_0001.txt";

    const bool hasVisible = ExistsNonEmpty(visible);
    const bool hasValidation = ExistsNonEmpty(validation);
    const bool hasCapture = ExistsNonEmpty(capture);
    const bool hasStageTrace = ExistsNonEmpty(stageTrace);
    const bool hasSettings = ExistsNonEmpty(settingsFile);

    const bool hasManualCapture =
        ExistsNonEmpty(manualCapture);

    const bool hasManualSettings =
        ExistsNonEmpty(manualSettings);

    if (!hasVisible ||
        !hasValidation ||
        !hasCapture ||
        !hasStageTrace ||
        !hasSettings ||
        !hasManualCapture ||
        !hasManualSettings)
    {
        std::wcerr
            << L"visible-pass evidence missing: visible="
            << hasVisible
            << L" validation="
            << hasValidation
            << L" capture="
            << hasCapture
            << L" stage_trace="
            << hasStageTrace
            << L" settings="
            << hasSettings
            << L" manual_capture="
            << hasManualCapture
            << L" manual_settings="
            << hasManualSettings
            << L"\n";

        if (hasVisible)
        {
            std::cerr << ReadAll(visible) << "\n";
        }

        if (hasValidation)
        {
            std::cerr << ReadAll(validation) << "\n";
        }

        return 14;
    }

    const std::string visibleText =
        ReadAll(visible);

    const std::string validationText =
        ReadAll(validation);

    const bool active =
        visibleText.find("status=active") !=
        std::string::npos;

    const bool splitMode =
        visibleText.find("split_ambient_contact") !=
        std::string::npos;

    const bool validated =
        validationText.find("status=validated") !=
        std::string::npos;

    const bool accessibleSeparator =
        CenterSeparatorIsAccessiblePattern(capture);

    const bool settingsMenuVisible =
        SettingsMenuLooksPresent(capture);

    const std::string settingsText =
        ReadAll(settingsFile);

    const std::string manualSettingsText =
        ReadAll(manualSettings);

    const bool manualSettingsComplete =
        manualSettingsText.find(
            "horizon_occlusion_strength=") !=
            std::string::npos &&
        manualSettingsText.find(
            "indirect_bounce_strength=") !=
            std::string::npos &&
        manualSettingsText.find(
            "local_glow_strength=") !=
            std::string::npos;

    const bool settingsPersisted =
        settingsText.find(
            "[Product]\nEnabled=0") !=
        std::string::npos;

    if (!active ||
        !splitMode ||
        !validated ||
        !settingsMenuVisible ||
        !settingsPersisted ||
        !manualSettingsComplete)
    {
        std::cerr
            << "visible_pass.txt:\n"
            << visibleText
            << "\ncamera_validation.txt:\n"
            << validationText
            << "\nchecks: active="
            << active
            << " split="
            << splitMode
            << " validated="
            << validated
            << " accessible_separator_behind_menu="
            << accessibleSeparator
            << " settings_menu_visible="
            << settingsMenuVisible
            << " settings_persisted="
            << settingsPersisted
            << " manual_settings_complete="
            << manualSettingsComplete
            << "\nSettings.ini:\n"
            << settingsText
            << "\n";

        return 15;
    }

    if (cameraBuffer) cameraBuffer->Release();
    if (depthDsv) depthDsv->Release();
    if (depth) depth->Release();
    if (sceneRtv) sceneRtv->Release();
    if (sceneColor) sceneColor->Release();
    if (backRtv) backRtv->Release();
    if (backbuffer) backbuffer->Release();
    if (context) context->Release();
    if (device) device->Release();
    if (swap) swap->Release();

    DestroyWindow(hwnd);
    FreeLibrary(gzfx);

    std::wcout
        << L"Visible pass harness passed.\n";

    return 0;
}
