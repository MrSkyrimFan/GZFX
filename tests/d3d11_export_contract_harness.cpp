#include <Windows.h>

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

    const HMODULE module =
        LoadLibraryW(
            argv[1]);

    if (module == nullptr)
    {
        return 3;
    }

    const bool publicSurface =
        GetProcAddress(
            module,
            "D3D11CreateDevice") != nullptr &&
        GetProcAddress(
            module,
            "D3D11CreateDeviceAndSwapChain") != nullptr &&
        GetProcAddress(
            module,
            "D3D11On12CreateDevice") != nullptr;

    const bool privateCoreAbsent =
        GetProcAddress(
            module,
            "D3D11CoreCreateDevice") == nullptr &&
        GetProcAddress(
            module,
            "D3D11CoreCreateLayeredDevice") == nullptr &&
        GetProcAddress(
            module,
            "D3D11CoreGetLayeredDeviceSize") == nullptr &&
        GetProcAddress(
            module,
            "D3D11CoreRegisterLayers") == nullptr;

    FreeLibrary(
        module);

    if (!publicSurface ||
        !privateCoreAbsent)
    {
        return 4;
    }

    std::wcout
        << L"D3D11 public export contract passed.\n";

    return 0;
}
