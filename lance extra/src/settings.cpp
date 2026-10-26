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

        void EnsureValue(
            const fs::path& path,
            const wchar_t* section,
            const wchar_t* key,
            const wchar_t* fallback)
        {
            wchar_t value[8]{};

            const DWORD read =
                GetPrivateProfileStringW(
                    section,
                    key,
                    L"",
                    value,
                    static_cast<DWORD>(
                        std::size(value)),
                    path.c_str());

            if (read == 0)
            {
                WritePrivateProfileStringW(
                    section,
                    key,
                    fallback,
                    path.c_str());
            }
        }

        void EnsureDefaults(
            const fs::path& path)
        {
            if (path.empty())
            {
                return;
            }

            EnsureValue(path, L"AmbientContact", L"Strength", L"0.62");
            EnsureValue(path, L"AmbientContact", L"RadiusPixels", L"4.0");

            EnsureValue(path, L"HorizonOcclusion", L"Enabled", L"1");
            EnsureValue(path, L"HorizonOcclusion", L"Strength", L"0.25");
            EnsureValue(path, L"HorizonOcclusion", L"RadiusPixels", L"28.0");
            EnsureValue(path, L"HorizonOcclusion", L"Quality", L"2");

            EnsureValue(path, L"IndirectBounce", L"Enabled", L"1");
            EnsureValue(path, L"IndirectBounce", L"Strength", L"0.12");
            EnsureValue(path, L"IndirectBounce", L"RadiusPixels", L"36.0");
            EnsureValue(path, L"IndirectBounce", L"Saturation", L"1.00");

            EnsureValue(path, L"LocalGlow", L"Enabled", L"1");
            EnsureValue(path, L"LocalGlow", L"Strength", L"0.18");
            EnsureValue(path, L"LocalGlow", L"Threshold", L"1.10");
            EnsureValue(path, L"LocalGlow", L"RadiusPixels", L"8.0");

            EnsureValue(path, L"Development", L"DefaultMode", L"1");
            EnsureValue(path, L"Development", L"ActivationDrawIndexedThreshold", L"1000");
            EnsureValue(path, L"Development", L"AccessibleDiagnostics", L"1");
            EnsureValue(path, L"Development", L"CaptureOnModeChange", L"1");
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
                    L"HorizonOcclusion",
                    L"Enabled",
                    g_values.horizonOcclusionEnabled
                        ? L"1"
                        : L"0"))
            {
                return false;
            }

            swprintf_s(
                floatBuffer,
                L"%.3f",
                static_cast<double>(
                    g_values.horizonOcclusionStrength));

            if (!WriteString(
                    path,
                    L"HorizonOcclusion",
                    L"Strength",
                    floatBuffer))
            {
                return false;
            }

            swprintf_s(
                floatBuffer,
                L"%.2f",
                static_cast<double>(
                    g_values.horizonOcclusionRadiusPixels));

            if (!WriteString(
                    path,
                    L"HorizonOcclusion",
                    L"RadiusPixels",
                    floatBuffer))
            {
                return false;
            }

            if (!WriteString(
                    path,
                    L"HorizonOcclusion",
                    L"Quality",
                    std::to_wstring(
                        g_values.horizonOcclusionQuality)))
            {
                return false;
            }

            if (!WriteString(
                    path,
                    L"IndirectBounce",
                    L"Enabled",
                    g_values.indirectBounceEnabled
                        ? L"1"
                        : L"0"))
            {
                return false;
            }

            swprintf_s(
                floatBuffer,
                L"%.3f",
                static_cast<double>(
                    g_values.indirectBounceStrength));

            if (!WriteString(
                    path,
                    L"IndirectBounce",
                    L"Strength",
                    floatBuffer))
            {
                return false;
            }

            swprintf_s(
                floatBuffer,
                L"%.2f",
                static_cast<double>(
                    g_values.indirectBounceRadiusPixels));

            if (!WriteString(
                    path,
                    L"IndirectBounce",
                    L"RadiusPixels",
                    floatBuffer))
            {
                return false;
            }

            swprintf_s(
                floatBuffer,
                L"%.3f",
                static_cast<double>(
                    g_values.indirectBounceSaturation));

            if (!WriteString(
                    path,
                    L"IndirectBounce",
                    L"Saturation",
                    floatBuffer))
            {
                return false;
            }

            if (!WriteString(
                    path,
                    L"LocalGlow",
                    L"Enabled",
                    g_values.localGlowEnabled
                        ? L"1"
                        : L"0"))
            {
                return false;
            }

            swprintf_s(
                floatBuffer,
                L"%.3f",
                static_cast<double>(
                    g_values.localGlowStrength));

            if (!WriteString(
                    path,
                    L"LocalGlow",
                    L"Strength",
                    floatBuffer))
            {
                return false;
            }

            swprintf_s(
                floatBuffer,
                L"%.3f",
                static_cast<double>(
                    g_values.localGlowThreshold));

            if (!WriteString(
                    path,
                    L"LocalGlow",
                    L"Threshold",
                    floatBuffer))
            {
                return false;
            }

            swprintf_s(
                floatBuffer,
                L"%.2f",
                static_cast<double>(
                    g_values.localGlowRadiusPixels));

            if (!WriteString(
                    path,
                    L"LocalGlow",
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

            next.horizonOcclusionEnabled =
                GetPrivateProfileIntW(
                    L"HorizonOcclusion",
                    L"Enabled",
                    1,
                    path.c_str()) != 0;

            next.horizonOcclusionStrength =
                std::clamp(
                    ReadFloat(
                        path,
                        L"HorizonOcclusion",
                        L"Strength",
                        L"0.25"),
                    0.0f,
                    1.0f);

            next.horizonOcclusionRadiusPixels =
                std::clamp(
                    ReadFloat(
                        path,
                        L"HorizonOcclusion",
                        L"RadiusPixels",
                        L"28.0"),
                    4.0f,
                    96.0f);

            next.horizonOcclusionQuality =
                static_cast<std::uint32_t>(
                    std::clamp(
                        GetPrivateProfileIntW(
                            L"HorizonOcclusion",
                            L"Quality",
                            2,
                            path.c_str()),
                        0u,
                        3u));

            next.indirectBounceEnabled =
                GetPrivateProfileIntW(
                    L"IndirectBounce",
                    L"Enabled",
                    1,
                    path.c_str()) != 0;

            next.indirectBounceStrength =
                std::clamp(
                    ReadFloat(
                        path,
                        L"IndirectBounce",
                        L"Strength",
                        L"0.12"),
                    0.0f,
                    0.60f);

            next.indirectBounceRadiusPixels =
                std::clamp(
                    ReadFloat(
                        path,
                        L"IndirectBounce",
                        L"RadiusPixels",
                        L"36.0"),
                    4.0f,
                    128.0f);

            next.indirectBounceSaturation =
                std::clamp(
                    ReadFloat(
                        path,
                        L"IndirectBounce",
                        L"Saturation",
                        L"1.00"),
                    0.0f,
                    1.50f);

            next.localGlowEnabled =
                GetPrivateProfileIntW(
                    L"LocalGlow",
                    L"Enabled",
                    1,
                    path.c_str()) != 0;

            next.localGlowStrength =
                std::clamp(
                    ReadFloat(
                        path,
                        L"LocalGlow",
                        L"Strength",
                        L"0.18"),
                    0.0f,
                    1.5f);

            next.localGlowThreshold =
                std::clamp(
                    ReadFloat(
                        path,
                        L"LocalGlow",
                        L"Threshold",
                        L"1.10"),
                    0.25f,
                    4.0f);

            next.localGlowRadiusPixels =
                std::clamp(
                    ReadFloat(
                        path,
                        L"LocalGlow",
                        L"RadiusPixels",
                        L"8.0"),
                    1.0f,
                    32.0f);

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
                            sign * 0.025f,
                        0.0f,
                        1.0f);
                break;

            case Editable::AmbientContactRadiusPixels:
                g_values.ambientContactRadiusPixels =
                    std::clamp(
                        g_values.ambientContactRadiusPixels +
                            sign * 0.5f,
                        1.0f,
                        24.0f);
                break;

            case Editable::HorizonOcclusionEnabled:
                g_values.horizonOcclusionEnabled =
                    direction > 0;
                break;

            case Editable::HorizonOcclusionStrength:
                g_values.horizonOcclusionStrength =
                    std::clamp(
                        g_values.horizonOcclusionStrength +
                            sign * 0.05f,
                        0.0f,
                        2.0f);
                break;

            case Editable::HorizonOcclusionRadiusPixels:
                g_values.horizonOcclusionRadiusPixels =
                    std::clamp(
                        g_values.horizonOcclusionRadiusPixels +
                            sign * 2.0f,
                        4.0f,
                        96.0f);
                break;

            case Editable::HorizonOcclusionQuality:
            {
                const int quality =
                    static_cast<int>(
                        g_values.horizonOcclusionQuality) +
                    (direction > 0 ? 1 : -1);

                g_values.horizonOcclusionQuality =
                    static_cast<std::uint32_t>(
                        std::clamp(
                            quality,
                            0,
                            3));
                break;
            }

            case Editable::IndirectBounceEnabled:
                g_values.indirectBounceEnabled =
                    direction > 0;
                break;

            case Editable::IndirectBounceStrength:
                g_values.indirectBounceStrength =
                    std::clamp(
                        g_values.indirectBounceStrength +
                            sign * 0.025f,
                        0.0f,
                        0.60f);
                break;

            case Editable::IndirectBounceRadiusPixels:
                g_values.indirectBounceRadiusPixels =
                    std::clamp(
                        g_values.indirectBounceRadiusPixels +
                            sign * 2.0f,
                        4.0f,
                        128.0f);
                break;

            case Editable::IndirectBounceSaturation:
                g_values.indirectBounceSaturation =
                    std::clamp(
                        g_values.indirectBounceSaturation +
                            sign * 0.05f,
                        0.0f,
                        1.50f);
                break;

            case Editable::LocalGlowEnabled:
                g_values.localGlowEnabled =
                    direction > 0;
                break;

            case Editable::LocalGlowStrength:
                g_values.localGlowStrength =
                    std::clamp(
                        g_values.localGlowStrength +
                            sign * 0.05f,
                        0.0f,
                        1.5f);
                break;

            case Editable::LocalGlowThreshold:
                g_values.localGlowThreshold =
                    std::clamp(
                        g_values.localGlowThreshold +
                            sign * 0.05f,
                        0.25f,
                        4.0f);
                break;

            case Editable::LocalGlowRadiusPixels:
                g_values.localGlowRadiusPixels =
                    std::clamp(
                        g_values.localGlowRadiusPixels +
                            sign * 0.5f,
                        1.0f,
                        32.0f);
                break;

            case Editable::DefaultMode:
            {
                int mode =
                    static_cast<int>(
                        g_values.defaultMode) +
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
                    static_cast<std::uint32_t>(
                        mode);
                break;
            }

            case Editable::ActivationDrawIndexedThreshold:
            {
                constexpr std::uint64_t step = 250;
                const std::uint64_t current =
                    g_values.activationDrawIndexedThreshold;

                g_values.activationDrawIndexedThreshold =
                    direction > 0
                    ? std::min<std::uint64_t>(
                        20000,
                        current + step)
                    : (current > step
                        ? current - step
                        : 0);
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
                std::clamp(
                    normalized,
                    0.0f,
                    1.0f);

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

            case Editable::HorizonOcclusionStrength:
                g_values.horizonOcclusionStrength =
                    value;
                break;

            case Editable::HorizonOcclusionRadiusPixels:
                g_values.horizonOcclusionRadiusPixels =
                    4.0f *
                    std::pow(
                        24.0f,
                        value);
                break;

            case Editable::HorizonOcclusionQuality:
                g_values.horizonOcclusionQuality =
                    static_cast<std::uint32_t>(
                        std::clamp(
                            static_cast<int>(
                                std::lround(
                                    value * 3.0f)),
                            0,
                            3));
                break;

            case Editable::IndirectBounceStrength:
                g_values.indirectBounceStrength =
                    value * 0.60f;
                break;

            case Editable::IndirectBounceRadiusPixels:
                g_values.indirectBounceRadiusPixels =
                    4.0f *
                    std::pow(
                        32.0f,
                        value);
                break;

            case Editable::IndirectBounceSaturation:
                g_values.indirectBounceSaturation =
                    value * 1.50f;
                break;

            case Editable::LocalGlowStrength:
                g_values.localGlowStrength =
                    value * 1.5f;
                break;

            case Editable::LocalGlowThreshold:
                g_values.localGlowThreshold =
                    0.25f + value * 3.75f;
                break;

            case Editable::LocalGlowRadiusPixels:
                g_values.localGlowRadiusPixels =
                    1.0f + value * 31.0f;
                break;

            case Editable::DefaultMode:
                g_values.defaultMode =
                    static_cast<std::uint32_t>(
                        std::clamp(
                            static_cast<int>(
                                std::lround(
                                    value * 4.0f)),
                            0,
                            4));
                break;

            case Editable::ActivationDrawIndexedThreshold:
                g_values.activationDrawIndexedThreshold =
                    static_cast<std::uint64_t>(
                        std::llround(
                            static_cast<double>(
                                value) *
                            20000.0));
                break;

            case Editable::HorizonOcclusionEnabled:
            case Editable::IndirectBounceEnabled:
            case Editable::LocalGlowEnabled:
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

            case Editable::HorizonOcclusionEnabled:
                return g_values.horizonOcclusionEnabled
                    ? 1.0f
                    : 0.0f;

            case Editable::HorizonOcclusionStrength:
                return std::clamp(
                    g_values.horizonOcclusionStrength,
                    0.0f,
                    1.0f);

            case Editable::HorizonOcclusionRadiusPixels:
                return std::clamp(
                    std::log(
                        std::max(
                            g_values.horizonOcclusionRadiusPixels,
                            4.0f) /
                        4.0f) /
                    std::log(24.0f),
                    0.0f,
                    1.0f);

            case Editable::HorizonOcclusionQuality:
                return std::clamp(
                    static_cast<float>(
                        g_values.horizonOcclusionQuality) /
                        3.0f,
                    0.0f,
                    1.0f);

            case Editable::IndirectBounceEnabled:
                return g_values.indirectBounceEnabled
                    ? 1.0f
                    : 0.0f;

            case Editable::IndirectBounceStrength:
                return std::clamp(
                    g_values.indirectBounceStrength / 0.60f,
                    0.0f,
                    1.0f);

            case Editable::IndirectBounceRadiusPixels:
                return std::clamp(
                    std::log(
                        std::max(
                            g_values.indirectBounceRadiusPixels,
                            4.0f) /
                        4.0f) /
                    std::log(32.0f),
                    0.0f,
                    1.0f);

            case Editable::IndirectBounceSaturation:
                return std::clamp(
                    g_values.indirectBounceSaturation / 1.50f,
                    0.0f,
                    1.0f);

            case Editable::LocalGlowEnabled:
                return g_values.localGlowEnabled
                    ? 1.0f
                    : 0.0f;

            case Editable::LocalGlowStrength:
                return std::clamp(
                    g_values.localGlowStrength / 1.5f,
                    0.0f,
                    1.0f);

            case Editable::LocalGlowThreshold:
                return std::clamp(
                    (g_values.localGlowThreshold - 0.25f) /
                        3.75f,
                    0.0f,
                    1.0f);

            case Editable::LocalGlowRadiusPixels:
                return std::clamp(
                    (g_values.localGlowRadiusPixels - 1.0f) /
                        31.0f,
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
            case Editable::HorizonOcclusionEnabled:
                g_values.horizonOcclusionEnabled =
                    !g_values.horizonOcclusionEnabled;
                break;

            case Editable::IndirectBounceEnabled:
                g_values.indirectBounceEnabled =
                    !g_values.indirectBounceEnabled;
                break;

            case Editable::LocalGlowEnabled:
                g_values.localGlowEnabled =
                    !g_values.localGlowEnabled;
                break;

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
