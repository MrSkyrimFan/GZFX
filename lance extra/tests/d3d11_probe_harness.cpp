#include <Windows.h>
#include <d3d11.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using InitializeFn = bool (*)();

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3)
    {
        std::wcerr << L"Usage: GZFXD3D11ProbeHarness <GZFX.dll> <output-root>\n";
        return 2;
    }

    const std::filesystem::path dllPath(argv[1]);
    const std::filesystem::path outputRoot(argv[2]);

    std::error_code error;
    std::filesystem::remove_all(outputRoot, error);
    error.clear();
    std::filesystem::create_directories(outputRoot, error);
    if (error)
    {
        std::wcerr << L"Failed to prepare output directory.\n";
        return 3;
    }

    if (!SetEnvironmentVariableW(L"GZFX_TEST_OUTPUT_ROOT", outputRoot.c_str()))
    {
        std::wcerr << L"Failed to configure test output root.\n";
        return 4;
    }

    const HMODULE module = LoadLibraryW(dllPath.c_str());
    if (module == nullptr)
    {
        std::wcerr << L"LoadLibraryW failed with " << GetLastError() << L".\n";
        return 5;
    }

    const auto initialize = reinterpret_cast<InitializeFn>(
        GetProcAddress(module, "GZFX_Initialize"));

    if (initialize == nullptr || !initialize())
    {
        std::wcerr << L"GZFX initialization failed.\n";
        FreeLibrary(module);
        return 6;
    }

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    D3D_FEATURE_LEVEL featureLevel{};

    const HRESULT result = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_WARP,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &device,
        &featureLevel,
        &context);

    if (FAILED(result))
    {
        std::wcerr << L"D3D11 WARP device creation failed.\n";
        FreeLibrary(module);
        return 7;
    }

    if (context != nullptr)
    {
        context->Release();
    }

    if (device != nullptr)
    {
        device->Release();
    }

    const std::filesystem::path logPath =
        outputRoot / L"GZFX" / L"Diagnostics" / L"d3d11_lifecycle.txt";

    std::ifstream log(logPath);
    if (!log)
    {
        std::wcerr << L"Lifecycle diagnostic was not created.\n";
        FreeLibrary(module);
        return 8;
    }

    std::ostringstream contents;
    contents << log.rdbuf();

    if (contents.str().find("event=D3D11CreateDevice") == std::string::npos)
    {
        std::wcerr << L"D3D11CreateDevice was not observed by the lifecycle probe.\n";
        FreeLibrary(module);
        return 9;
    }

    std::wcout << L"D3D11 lifecycle probe passed: " << logPath << L"\n";
    FreeLibrary(module);
    return 0;
}
