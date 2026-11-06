#include "gzfx/settings.hpp"

#include "gzfx/paths.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <limits>
#include <mutex>
#include <string>
#include <type_traits>

namespace gzfx::settings
{
    namespace fs = std::filesystem;

    namespace
    {
        enum class ValueType : std::uint8_t
        {
            Boolean,
            Float,
            UInt32,
            UInt64,
            Action
        };

        enum class Mapping : std::uint8_t
        {
            Linear,
            Logarithmic
        };

        struct Descriptor
        {
            Editable editable = Editable::Count;
            const wchar_t* section = nullptr;
            const wchar_t* key = nullptr;
            ValueType type = ValueType::Action;
            Mapping mapping = Mapping::Linear;
            std::size_t offset = 0;
            double minimum = 0.0;
            double maximum = 1.0;
            double step = 1.0;
            bool profile = false;
        };

#define GZFX_BOOL(id, field, sectionName, keyName, profileValue) \
    Descriptor{Editable::id, L##sectionName, L##keyName, ValueType::Boolean, \
        Mapping::Linear, offsetof(Values, field), 0.0, 1.0, 1.0, profileValue}

#define GZFX_FLOAT(id, field, sectionName, keyName, minValue, maxValue, stepValue, mapValue, profileValue) \
    Descriptor{Editable::id, L##sectionName, L##keyName, ValueType::Float, \
        Mapping::mapValue, offsetof(Values, field), minValue, maxValue, stepValue, profileValue}

#define GZFX_U32(id, field, sectionName, keyName, minValue, maxValue, stepValue, profileValue) \
    Descriptor{Editable::id, L##sectionName, L##keyName, ValueType::UInt32, \
        Mapping::Linear, offsetof(Values, field), minValue, maxValue, stepValue, profileValue}

#define GZFX_U64(id, field, sectionName, keyName, minValue, maxValue, stepValue, profileValue) \
    Descriptor{Editable::id, L##sectionName, L##keyName, ValueType::UInt64, \
        Mapping::Linear, offsetof(Values, field), minValue, maxValue, stepValue, profileValue}

#define GZFX_ACTION(id) \
    Descriptor{Editable::id, nullptr, nullptr, ValueType::Action, \
        Mapping::Linear, 0, 0.0, 0.0, 0.0, false}

        constexpr std::array<
            Descriptor,
            static_cast<std::size_t>(Editable::Count)>
            kDescriptors
        {{
            GZFX_BOOL(ProductEnabled, productEnabled, "Product", "Enabled", true),
            GZFX_U32(ProductQuality, productQuality, "Product", "Quality", 0, 1, 1, true),
            GZFX_U32(ProductPreset, productPreset, "Product", "Preset", 0, 5, 1, false),
            GZFX_U32(ProductCustomSlot, productCustomSlot, "Product", "CustomSlot", 1, 3, 1, false),
            GZFX_ACTION(ApplyPresetAction),
            GZFX_ACTION(LoadCustomAction),
            GZFX_ACTION(SaveCustomAction),

            GZFX_FLOAT(AmbientContactStrength, ambientContactStrength, "AmbientContact", "Strength", 0.0, 2.0, 0.05, Linear, true),
            GZFX_FLOAT(AmbientContactRadiusPixels, ambientContactRadiusPixels, "AmbientContact", "RadiusPixels", 1.0, 24.0, 0.5, Logarithmic, true),
            GZFX_BOOL(HorizonOcclusionEnabled, horizonOcclusionEnabled, "HorizonOcclusion", "Enabled", true),
            GZFX_FLOAT(HorizonOcclusionStrength, horizonOcclusionStrength, "HorizonOcclusion", "Strength", 0.0, 2.0, 0.05, Linear, true),
            GZFX_FLOAT(HorizonOcclusionRadiusPixels, horizonOcclusionRadiusPixels, "HorizonOcclusion", "RadiusPixels", 4.0, 96.0, 2.0, Logarithmic, true),
            GZFX_U32(HorizonOcclusionQuality, horizonOcclusionQuality, "HorizonOcclusion", "Quality", 0, 3, 1, true),
            GZFX_BOOL(IndirectBounceEnabled, indirectBounceEnabled, "IndirectBounce", "Enabled", true),
            GZFX_FLOAT(IndirectBounceStrength, indirectBounceStrength, "IndirectBounce", "Strength", 0.0, 0.60, 0.025, Linear, true),
            GZFX_FLOAT(IndirectBounceRadiusPixels, indirectBounceRadiusPixels, "IndirectBounce", "RadiusPixels", 4.0, 128.0, 2.0, Logarithmic, true),
            GZFX_FLOAT(IndirectBounceSaturation, indirectBounceSaturation, "IndirectBounce", "Saturation", 0.0, 1.5, 0.05, Linear, true),
            GZFX_BOOL(LocalGlowEnabled, localGlowEnabled, "LocalGlow", "Enabled", true),
            GZFX_FLOAT(LocalGlowStrength, localGlowStrength, "LocalGlow", "Strength", 0.0, 1.5, 0.05, Linear, true),
            GZFX_FLOAT(LocalGlowThreshold, localGlowThreshold, "LocalGlow", "Threshold", 0.25, 4.0, 0.05, Linear, true),
            GZFX_FLOAT(LocalGlowRadiusPixels, localGlowRadiusPixels, "LocalGlow", "RadiusPixels", 1.0, 32.0, 0.5, Logarithmic, true),

            GZFX_BOOL(ExposureEnabled, exposureEnabled, "Exposure", "Enabled", true),
            GZFX_BOOL(AutoExposureEnabled, autoExposureEnabled, "Exposure", "AutoEnabled", true),
            GZFX_FLOAT(ManualExposureEv, manualExposureEv, "Exposure", "ManualEV", -4.0, 4.0, 0.10, Linear, true),
            GZFX_FLOAT(ExposureCompensationEv, exposureCompensationEv, "Exposure", "CompensationEV", -4.0, 4.0, 0.10, Linear, true),
            GZFX_FLOAT(ExposureKey, exposureKey, "Exposure", "Key", 0.05, 0.50, 0.01, Linear, true),
            GZFX_FLOAT(ExposureMinEv, exposureMinEv, "Exposure", "MinEV", -12.0, 0.0, 0.25, Linear, true),
            GZFX_FLOAT(ExposureMaxEv, exposureMaxEv, "Exposure", "MaxEV", 0.0, 12.0, 0.25, Linear, true),
            GZFX_FLOAT(ExposureSpeedUp, exposureSpeedUp, "Exposure", "SpeedUp", 0.10, 10.0, 0.10, Logarithmic, true),
            GZFX_FLOAT(ExposureSpeedDown, exposureSpeedDown, "Exposure", "SpeedDown", 0.10, 10.0, 0.10, Logarithmic, true),

            GZFX_BOOL(BloomEnabled, bloomEnabled, "Bloom", "Enabled", true),
            GZFX_FLOAT(BloomStrength, bloomStrength, "Bloom", "Strength", 0.0, 3.0, 0.05, Linear, true),
            GZFX_FLOAT(BloomThreshold, bloomThreshold, "Bloom", "Threshold", 0.0, 8.0, 0.10, Linear, true),
            GZFX_FLOAT(BloomSoftKnee, bloomSoftKnee, "Bloom", "SoftKnee", 0.0, 1.0, 0.025, Linear, true),
            GZFX_FLOAT(BloomRadius, bloomRadius, "Bloom", "Radius", 0.0, 8.0, 0.25, Linear, true),
            GZFX_BOOL(LensDirtEnabled, lensDirtEnabled, "Bloom", "LensDirtEnabled", true),
            GZFX_FLOAT(LensDirtStrength, lensDirtStrength, "Bloom", "LensDirtStrength", 0.0, 2.0, 0.05, Linear, true),

            GZFX_BOOL(ColorEnabled, colorEnabled, "Color", "Enabled", true),
            GZFX_FLOAT(ColorContrast, colorContrast, "Color", "Contrast", 0.50, 2.0, 0.025, Linear, true),
            GZFX_FLOAT(ColorGamma, colorGamma, "Color", "Gamma", 0.50, 2.0, 0.025, Linear, true),
            GZFX_FLOAT(ColorSaturation, colorSaturation, "Color", "Saturation", 0.0, 2.0, 0.025, Linear, true),
            GZFX_FLOAT(ColorVibrance, colorVibrance, "Color", "Vibrance", -1.0, 1.0, 0.025, Linear, true),
            GZFX_FLOAT(ColorTemperature, colorTemperature, "Color", "Temperature", -1.0, 1.0, 0.025, Linear, true),
            GZFX_FLOAT(ColorTint, colorTint, "Color", "Tint", -1.0, 1.0, 0.025, Linear, true),
            GZFX_FLOAT(ColorLift, colorLift, "Color", "Lift", -0.25, 0.25, 0.01, Linear, true),
            GZFX_FLOAT(ColorGain, colorGain, "Color", "Gain", 0.50, 2.0, 0.025, Linear, true),
            GZFX_FLOAT(ToneToe, toneToe, "Tone", "Toe", 0.0, 1.0, 0.025, Linear, true),
            GZFX_FLOAT(ToneShoulder, toneShoulder, "Tone", "Shoulder", 0.0, 1.0, 0.025, Linear, true),
            GZFX_FLOAT(ToneWhiteClip, toneWhiteClip, "Tone", "WhiteClip", 1.0, 16.0, 0.25, Logarithmic, true),

            GZFX_BOOL(DofEnabled, dofEnabled, "DepthOfField", "Enabled", true),
            GZFX_FLOAT(DofFocusDistance, dofFocusDistance, "DepthOfField", "FocusDistance", 10.0, 50000.0, 50.0, Logarithmic, true),
            GZFX_FLOAT(DofFocusRange, dofFocusRange, "DepthOfField", "FocusRange", 10.0, 10000.0, 25.0, Logarithmic, true),
            GZFX_FLOAT(DofNearStrength, dofNearStrength, "DepthOfField", "NearStrength", 0.0, 1.0, 0.025, Linear, true),
            GZFX_FLOAT(DofFarStrength, dofFarStrength, "DepthOfField", "FarStrength", 0.0, 1.0, 0.025, Linear, true),
            GZFX_FLOAT(DofMaxRadius, dofMaxRadius, "DepthOfField", "MaxRadius", 0.0, 32.0, 0.50, Linear, true),

            GZFX_BOOL(MotionBlurEnabled, motionBlurEnabled, "MotionBlur", "Enabled", true),
            GZFX_FLOAT(MotionBlurStrength, motionBlurStrength, "MotionBlur", "Strength", 0.0, 2.0, 0.025, Linear, true),
            GZFX_FLOAT(MotionBlurMaxPixels, motionBlurMaxPixels, "MotionBlur", "MaxPixels", 0.0, 64.0, 1.0, Linear, true),

            GZFX_BOOL(DepthFogEnabled, depthFogEnabled, "DepthFog", "Enabled", true),
            GZFX_FLOAT(DepthFogStart, depthFogStart, "DepthFog", "Start", 0.0, 50000.0, 100.0, Linear, true),
            GZFX_FLOAT(DepthFogEnd, depthFogEnd, "DepthFog", "End", 10.0, 100000.0, 100.0, Logarithmic, true),
            GZFX_FLOAT(DepthFogDensity, depthFogDensity, "DepthFog", "Density", 0.0, 4.0, 0.05, Linear, true),
            GZFX_FLOAT(DepthFogColorR, depthFogColorR, "DepthFog", "ColorR", 0.0, 2.0, 0.025, Linear, true),
            GZFX_FLOAT(DepthFogColorG, depthFogColorG, "DepthFog", "ColorG", 0.0, 2.0, 0.025, Linear, true),
            GZFX_FLOAT(DepthFogColorB, depthFogColorB, "DepthFog", "ColorB", 0.0, 2.0, 0.025, Linear, true),

            GZFX_BOOL(VignetteEnabled, vignetteEnabled, "Optics", "VignetteEnabled", true),
            GZFX_FLOAT(VignetteStrength, vignetteStrength, "Optics", "VignetteStrength", 0.0, 1.0, 0.025, Linear, true),
            GZFX_FLOAT(VignetteRadius, vignetteRadius, "Optics", "VignetteRadius", 0.0, 1.0, 0.025, Linear, true),
            GZFX_FLOAT(VignetteSoftness, vignetteSoftness, "Optics", "VignetteSoftness", 0.01, 1.0, 0.025, Linear, true),
            GZFX_BOOL(FilmGrainEnabled, filmGrainEnabled, "Optics", "FilmGrainEnabled", true),
            GZFX_FLOAT(FilmGrainStrength, filmGrainStrength, "Optics", "FilmGrainStrength", 0.0, 0.25, 0.005, Linear, true),
            GZFX_FLOAT(FilmGrainScale, filmGrainScale, "Optics", "FilmGrainScale", 0.50, 4.0, 0.10, Logarithmic, true),
            GZFX_BOOL(ChromaticAberrationEnabled, chromaticAberrationEnabled, "Optics", "ChromaticAberrationEnabled", true),
            GZFX_FLOAT(ChromaticAberrationStrength, chromaticAberrationStrength, "Optics", "ChromaticAberrationStrength", 0.0, 8.0, 0.10, Linear, true),
            GZFX_BOOL(DenoiseEnabled, denoiseEnabled, "Filtering", "DenoiseEnabled", true),
            GZFX_FLOAT(DenoiseStrength, denoiseStrength, "Filtering", "DenoiseStrength", 0.0, 1.0, 0.025, Linear, true),
            GZFX_BOOL(SharpenEnabled, sharpenEnabled, "Filtering", "SharpenEnabled", true),
            GZFX_FLOAT(SharpenStrength, sharpenStrength, "Filtering", "SharpenStrength", 0.0, 2.0, 0.025, Linear, true),
            GZFX_FLOAT(SharpenClamp, sharpenClamp, "Filtering", "SharpenClamp", 0.0, 2.0, 0.025, Linear, true),
            GZFX_BOOL(BarsEnabled, barsEnabled, "Composition", "BarsEnabled", true),
            GZFX_FLOAT(LetterboxAmount, letterboxAmount, "Composition", "LetterboxAmount", 0.0, 0.30, 0.005, Linear, true),
            GZFX_FLOAT(PillarboxAmount, pillarboxAmount, "Composition", "PillarboxAmount", 0.0, 0.30, 0.005, Linear, true),

            GZFX_U32(DefaultMode, defaultMode, "Development", "DefaultMode", 0, 4, 1, false),
            GZFX_U64(ActivationDrawIndexedThreshold, activationDrawIndexedThreshold, "Development", "ActivationDrawIndexedThreshold", 0, 20000, 250, false),
            GZFX_BOOL(AccessibleDiagnostics, accessibleDiagnostics, "Development", "AccessibleDiagnostics", false),
            GZFX_BOOL(CaptureOnModeChange, captureOnModeChange, "Development", "CaptureOnModeChange", false)
        }};

#undef GZFX_BOOL
#undef GZFX_FLOAT
#undef GZFX_U32
#undef GZFX_U64
#undef GZFX_ACTION

        static_assert(
            kDescriptors.size() ==
                static_cast<std::size_t>(Editable::Count));

        std::mutex g_mutex;
        Values g_values{};
        bool g_loaded = false;
        std::uint64_t g_revision = 1;

        fs::path SettingsPath()
        {
            const fs::path root =
                gzfx::paths::RuntimeRoot();

            return root.empty()
                ? fs::path{}
                : root / L"Settings.ini";
        }

        const Descriptor* DescriptorFor(
            const Editable editable)
        {
            const auto index =
                static_cast<std::size_t>(editable);

            if (index >= kDescriptors.size())
            {
                return nullptr;
            }

            const Descriptor& descriptor =
                kDescriptors[index];

            return descriptor.editable == editable
                ? &descriptor
                : nullptr;
        }

        template <class T>
        T& Field(
            Values& values,
            const std::size_t offset)
        {
            return *reinterpret_cast<T*>(
                reinterpret_cast<std::byte*>(
                    &values) +
                offset);
        }

        template <class T>
        const T& Field(
            const Values& values,
            const std::size_t offset)
        {
            return *reinterpret_cast<const T*>(
                reinterpret_cast<const std::byte*>(
                    &values) +
                offset);
        }

        std::wstring ReadString(
            const fs::path& path,
            const wchar_t* section,
            const wchar_t* key,
            const wchar_t* fallback)
        {
            std::array<wchar_t, 128> value{};

            GetPrivateProfileStringW(
                section,
                key,
                fallback,
                value.data(),
                static_cast<DWORD>(
                    value.size()),
                path.c_str());

            return value.data();
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

        double ReadNumber(
            const fs::path& path,
            const wchar_t* section,
            const wchar_t* key,
            const double fallback)
        {
            wchar_t fallbackText[64]{};

            swprintf_s(
                fallbackText,
                L"%.9g",
                fallback);

            const std::wstring text =
                ReadString(
                    path,
                    section,
                    key,
                    fallbackText);

            wchar_t* end = nullptr;
            const double parsed =
                std::wcstod(
                    text.c_str(),
                    &end);

            return end == text.c_str() ||
                !std::isfinite(parsed)
                ? fallback
                : parsed;
        }

        std::wstring SectionFor(
            const Descriptor& descriptor,
            const wchar_t* profilePrefix)
        {
            if (profilePrefix == nullptr ||
                profilePrefix[0] == L'\0')
            {
                return descriptor.section != nullptr
                    ? descriptor.section
                    : L"";
            }

            std::wstring section =
                profilePrefix;

            section += L".";
            section += descriptor.section != nullptr
                ? descriptor.section
                : L"";

            return section;
        }

        double NumberFrom(
            const Values& values,
            const Descriptor& descriptor)
        {
            switch (descriptor.type)
            {
            case ValueType::Boolean:
                return Field<bool>(
                    values,
                    descriptor.offset)
                    ? 1.0
                    : 0.0;

            case ValueType::Float:
                return Field<float>(
                    values,
                    descriptor.offset);

            case ValueType::UInt32:
                return static_cast<double>(
                    Field<std::uint32_t>(
                        values,
                        descriptor.offset));

            case ValueType::UInt64:
                return static_cast<double>(
                    Field<std::uint64_t>(
                        values,
                        descriptor.offset));

            case ValueType::Action:
                return 0.0;
            }

            return 0.0;
        }

        void SetNumber(
            Values& values,
            const Descriptor& descriptor,
            double number)
        {
            number =
                std::clamp(
                    number,
                    descriptor.minimum,
                    descriptor.maximum);

            switch (descriptor.type)
            {
            case ValueType::Boolean:
                Field<bool>(
                    values,
                    descriptor.offset) =
                    number >= 0.5;
                break;

            case ValueType::Float:
                Field<float>(
                    values,
                    descriptor.offset) =
                    static_cast<float>(
                        number);
                break;

            case ValueType::UInt32:
                Field<std::uint32_t>(
                    values,
                    descriptor.offset) =
                    static_cast<std::uint32_t>(
                        std::llround(number));
                break;

            case ValueType::UInt64:
                Field<std::uint64_t>(
                    values,
                    descriptor.offset) =
                    static_cast<std::uint64_t>(
                        std::llround(number));
                break;

            case ValueType::Action:
                break;
            }
        }

        bool WriteDescriptor(
            const fs::path& path,
            const Descriptor& descriptor,
            const Values& values,
            const wchar_t* profilePrefix = nullptr)
        {
            if (descriptor.type ==
                    ValueType::Action ||
                descriptor.section == nullptr ||
                descriptor.key == nullptr)
            {
                return true;
            }

            const std::wstring section =
                SectionFor(
                    descriptor,
                    profilePrefix);

            wchar_t text[64]{};

            switch (descriptor.type)
            {
            case ValueType::Boolean:
                wcscpy_s(
                    text,
                    NumberFrom(
                        values,
                        descriptor) >= 0.5
                        ? L"1"
                        : L"0");
                break;

            case ValueType::Float:
                swprintf_s(
                    text,
                    L"%.6g",
                    NumberFrom(
                        values,
                        descriptor));
                break;

            case ValueType::UInt32:
            case ValueType::UInt64:
                swprintf_s(
                    text,
                    L"%.0f",
                    NumberFrom(
                        values,
                        descriptor));
                break;

            case ValueType::Action:
                return true;
            }

            return WriteString(
                path,
                section.c_str(),
                descriptor.key,
                text);
        }

        void ReadDescriptor(
            const fs::path& path,
            const Descriptor& descriptor,
            Values& destination,
            const Values& fallback,
            const wchar_t* profilePrefix = nullptr)
        {
            if (descriptor.type ==
                    ValueType::Action ||
                descriptor.section == nullptr ||
                descriptor.key == nullptr)
            {
                return;
            }

            const std::wstring section =
                SectionFor(
                    descriptor,
                    profilePrefix);

            const double fallbackNumber =
                NumberFrom(
                    fallback,
                    descriptor);

            const double number =
                ReadNumber(
                    path,
                    section.c_str(),
                    descriptor.key,
                    fallbackNumber);

            SetNumber(
                destination,
                descriptor,
                number);
        }

        void EnsureDefaults(
            const fs::path& path)
        {
            if (path.empty())
            {
                return;
            }

            const Values defaults{};

            for (const Descriptor& descriptor :
                 kDescriptors)
            {
                if (descriptor.type ==
                        ValueType::Action ||
                    descriptor.section == nullptr ||
                    descriptor.key == nullptr)
                {
                    continue;
                }

                const std::wstring present =
                    ReadString(
                        path,
                        descriptor.section,
                        descriptor.key,
                        L"");

                if (present.empty())
                {
                    WriteDescriptor(
                        path,
                        descriptor,
                        defaults);
                }
            }
        }

        void NormalizeRelations(
            Values& values)
        {
            if (values.exposureMinEv >
                values.exposureMaxEv)
            {
                std::swap(
                    values.exposureMinEv,
                    values.exposureMaxEv);
            }

            if (values.exposureMaxEv -
                    values.exposureMinEv <
                0.25f)
            {
                values.exposureMaxEv =
                    std::min(
                        12.0f,
                        values.exposureMinEv +
                            0.25f);
            }

            values.depthFogEnd =
                std::max(
                    values.depthFogEnd,
                    values.depthFogStart +
                        10.0f);

            values.productPreset =
                std::min<std::uint32_t>(
                    values.productPreset,
                    static_cast<std::uint32_t>(
                        Preset::SableCinema));

            values.productCustomSlot =
                std::clamp<std::uint32_t>(
                    values.productCustomSlot,
                    1,
                    3);
        }

        bool PersistValues(
            const fs::path& path,
            const Values& values,
            const wchar_t* profilePrefix = nullptr,
            const bool profilesOnly = false)
        {
            for (const Descriptor& descriptor :
                 kDescriptors)
            {
                if (profilesOnly &&
                    !descriptor.profile)
                {
                    continue;
                }

                if (!WriteDescriptor(
                        path,
                        descriptor,
                        values,
                        profilePrefix))
                {
                    return false;
                }
            }

            return true;
        }

        bool PersistUnlocked()
        {
            const fs::path path =
                SettingsPath();

            if (path.empty())
            {
                return false;
            }

            return PersistValues(
                path,
                g_values);
        }

        bool LoadUnlocked()
        {
            const fs::path path =
                SettingsPath();

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

            Values loaded{};
            const Values defaults{};

            for (const Descriptor& descriptor :
                 kDescriptors)
            {
                ReadDescriptor(
                    path,
                    descriptor,
                    loaded,
                    defaults);
            }

            NormalizeRelations(loaded);

            g_values = loaded;
            g_loaded = true;
            return true;
        }

        void MarkCustomUnlocked(
            const Editable setting)
        {
            switch (setting)
            {
            case Editable::ProductEnabled:
            case Editable::ProductPreset:
            case Editable::ProductCustomSlot:
            case Editable::ApplyPresetAction:
            case Editable::LoadCustomAction:
            case Editable::SaveCustomAction:
            case Editable::DefaultMode:
            case Editable::ActivationDrawIndexedThreshold:
            case Editable::AccessibleDiagnostics:
            case Editable::CaptureOnModeChange:
            case Editable::Count:
                return;

            default:
                g_values.productPreset =
                    static_cast<std::uint32_t>(
                        Preset::Custom);
                break;
            }
        }

        double ToNormalized(
            const Descriptor& descriptor,
            const double number)
        {
            if (descriptor.maximum <=
                descriptor.minimum)
            {
                return 0.0;
            }

            const double clamped =
                std::clamp(
                    number,
                    descriptor.minimum,
                    descriptor.maximum);

            if (descriptor.mapping ==
                    Mapping::Logarithmic &&
                descriptor.minimum > 0.0 &&
                descriptor.maximum > 0.0)
            {
                return
                    std::log(
                        clamped /
                        descriptor.minimum) /
                    std::log(
                        descriptor.maximum /
                        descriptor.minimum);
            }

            return
                (clamped -
                    descriptor.minimum) /
                (descriptor.maximum -
                    descriptor.minimum);
        }

        double FromNormalized(
            const Descriptor& descriptor,
            const double normalized)
        {
            const double value =
                std::clamp(
                    normalized,
                    0.0,
                    1.0);

            if (descriptor.mapping ==
                    Mapping::Logarithmic &&
                descriptor.minimum > 0.0 &&
                descriptor.maximum > 0.0)
            {
                return
                    descriptor.minimum *
                    std::pow(
                        descriptor.maximum /
                            descriptor.minimum,
                        value);
            }

            return
                descriptor.minimum +
                value *
                (descriptor.maximum -
                    descriptor.minimum);
        }

        void ApplyPresetUnlocked(
            const Preset preset)
        {
            Values next =
                g_values;

            // Start every built-in look from a known coherent product base.
            next.productEnabled = true;
            next.productQuality = 1;
            next.productPreset =
                static_cast<std::uint32_t>(
                    preset);

            next.exposureEnabled = true;
            next.autoExposureEnabled = true;
            next.manualExposureEv = 0.0f;
            next.exposureCompensationEv = 0.0f;
            next.exposureKey = 0.18f;
            next.exposureMinEv = -8.0f;
            next.exposureMaxEv = 8.0f;
            next.exposureSpeedUp = 2.5f;
            next.exposureSpeedDown = 1.5f;

            next.bloomEnabled = true;
            next.bloomStrength = 0.28f;
            next.bloomThreshold = 1.1f;
            next.bloomSoftKnee = 0.5f;
            next.bloomRadius = 4.0f;
            next.lensDirtEnabled = false;
            next.lensDirtStrength = 0.0f;

            next.colorEnabled = true;
            next.colorContrast = 1.0f;
            next.colorGamma = 1.0f;
            next.colorSaturation = 1.0f;
            next.colorVibrance = 0.0f;
            next.colorTemperature = 0.0f;
            next.colorTint = 0.0f;
            next.colorLift = 0.0f;
            next.colorGain = 1.0f;

            next.toneToe = 0.0f;
            next.toneShoulder = 0.0f;
            next.toneWhiteClip = 16.0f;

            next.dofEnabled = false;
            next.motionBlurEnabled = false;
            next.depthFogEnabled = false;
            next.vignetteEnabled = false;
            next.filmGrainEnabled = false;
            next.chromaticAberrationEnabled = false;
            next.denoiseEnabled = false;
            next.sharpenEnabled = true;
            next.sharpenStrength = 0.18f;
            next.sharpenClamp = 0.25f;
            next.barsEnabled = false;
            next.letterboxAmount = 0.0f;
            next.pillarboxAmount = 0.0f;

            switch (preset)
            {
            case Preset::Custom:
                next.productPreset =
                    static_cast<std::uint32_t>(
                        Preset::Custom);
                break;

            case Preset::Clearfell:
                next.exposureCompensationEv = 0.15f;
                next.bloomStrength = 0.20f;
                next.colorContrast = 1.06f;
                next.colorSaturation = 1.02f;
                next.colorTemperature = -0.04f;
                next.colorVibrance = 0.08f;
                next.toneShoulder = 0.10f;
                next.sharpenStrength = 0.26f;
                break;

            case Preset::Emberveil:
                next.exposureCompensationEv = 0.10f;
                next.bloomStrength = 0.48f;
                next.bloomThreshold = 0.85f;
                next.lensDirtEnabled = true;
                next.lensDirtStrength = 0.18f;
                next.colorContrast = 1.10f;
                next.colorSaturation = 1.08f;
                next.colorTemperature = 0.18f;
                next.colorTint = -0.03f;
                next.toneToe = 0.08f;
                next.toneShoulder = 0.28f;
                next.vignetteEnabled = true;
                next.vignetteStrength = 0.12f;
                break;

            case Preset::Moonforge:
                next.exposureCompensationEv = -0.35f;
                next.bloomStrength = 0.30f;
                next.colorContrast = 1.13f;
                next.colorSaturation = 0.82f;
                next.colorTemperature = -0.24f;
                next.colorTint = 0.04f;
                next.toneToe = 0.12f;
                next.toneShoulder = 0.18f;
                next.vignetteEnabled = true;
                next.vignetteStrength = 0.16f;
                break;

            case Preset::VerdantNorth:
                next.exposureCompensationEv = 0.05f;
                next.bloomStrength = 0.24f;
                next.colorContrast = 1.04f;
                next.colorSaturation = 1.08f;
                next.colorVibrance = 0.16f;
                next.colorTemperature = -0.02f;
                next.colorTint = -0.10f;
                next.toneShoulder = 0.08f;
                next.sharpenStrength = 0.22f;
                break;

            case Preset::SableCinema:
                next.exposureCompensationEv = -0.20f;
                next.bloomStrength = 0.22f;
                next.colorContrast = 1.20f;
                next.colorGamma = 0.96f;
                next.colorSaturation = 0.72f;
                next.colorVibrance = -0.08f;
                next.toneToe = 0.18f;
                next.toneShoulder = 0.24f;
                next.vignetteEnabled = true;
                next.vignetteStrength = 0.24f;
                next.filmGrainEnabled = true;
                next.filmGrainStrength = 0.035f;
                next.barsEnabled = true;
                next.letterboxAmount = 0.055f;
                next.sharpenStrength = 0.14f;
                break;

            case Preset::Count:
                return;
            }

            NormalizeRelations(next);
            g_values = next;
        }

        std::wstring CustomPrefix(
            const std::uint32_t slot)
        {
            return
                L"Custom" +
                std::to_wstring(
                    std::clamp<std::uint32_t>(
                        slot,
                        1,
                        3));
        }
    }

    const Values& Get() noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

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

    std::uint64_t Revision() noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (!g_loaded)
            {
                LoadUnlocked();
            }

            return g_revision;
        }
        catch (...)
        {
            return 0;
        }
    }

    bool Reload() noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            g_loaded = false;

            if (!LoadUnlocked())
            {
                return false;
            }

            ++g_revision;
            return true;
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
            if (IsAction(setting))
            {
                return direction != 0 &&
                    Activate(setting);
            }

            std::scoped_lock lock(
                g_mutex);

            if (!g_loaded &&
                !LoadUnlocked())
            {
                return false;
            }

            const Descriptor* descriptor =
                DescriptorFor(setting);

            if (descriptor == nullptr ||
                descriptor->type ==
                    ValueType::Action)
            {
                return false;
            }

            if (descriptor->type ==
                    ValueType::Boolean)
            {
                SetNumber(
                    g_values,
                    *descriptor,
                    direction > 0
                        ? 1.0
                        : 0.0);
            }
            else
            {
                const double current =
                    NumberFrom(
                        g_values,
                        *descriptor);

                SetNumber(
                    g_values,
                    *descriptor,
                    current +
                        (direction > 0
                            ? descriptor->step
                            : -descriptor->step));
            }

            MarkCustomUnlocked(setting);
            NormalizeRelations(g_values);

            if (!PersistUnlocked())
            {
                return false;
            }

            ++g_revision;
            return true;
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
            std::scoped_lock lock(
                g_mutex);

            if (!g_loaded &&
                !LoadUnlocked())
            {
                return false;
            }

            const Descriptor* descriptor =
                DescriptorFor(setting);

            if (descriptor == nullptr ||
                descriptor->type ==
                    ValueType::Action ||
                descriptor->type ==
                    ValueType::Boolean)
            {
                return false;
            }

            SetNumber(
                g_values,
                *descriptor,
                FromNormalized(
                    *descriptor,
                    normalized));

            MarkCustomUnlocked(setting);
            NormalizeRelations(g_values);

            if (!PersistUnlocked())
            {
                return false;
            }

            ++g_revision;
            return true;
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
            std::scoped_lock lock(
                g_mutex);

            if (!g_loaded &&
                !LoadUnlocked())
            {
                return 0.0f;
            }

            const Descriptor* descriptor =
                DescriptorFor(setting);

            if (descriptor == nullptr ||
                descriptor->type ==
                    ValueType::Action)
            {
                return 0.0f;
            }

            return static_cast<float>(
                ToNormalized(
                    *descriptor,
                    NumberFrom(
                        g_values,
                        *descriptor)));
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
            std::scoped_lock lock(
                g_mutex);

            if (!g_loaded &&
                !LoadUnlocked())
            {
                return false;
            }

            const Descriptor* descriptor =
                DescriptorFor(setting);

            if (descriptor == nullptr ||
                descriptor->type !=
                    ValueType::Boolean)
            {
                return false;
            }

            const bool current =
                Field<bool>(
                    g_values,
                    descriptor->offset);

            Field<bool>(
                g_values,
                descriptor->offset) =
                !current;

            MarkCustomUnlocked(setting);

            if (!PersistUnlocked())
            {
                return false;
            }

            ++g_revision;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool Activate(
        const Editable setting) noexcept
    {
        switch (setting)
        {
        case Editable::ApplyPresetAction:
        {
            const auto preset =
                static_cast<Preset>(
                    Get().productPreset);

            return ApplyPreset(preset);
        }

        case Editable::LoadCustomAction:
            return LoadCustom(
                Get().productCustomSlot);

        case Editable::SaveCustomAction:
            return SaveCustom(
                Get().productCustomSlot);

        default:
            return false;
        }
    }

    bool IsToggle(
        const Editable setting) noexcept
    {
        const Descriptor* descriptor =
            DescriptorFor(setting);

        return descriptor != nullptr &&
            descriptor->type ==
                ValueType::Boolean;
    }

    bool IsSlider(
        const Editable setting) noexcept
    {
        const Descriptor* descriptor =
            DescriptorFor(setting);

        return descriptor != nullptr &&
            descriptor->type !=
                ValueType::Boolean &&
            descriptor->type !=
                ValueType::Action;
    }

    bool IsAction(
        const Editable setting) noexcept
    {
        const Descriptor* descriptor =
            DescriptorFor(setting);

        return descriptor != nullptr &&
            descriptor->type ==
                ValueType::Action;
    }

    bool ApplyPreset(
        const Preset preset) noexcept
    {
        try
        {
            if (preset >= Preset::Count)
            {
                return false;
            }

            std::scoped_lock lock(
                g_mutex);

            if (!g_loaded &&
                !LoadUnlocked())
            {
                return false;
            }

            ApplyPresetUnlocked(preset);

            if (!PersistUnlocked())
            {
                return false;
            }

            ++g_revision;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool SaveCustom(
        const std::uint32_t slot) noexcept
    {
        try
        {
            const std::uint32_t selected =
                std::clamp<std::uint32_t>(
                    slot,
                    1,
                    3);

            std::scoped_lock lock(
                g_mutex);

            if (!g_loaded &&
                !LoadUnlocked())
            {
                return false;
            }

            const fs::path path =
                SettingsPath();

            if (path.empty())
            {
                return false;
            }

            const std::wstring prefix =
                CustomPrefix(selected);

            if (!PersistValues(
                    path,
                    g_values,
                    prefix.c_str(),
                    true))
            {
                return false;
            }

            const std::wstring meta =
                prefix + L".Meta";

            if (!WriteString(
                    path,
                    meta.c_str(),
                    L"Saved",
                    L"1"))
            {
                return false;
            }

            g_values.productCustomSlot =
                selected;

            g_values.productPreset =
                static_cast<std::uint32_t>(
                    Preset::Custom);

            if (!PersistUnlocked())
            {
                return false;
            }

            ++g_revision;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool LoadCustom(
        const std::uint32_t slot) noexcept
    {
        try
        {
            const std::uint32_t selected =
                std::clamp<std::uint32_t>(
                    slot,
                    1,
                    3);

            std::scoped_lock lock(
                g_mutex);

            if (!g_loaded &&
                !LoadUnlocked())
            {
                return false;
            }

            const fs::path path =
                SettingsPath();

            if (path.empty())
            {
                return false;
            }

            const std::wstring prefix =
                CustomPrefix(selected);

            const std::wstring meta =
                prefix + L".Meta";

            if (ReadString(
                    path,
                    meta.c_str(),
                    L"Saved",
                    L"0") != L"1")
            {
                return false;
            }

            Values loaded =
                g_values;

            for (const Descriptor& descriptor :
                 kDescriptors)
            {
                if (!descriptor.profile)
                {
                    continue;
                }

                ReadDescriptor(
                    path,
                    descriptor,
                    loaded,
                    g_values,
                    prefix.c_str());
            }

            loaded.productCustomSlot =
                selected;

            loaded.productPreset =
                static_cast<std::uint32_t>(
                    Preset::Custom);

            NormalizeRelations(loaded);
            g_values = loaded;

            if (!PersistUnlocked())
            {
                return false;
            }

            ++g_revision;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    const char* PresetName(
        const Preset preset) noexcept
    {
        switch (preset)
        {
        case Preset::Custom:
            return "Custom";
        case Preset::Clearfell:
            return "Clearfell";
        case Preset::Emberveil:
            return "Emberveil";
        case Preset::Moonforge:
            return "Moonforge";
        case Preset::VerdantNorth:
            return "Verdant North";
        case Preset::SableCinema:
            return "Sable Cinema";
        case Preset::Count:
            return "Invalid";
        }

        return "Invalid";
    }

    const wchar_t* EditableName(
        const Editable setting) noexcept
    {
        static constexpr std::array<
            const wchar_t*,
            static_cast<std::size_t>(Editable::Count)>
            names
        {{
            L"PRODUCT MASTER",
            L"PRODUCT QUALITY",
            L"PRODUCT PRESET",
            L"CUSTOM PROFILE SLOT",
            L"APPLY PRESET",
            L"LOAD CUSTOM PROFILE",
            L"SAVE CUSTOM PROFILE",

            L"AMBIENT CONTACT STRENGTH",
            L"AMBIENT CONTACT RADIUS",
            L"HORIZON OCCLUSION",
            L"HORIZON OCCLUSION STRENGTH",
            L"HORIZON OCCLUSION RADIUS",
            L"HORIZON OCCLUSION QUALITY",
            L"INDIRECT BOUNCE (SSIL)",
            L"INDIRECT BOUNCE STRENGTH",
            L"INDIRECT BOUNCE RADIUS",
            L"INDIRECT BOUNCE SATURATION",
            L"LOCAL GLOW",
            L"LOCAL GLOW STRENGTH",
            L"LOCAL GLOW THRESHOLD",
            L"LOCAL GLOW RADIUS",

            L"EXPOSURE",
            L"AUTOMATIC EXPOSURE",
            L"MANUAL EXPOSURE EV",
            L"EXPOSURE COMPENSATION EV",
            L"EXPOSURE KEY",
            L"EXPOSURE MINIMUM EV",
            L"EXPOSURE MAXIMUM EV",
            L"EXPOSURE SPEED UP",
            L"EXPOSURE SPEED DOWN",

            L"BLOOM",
            L"BLOOM STRENGTH",
            L"BLOOM THRESHOLD",
            L"BLOOM SOFT KNEE",
            L"BLOOM RADIUS",
            L"LENS DIRT",
            L"LENS DIRT STRENGTH",

            L"COLOR CONTROLS",
            L"COLOR CONTRAST",
            L"COLOR GAMMA",
            L"COLOR SATURATION",
            L"COLOR VIBRANCE",
            L"COLOR TEMPERATURE",
            L"COLOR TINT",
            L"COLOR LIFT",
            L"COLOR GAIN",
            L"TONE TOE",
            L"TONE SHOULDER",
            L"TONE WHITE CLIP",

            L"DEPTH OF FIELD",
            L"DOF FOCUS DISTANCE",
            L"DOF FOCUS RANGE",
            L"DOF NEAR STRENGTH",
            L"DOF FAR STRENGTH",
            L"DOF MAXIMUM RADIUS",

            L"MOTION BLUR",
            L"MOTION BLUR STRENGTH",
            L"MOTION BLUR MAXIMUM PIXELS",

            L"DEPTH FOG",
            L"DEPTH FOG START",
            L"DEPTH FOG END",
            L"DEPTH FOG DENSITY",
            L"DEPTH FOG RED",
            L"DEPTH FOG GREEN",
            L"DEPTH FOG BLUE",

            L"VIGNETTE",
            L"VIGNETTE STRENGTH",
            L"VIGNETTE RADIUS",
            L"VIGNETTE SOFTNESS",
            L"FILM GRAIN",
            L"FILM GRAIN STRENGTH",
            L"FILM GRAIN SCALE",
            L"CHROMATIC ABERRATION",
            L"CHROMATIC ABERRATION STRENGTH",
            L"DENOISE",
            L"DENOISE STRENGTH",
            L"SHARPEN",
            L"SHARPEN STRENGTH",
            L"SHARPEN CLAMP",
            L"COMPOSITION BARS",
            L"LETTERBOX AMOUNT",
            L"PILLARBOX AMOUNT",

            L"DEFAULT DEVELOPMENT VIEW",
            L"WORLD DRAW THRESHOLD",
            L"ACCESSIBLE DIAGNOSTICS",
            L"CAPTURE ON MODE CHANGE"
        }};

        const auto index =
            static_cast<std::size_t>(setting);

        return index < names.size()
            ? names[index]
            : L"";
    }

    std::wstring EditableValue(
        const Editable setting) noexcept
    {
        try
        {
            const Values values = Get();
            const Descriptor* descriptor =
                DescriptorFor(setting);

            if (descriptor == nullptr)
            {
                return L"";
            }

            switch (setting)
            {
            case Editable::ProductQuality:
                return values.productQuality == 0
                    ? L"MEDIUM"
                    : L"ULTRA+";

            case Editable::ProductPreset:
                switch (static_cast<Preset>(
                    values.productPreset))
                {
                case Preset::Custom: return L"CUSTOM";
                case Preset::Clearfell: return L"CLEARFELL";
                case Preset::Emberveil: return L"EMBERVEIL";
                case Preset::Moonforge: return L"MOONFORGE";
                case Preset::VerdantNorth: return L"VERDANT NORTH";
                case Preset::SableCinema: return L"SABLE CINEMA";
                case Preset::Count: return L"INVALID";
                }
                return L"INVALID";

            case Editable::ProductCustomSlot:
                return std::wstring(L"CUSTOM ") +
                    std::to_wstring(
                        values.productCustomSlot);

            case Editable::ApplyPresetAction:
                return L"APPLY";

            case Editable::LoadCustomAction:
                return L"LOAD";

            case Editable::SaveCustomAction:
                return L"SAVE";

            case Editable::HorizonOcclusionQuality:
                switch (values.horizonOcclusionQuality)
                {
                case 0: return L"LOW";
                case 1: return L"MEDIUM";
                case 2: return L"HIGH";
                case 3: return L"ULTRA";
                default: return L"UNKNOWN";
                }

            case Editable::DefaultMode:
                switch (values.defaultMode)
                {
                case 0: return L"OFF";
                case 1: return L"SPLIT";
                case 2: return L"FULL";
                case 3: return L"DEPTH";
                case 4: return L"PRE-UI COLOR";
                default: return L"UNKNOWN";
                }

            default:
                break;
            }

            if (descriptor->type == ValueType::Action)
            {
                return L"RUN";
            }

            if (descriptor->type == ValueType::Boolean)
            {
                return NumberFrom(
                    values,
                    *descriptor) >= 0.5
                    ? L"ON"
                    : L"OFF";
            }

            if (descriptor->type == ValueType::UInt32 ||
                descriptor->type == ValueType::UInt64)
            {
                return std::to_wstring(
                    static_cast<std::uint64_t>(
                        std::llround(
                            NumberFrom(
                                values,
                                *descriptor))));
            }

            std::wostringstream stream;
            stream
                << std::fixed
                << std::setprecision(2)
                << NumberFrom(
                    values,
                    *descriptor);

            return stream.str();
        }
        catch (...)
        {
            return L"";
        }
    }

}
