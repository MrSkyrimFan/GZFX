#include "gzfx/settings.hpp"

#include "gzfx/paths.hpp"

#include <Windows.h>

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <mutex>
#include <string>

namespace gzfx::settings
{
    namespace fs = std::filesystem;

    namespace
    {
        std::mutex g_mutex;
        Values g_values{};
        bool g_loaded = false;

        fs::path SettingsPath()
        {
            const fs::path root = gzfx::paths::RuntimeRoot();
            return root.empty() ? fs::path{} : root / L"Settings.ini";
        }

        void EnsureDefaults(const fs::path& path)
        {
            if (path.empty())
            {
                return;
            }

            std::error_code error;
            if (fs::exists(path, error) && !error)
            {
                return;
            }

            WritePrivateProfileStringW(
                L"AmbientContact",
                L"Strength",
                L"0.62",
                path.c_str());

            WritePrivateProfileStringW(
                L"AmbientContact",
                L"RadiusPixels",
                L"4.0",
                path.c_str());

            WritePrivateProfileStringW(
                L"Development",
                L"DefaultMode",
                L"1",
                path.c_str());

            WritePrivateProfileStringW(
                L"Development",
                L"ActivationDrawIndexedThreshold",
                L"1000",
                path.c_str());

            WritePrivateProfileStringW(
                L"Development",
                L"AccessibleDiagnostics",
                L"1",
                path.c_str());

            WritePrivateProfileStringW(
                L"Development",
                L"CaptureOnModeChange",
                L"1",
                path.c_str());
        }

        float ReadFloat(
            const fs::path& path,
            const wchar_t* section,
            const wchar_t* key,
            const wchar_t* fallback)
        {
            wchar_t value[64]{};

            GetPrivateProfileStringW(
                section,
                key,
                fallback,
                value,
                static_cast<DWORD>(std::size(value)),
                path.c_str());

            wchar_t* end = nullptr;
            const float parsed =
                std::wcstof(value, &end);

            if (end == value)
            {
                return std::wcstof(fallback, nullptr);
            }

            return parsed;
        }

        bool LoadUnlocked()
        {
            const fs::path path = SettingsPath();
            if (path.empty())
            {
                return false;
            }

            std::error_code error;
            fs::create_directories(
                path.parent_path(),
                error);

            if (error)
            {
                return false;
            }

            EnsureDefaults(path);

            Values next{};

            next.ambientContactStrength =
                std::clamp(
                    ReadFloat(
                        path,
                        L"AmbientContact",
                        L"Strength",
                        L"0.62"),
                    0.0f,
                    2.0f);

            next.ambientContactRadiusPixels =
                std::clamp(
                    ReadFloat(
                        path,
                        L"AmbientContact",
                        L"RadiusPixels",
                        L"4.0"),
                    1.0f,
                    24.0f);

            next.defaultMode =
                static_cast<std::uint32_t>(
                    std::clamp(
                        GetPrivateProfileIntW(
                            L"Development",
                            L"DefaultMode",
                            1,
                            path.c_str()),
                        0u,
                        4u));

            next.activationDrawIndexedThreshold =
                static_cast<std::uint64_t>(
                    std::max(
                        0,
                        GetPrivateProfileIntW(
                            L"Development",
                            L"ActivationDrawIndexedThreshold",
                            1000,
                            path.c_str())));

            next.accessibleDiagnostics =
                GetPrivateProfileIntW(
                    L"Development",
                    L"AccessibleDiagnostics",
                    1,
                    path.c_str()) != 0;

            next.captureOnModeChange =
                GetPrivateProfileIntW(
                    L"Development",
                    L"CaptureOnModeChange",
                    1,
                    path.c_str()) != 0;

            g_values = next;
            g_loaded = true;
            return true;
        }
    }

    const Values& Get() noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);
            if (!g_loaded)
            {
                LoadUnlocked();
            }

            return g_values;
        }
        catch (...)
        {
            return g_values;
        }
    }

    bool Reload() noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);
            return LoadUnlocked();
        }
        catch (...)
        {
            return false;
        }
    }
}
