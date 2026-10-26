#include <Windows.h>

#include <filesystem>
#include <iostream>
#include <string>

using InitializeFn = bool (*)();

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3)
    {
        std::wcerr << L"Usage: GZFXSmokeHost <GZFX.dll> <output-root>\n";
        return 2;
    }

    const std::filesystem::path dllPath(argv[1]);
    const std::filesystem::path outputRoot(argv[2]);

    if (!SetEnvironmentVariableW(L"GZFX_TEST_OUTPUT_ROOT", outputRoot.c_str()))
    {
        std::wcerr << L"Failed to configure test output root.\n";
        return 3;
    }

    const HMODULE module = LoadLibraryW(dllPath.c_str());
    if (module == nullptr)
    {
        std::wcerr << L"LoadLibraryW failed with " << GetLastError() << L".\n";
        return 4;
    }

    const auto initialize = reinterpret_cast<InitializeFn>(
        GetProcAddress(module, "GZFX_Initialize"));
    if (initialize == nullptr)
    {
        std::wcerr << L"GZFX_Initialize export not found.\n";
        FreeLibrary(module);
        return 5;
    }

    if (!initialize() || !initialize())
    {
        std::wcerr << L"GZFX initialization failed or was not idempotent.\n";
        FreeLibrary(module);
        return 6;
    }

    const std::filesystem::path logPath = outputRoot / L"GZFX" / L"GZFX.log";
    if (!std::filesystem::exists(logPath))
    {
        std::wcerr << L"Expected log was not created: " << logPath << L"\n";
        FreeLibrary(module);
        return 7;
    }

    std::wcout << L"GZFX smoke test passed: " << logPath << L"\n";
    FreeLibrary(module);
    return 0;
}
