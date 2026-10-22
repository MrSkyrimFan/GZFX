#include "gzfx/runtime.hpp"
#include "gzfx/version.hpp"

#include <Windows.h>
#include <ShlObj.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <string>

namespace
{
    namespace fs = std::filesystem;

    std::once_flag g_initializeOnce;
    bool g_initializeResult = false;

    std::wstring GetEnvironmentValue(const wchar_t* name)
    {
        const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
        if (required == 0)
        {
            return {};
        }

        std::wstring value(required, L'\0');
        const DWORD written = GetEnvironmentVariableW(name, value.data(), required);
        if (written == 0 || written >= required)
        {
            return {};
        }

        value.resize(written);
        return value;
    }

    fs::path GetExecutablePath()
    {
        std::wstring buffer(32768, L'\0');
        const DWORD written = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0 || written >= buffer.size())
        {
            return {};
        }

        buffer.resize(written);
        return fs::path(buffer);
    }

    fs::path ResolveRuntimeRoot()
    {
        const std::wstring testRoot = GetEnvironmentValue(L"GZFX_TEST_OUTPUT_ROOT");
        if (!testRoot.empty())
        {
            return fs::path(testRoot) / L"GZFX";
        }

        PWSTR documentsRaw = nullptr;
        const HRESULT result = SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &documentsRaw);
        if (FAILED(result) || documentsRaw == nullptr)
        {
            return {};
        }

        const fs::path documents(documentsRaw);
        CoTaskMemFree(documentsRaw);

        return documents / L"My Games" / L"Skyrim Special Edition" / L"GZFX";
    }

    bool IsAllowedProcess(const fs::path& executable)
    {
        if (!GetEnvironmentValue(L"GZFX_TEST_OUTPUT_ROOT").empty())
        {
            return true;
        }

        const std::wstring name = executable.filename().wstring();
        return _wcsicmp(name.c_str(), L"SkyrimSE.exe") == 0;
    }

    std::string CurrentTimestamp()
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t time = std::chrono::system_clock::to_time_t(now);
        std::tm localTime{};
        localtime_s(&localTime, &time);

        std::ostringstream stream;
        stream << std::put_time(&localTime, "%Y-%m-%dT%H:%M:%S");
        return stream.str();
    }

    void InitializeRuntime()
    {
        const fs::path executable = GetExecutablePath();
        if (executable.empty() || !IsAllowedProcess(executable))
        {
            g_initializeResult = false;
            return;
        }

        const fs::path runtimeRoot = ResolveRuntimeRoot();
        if (runtimeRoot.empty())
        {
            g_initializeResult = false;
            return;
        }

        std::error_code error;
        fs::create_directories(runtimeRoot / L"Diagnostics", error);
        if (error)
        {
            g_initializeResult = false;
            return;
        }

        const fs::path logPath = runtimeRoot / L"GZFX.log";
        std::ofstream log(logPath, std::ios::out | std::ios::app);
        if (!log)
        {
            g_initializeResult = false;
            return;
        }

        log << "=== GZFX runtime initialization ===\n";
        log << "timestamp=" << CurrentTimestamp() << "\n";
        log << "version=" << gzfx::kVersion << "\n";
        log << "pid=" << GetCurrentProcessId() << "\n";
        log << "tid=" << GetCurrentThreadId() << "\n";
        log << "executable=" << executable.string() << "\n";
        log << "runtime_root=" << runtimeRoot.string() << "\n";
        log << "d3d11_loaded=" << (GetModuleHandleW(L"d3d11.dll") != nullptr ? "true" : "false") << "\n";
        log << "dxgi_loaded=" << (GetModuleHandleW(L"dxgi.dll") != nullptr ? "true" : "false") << "\n";
        log << "status=initialized\n";
        log << "===============================\n";
        log.flush();

        g_initializeResult = static_cast<bool>(log);
    }
}

extern "C" __declspec(dllexport) bool GZFX_Initialize() noexcept
{
    try
    {
        std::call_once(g_initializeOnce, InitializeRuntime);
        return g_initializeResult;
    }
    catch (...)
    {
        return false;
    }
}
