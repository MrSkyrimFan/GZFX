#include "gzfx/paths.hpp"

#include <Windows.h>
#include <ShlObj.h>

#include <string>

namespace gzfx::paths
{
    namespace fs = std::filesystem;

    namespace
    {
        std::wstring EnvironmentValue(const wchar_t* name)
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
    }

    bool TestOverrideActive()
    {
        return !EnvironmentValue(L"GZFX_TEST_OUTPUT_ROOT").empty();
    }

    fs::path ExecutablePath()
    {
        std::wstring buffer(32768, L'\0');
        const DWORD written = GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));

        if (written == 0 || static_cast<std::size_t>(written) >= buffer.size())
        {
            return {};
        }

        buffer.resize(written);
        return fs::path(buffer);
    }

    fs::path RuntimeRoot()
    {
        const std::wstring testRoot = EnvironmentValue(L"GZFX_TEST_OUTPUT_ROOT");
        if (!testRoot.empty())
        {
            return fs::path(testRoot) / L"GZFX";
        }

        PWSTR documentsRaw = nullptr;
        const HRESULT result = SHGetKnownFolderPath(
            FOLDERID_Documents,
            KF_FLAG_DEFAULT,
            nullptr,
            &documentsRaw);

        if (FAILED(result) || documentsRaw == nullptr)
        {
            return {};
        }

        const fs::path documents(documentsRaw);
        CoTaskMemFree(documentsRaw);

        return documents / L"My Games" / L"Skyrim Special Edition" / L"GZFX";
    }

    fs::path DiagnosticsRoot()
    {
        const fs::path root = RuntimeRoot();
        return root.empty() ? fs::path{} : root / L"Diagnostics";
    }

    bool IsAllowedProcess(const fs::path& executable)
    {
        if (TestOverrideActive())
        {
            return true;
        }

        const std::wstring name = executable.filename().wstring();
        return _wcsicmp(name.c_str(), L"SkyrimSE.exe") == 0;
    }
}
