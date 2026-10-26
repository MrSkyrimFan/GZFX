#include <Windows.h>
#include <d3d11.h>

#include <filesystem>
#include <iostream>

int wmain(
    int argc,
    wchar_t** argv)
{
    if (argc != 2)
    {
        return 2;
    }

    const std::filesystem::path outputRoot =
        argv[1];

    std::error_code error;
    std::filesystem::remove_all(
        outputRoot,
        error);

    if (!SetEnvironmentVariableW(
            L"GZFX_TEST_OUTPUT_ROOT",
            outputRoot.c_str()))
    {
        return 3;
    }

    SetEnvironmentVariableW(
        L"GZFX_TEST_ALLOW_SOFTWARE_PRIMARY",
        nullptr);

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL level{};

    const HRESULT result =
        D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            0,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &device,
            &level,
            &context);

    if (FAILED(result) ||
        device == nullptr ||
        context == nullptr)
    {
        return 4;
    }

    const bool gzfxLoaded =
        GetModuleHandleW(
            L"GZFX.dll") != nullptr;

    context->Release();
    device->Release();

    if (gzfxLoaded)
    {
        std::cerr
            << "Rejected WARP device incorrectly loaded GZFX.\n";

        return 5;
    }

    std::wcout
        << L"Software primary rejection passed.\n";

    return 0;
}
