#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

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

    std::wstring Quote(
        const fs::path& path)
    {
        return
            L"\"" +
            path.wstring() +
            L"\"";
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

    const fs::path child =
        argv[1];

    const fs::path outputRoot =
        argv[2];

    std::error_code error;

    fs::remove_all(
        outputRoot,
        error);

    fs::create_directories(
        outputRoot,
        error);

    if (error)
    {
        return 3;
    }

    std::wstring command =
        Quote(child) +
        L" " +
        Quote(outputRoot);

    STARTUPINFOW startup{};
    startup.cb =
        sizeof(startup);

    PROCESS_INFORMATION process{};

    if (!CreateProcessW(
            nullptr,
            command.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            child.parent_path().c_str(),
            &startup,
            &process))
    {
        return 4;
    }

    const DWORD wait =
        WaitForSingleObject(
            process.hProcess,
            30000);

    if (wait != WAIT_OBJECT_0)
    {
        TerminateProcess(
            process.hProcess,
            90);

        CloseHandle(
            process.hThread);

        CloseHandle(
            process.hProcess);

        return 5;
    }

    DWORD exitCode = 0;

    GetExitCodeProcess(
        process.hProcess,
        &exitCode);

    CloseHandle(
        process.hThread);

    CloseHandle(
        process.hProcess);

    if (exitCode != 0)
    {
        std::cerr
            << "Natural-exit child returned "
            << exitCode
            << ".\n";

        return 6;
    }

    const fs::path telemetry =
        outputRoot /
        L"GZFX" /
        L"Telemetry";

    if (!fs::exists(
            telemetry))
    {
        return 7;
    }

    bool passed = false;

    for (const auto& entry :
         fs::directory_iterator(
             telemetry))
    {
        if (!entry.is_directory())
        {
            continue;
        }

        const std::string closeState =
            ReadAll(
                entry.path() /
                L"session_close_state.txt");

        const std::string complete =
            ReadAll(
                entry.path() /
                L"telemetry_session_complete.txt");

        const std::string startup =
            ReadAll(
                entry.path() /
                L"renderer_startup.csv");

        if (closeState.find(
                "state=complete") !=
                    std::string::npos &&
            complete.find(
                "status=complete") !=
                    std::string::npos &&
            startup.find(
                "process_exit_intercepted") !=
                    std::string::npos)
        {
            passed = true;
            break;
        }
    }

    if (!passed)
    {
        std::cerr
            << "Natural process exit did not produce complete telemetry closure.\n";

        return 8;
    }

    std::wcout
        << L"Natural process-exit telemetry closure passed.\n";

    return 0;
}
