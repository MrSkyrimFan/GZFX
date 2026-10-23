#include "gzfx/settings.hpp"

#include "gzfx/paths.hpp"

#include <Windows.h>

#include <algorithm>
#include <cwchar>
#include <cmath>
#include <filesystem>
#include <iterator>
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

        bool WriteString(
            const fs::path& path,
            const wchar_t* section,
            const wchar_t* key,
            const std::wstring& value)
        {
            return WritePrivateProfileStringW(
                section,
                key,
                value.c_str(),
                path.c_str()) != FALSE;
        }

        bool PersistUnlocked()
        {
            const fs::path path = SettingsPath();
            if (path.empty())
            {
                return false;
            }

            wchar_t floatBuffer[64]{};

            swprintf_s(
                floatBuffer,
                L"%.3f",
                static_cast<double>(
                    g_values.ambientContactStrength));

            if (!WriteString(
                    path,
                    L"AmbientContact",
                    L"Strength",
                    floatBuffer))
            {
                return false;
            }

            swprintf_s(
                floatBuffer,
                L"%.2f",
                static_cast<double>(
                    g_values.ambientContactRadiusPixels));

            if (!WriteString(
                    path,
                    L"AmbientContact",
                    L"RadiusPixels",
                    floatBuffer))
            {
                return false;
            }

            if (!WriteString(
                    path,
                    L"Development",
                    L"DefaultMode",
                    std::to_wstring(g_values.defaultMode)))
            {
                return false;
            }

            if (!WriteString(
                    path,
                    L"Development",
                    L"ActivationDrawIndexedThreshold",
                    std::to_wstring(
                        g_values.activationDrawIndexedThreshold)))
            {
                return false;
            }

            if (!WriteString(
                    path,
                    L"Development",
                    L"AccessibleDiagnostics",
                    g_values.accessibleDiagnostics
                        ? L"1"
                        : L"0"))
            {
                return false;
            }

            if (!WriteString(
                    path,
                    L"Development",
                    L"CaptureOnModeChange",
                    g_values.captureOnModeChange
                        ? L"1"
                        : L"0"))
            {
                return false;
            }

            WritePrivateProfileStringW(
                nullptr,
                nullptr,
                nullptr,
                path.c_str());

            return true;
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
                    GetPrivateProfileIntW(
                        L"Development",
                        L"ActivationDrawIndexedThreshold",
                        1000,
                        path.c_str()));

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

    bool Adjust(
        const Editable setting,
        const int direction) noexcept
    {
        try
        {
            if (direction == 0)
            {
                return true;
            }

            std::scoped_lock lock(g_mutex);

            if (!g_loaded &&
                !LoadUnlocked())
            {
                return false;
            }

            const float sign =
                direction > 0 ? 1.0f : -1.0f;

            switch (setting)
            {
            case Editable::AmbientContactStrength:
                g_values.ambientContactStrength =
                    std::clamp(
                        g_values.ambientContactStrength +
                            sign * 0.05f,
                        0.0f,
                        2.0f);
                break;

            case Editable::AmbientContactRadiusPixels:
                g_values.ambientContactRadiusPixels =
                    std::clamp(
                        g_values.ambientContactRadiusPixels +
                            sign * 0.5f,
                        1.0f,
                        24.0f);
                break;

            case Editable::DefaultMode:
            {
                int mode =
                    static_cast<int>(g_values.defaultMode) +
                    (direction > 0 ? 1 : -1);

                if (mode < 0)
                {
                    mode = 4;
                }

                if (mode > 4)
                {
                    mode = 0;
                }

                g_values.defaultMode =
                    static_cast<std::uint32_t>(mode);
                break;
            }

            case Editable::ActivationDrawIndexedThreshold:
            {
                constexpr std::uint64_t step = 250;
                const std::uint64_t current =
                    g_values.activationDrawIndexedThreshold;

                if (direction > 0)
                {
                    g_values.activationDrawIndexedThreshold =
                        std::min<std::uint64_t>(
                            20000,
                            current + step);
                }
                else
                {
                    g_values.activationDrawIndexedThreshold =
                        current > step
                            ? current - step
                            : 0;
                }
                break;
            }

            case Editable::AccessibleDiagnostics:
                g_values.accessibleDiagnostics =
                    direction > 0;
                break;

            case Editable::CaptureOnModeChange:
                g_values.captureOnModeChange =
                    direction > 0;
                break;

            case Editable::Count:
                return false;
            }

            return PersistUnlocked();
        }
        catch (...)
        {
            return false;
        }
    }

    bool SetNormalized(
        const Editable setting,
        const float normalized) noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);

            if (!g_loaded &&
                !LoadUnlocked())
            {
                return false;
            }

            const float value =
                std::clamp(normalized, 0.0f, 1.0f);

            switch (setting)
            {
            case Editable::AmbientContactStrength:
                g_values.ambientContactStrength =
                    value * 2.0f;
                break;

            case Editable::AmbientContactRadiusPixels:
                g_values.ambientContactRadiusPixels =
                    1.0f + value * 23.0f;
                break;

            case Editable::DefaultMode:
                g_values.defaultMode =
                    static_cast<std::uint32_t>(
                        std::clamp(
                            static_cast<int>(
                                std::lround(value * 4.0f)),
                            0,
                            4));
                break;

            case Editable::ActivationDrawIndexedThreshold:
                g_values.activationDrawIndexedThreshold =
                    static_cast<std::uint64_t>(
                        std::llround(
                            static_cast<double>(value) *
                            20000.0));
                break;

            case Editable::AccessibleDiagnostics:
            case Editable::CaptureOnModeChange:
            case Editable::Count:
                return false;
            }

            return PersistUnlocked();
        }
        catch (...)
        {
            return false;
        }
    }

    float GetNormalized(
        const Editable setting) noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);

            if (!g_loaded &&
                !LoadUnlocked())
            {
                return 0.0f;
            }

            switch (setting)
            {
            case Editable::AmbientContactStrength:
                return std::clamp(
                    g_values.ambientContactStrength / 2.0f,
                    0.0f,
                    1.0f);

            case Editable::AmbientContactRadiusPixels:
                return std::clamp(
                    (g_values.ambientContactRadiusPixels - 1.0f) /
                        23.0f,
                    0.0f,
                    1.0f);

            case Editable::DefaultMode:
                return std::clamp(
                    static_cast<float>(g_values.defaultMode) /
                        4.0f,
                    0.0f,
                    1.0f);

            case Editable::ActivationDrawIndexedThreshold:
                return std::clamp(
                    static_cast<float>(
                        g_values.activationDrawIndexedThreshold) /
                        20000.0f,
                    0.0f,
                    1.0f);

            case Editable::AccessibleDiagnostics:
                return g_values.accessibleDiagnostics ? 1.0f : 0.0f;

            case Editable::CaptureOnModeChange:
                return g_values.captureOnModeChange ? 1.0f : 0.0f;

            case Editable::Count:
                return 0.0f;
            }

            return 0.0f;
        }
        catch (...)
        {
            return 0.0f;
        }
    }

    bool Toggle(
        const Editable setting) noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);

            if (!g_loaded &&
                !LoadUnlocked())
            {
                return false;
            }

            switch (setting)
            {
            case Editable::AccessibleDiagnostics:
                g_values.accessibleDiagnostics =
                    !g_values.accessibleDiagnostics;
                break;

            case Editable::CaptureOnModeChange:
                g_values.captureOnModeChange =
                    !g_values.captureOnModeChange;
                break;

            default:
                return false;
            }

            return PersistUnlocked();
        }
        catch (...)
        {
            return false;
        }
    }
}
