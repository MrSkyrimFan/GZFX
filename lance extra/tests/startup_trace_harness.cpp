#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using RecordFn = void(*)(
    const char*,
    std::uint64_t,
    std::uint32_t,
    std::int32_t,
    std::uintptr_t,
    std::uintptr_t) noexcept;

namespace
{
    namespace fs = std::filesystem;

    std::string ReadAll(
        const fs::path& path)
    {
        std::ifstream input(
            path,
            std::ios::binary);

        return
            std::string(
                std::istreambuf_iterator<char>(input),
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

    const fs::path dllPath =
        argv[1];

    const fs::path outputRoot =
        argv[2];

    std::error_code error;

    fs::remove_all(
        outputRoot,
        error);

    const fs::path diagnostics =
        outputRoot /
        L"GZFX" /
        L"Diagnostics";

    fs::create_directories(
        diagnostics,
        error);

    if (error ||
        !SetEnvironmentVariableW(
            L"GZFX_TEST_OUTPUT_ROOT",
            outputRoot.c_str()))
    {
        return 3;
    }

    const fs::path trace =
        diagnostics /
        L"renderer_startup.csv";

    {
        std::ofstream stale(
            trace,
            std::ios::out |
                std::ios::trunc);

        stale
            << "STALE_STARTUP_MARKER\n";
    }

    const HMODULE module =
        LoadLibraryW(
            dllPath.c_str());

    if (module == nullptr)
    {
        return 4;
    }

    const auto record =
        reinterpret_cast<RecordFn>(
            GetProcAddress(
                module,
                "GZFX_RecordBootstrapEvent"));

    if (record == nullptr)
    {
        FreeLibrary(module);
        return 5;
    }

    record(
        "trace_event_a",
        10,
        GetCurrentThreadId(),
        0,
        1,
        2);

    record(
        "trace_event_b",
        20,
        GetCurrentThreadId(),
        0,
        3,
        4);

    const std::string content =
        ReadAll(
            trace);

    FreeLibrary(module);

    const std::string pid =
        std::to_string(
            GetCurrentProcessId());

    if (content.find(
            "STALE_STARTUP_MARKER") !=
                std::string::npos ||
        content.find(
            "pid,sequence,qpc,thread,event") ==
                std::string::npos ||
        content.find(
            pid + ",1,10,") ==
                std::string::npos ||
        content.find(
            pid + ",2,20,") ==
                std::string::npos)
    {
        std::cerr
            << content
            << "\n";

        return 6;
    }

    std::wcout
        << L"Process-scoped startup trace passed.\n";

    return 0;
}
