#include "gzfx/render/unified_product_math.hpp"
#include "gzfx/settings.hpp"

#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

namespace
{
    bool Near(
        const float left,
        const float right,
        const float epsilon = 1e-4f)
    {
        return std::fabs(left - right) <= epsilon;
    }

    bool Has(
        const std::uint64_t mask,
        const gzfx::render::unified_product::Effect effect)
    {
        return (mask &
            gzfx::render::unified_product::Bit(effect)) != 0;
    }
}

int wmain(
    int argc,
    wchar_t** argv)
{
    if (argc != 2)
    {
        return 2;
    }

    const std::filesystem::path outputRoot(argv[1]);
    std::error_code error;
    std::filesystem::remove_all(outputRoot, error);
    error.clear();
    std::filesystem::create_directories(outputRoot, error);

    if (error ||
        !SetEnvironmentVariableW(
            L"GZFX_TEST_OUTPUT_ROOT",
            outputRoot.c_str()))
    {
        return 3;
    }

    using namespace gzfx::render::unified_product;

    const auto medium = Budget(0);
    const auto ultra = Budget(1);

    if (medium.bloomLevels != 4 ||
        medium.dofSamples != 8 ||
        medium.motionSamples != 6 ||
        medium.denoiseSamples != 4 ||
        ultra.bloomLevels != 6 ||
        ultra.dofSamples != 16 ||
        ultra.motionSamples != 12 ||
        ultra.denoiseSamples != 8)
    {
        return 4;
    }

    if (kGraphStages.size() != 6 ||
        std::string(kGraphStages.front()) !=
            "source_snapshot" ||
        std::string(kGraphStages.back()) !=
            "scene_writeback")
    {
        return 5;
    }

    if (!gzfx::settings::Reload())
    {
        return 6;
    }

    const auto defaults =
        gzfx::settings::Get();

    const std::uint64_t defaultMask =
        BuildEffectMask(defaults);

    if (!defaults.productEnabled ||
        defaults.exposureEnabled ||
        defaults.bloomEnabled ||
        defaults.dofEnabled ||
        defaults.motionBlurEnabled ||
        !Has(defaultMask, Effect::AmbientContact) ||
        !Has(defaultMask, Effect::HorizonOcclusion) ||
        !Has(defaultMask, Effect::IndirectBounce) ||
        Has(defaultMask, Effect::Bloom) ||
        !Has(defaultMask, Effect::LocalGlow))
    {
        return 7;
    }

    auto disabled = defaults;
    disabled.productEnabled = false;

    if (BuildEffectMask(disabled) != 0)
    {
        return 8;
    }

    if (BloomSoftKneeWeight(0.1f, 1.0f, 0.5f) > 1e-5f ||
        BloomSoftKneeWeight(4.0f, 1.0f, 0.5f) <= 0.0f)
    {
        return 9;
    }

    const float adaptedUp =
        AdaptExposure(
            1.0f,
            4.0f,
            2.0f,
            1.0f,
            1.0f / 60.0f);

    const float adaptedDown =
        AdaptExposure(
            4.0f,
            1.0f,
            2.0f,
            1.0f,
            1.0f / 60.0f);

    if (!(adaptedUp > 1.0f && adaptedUp < 4.0f) ||
        !(adaptedDown < 4.0f && adaptedDown > 1.0f))
    {
        return 10;
    }

    for (std::uint32_t index = 0;
         index < static_cast<std::uint32_t>(
            gzfx::settings::Editable::Count);
         ++index)
    {
        const auto item =
            static_cast<gzfx::settings::Editable>(index);

        if (gzfx::settings::EditableName(item)[0] == L'\0' ||
            gzfx::settings::EditableValue(item).empty())
        {
            return 11;
        }
    }

    const auto beforePresetRevision =
        gzfx::settings::Revision();

    if (!gzfx::settings::ApplyPreset(
            gzfx::settings::Preset::Emberveil))
    {
        return 12;
    }

    const auto ember =
        gzfx::settings::Get();

    const std::uint64_t emberMask =
        BuildEffectMask(ember);

    if (gzfx::settings::Revision() <=
            beforePresetRevision ||
        ember.productPreset !=
            static_cast<std::uint32_t>(
                gzfx::settings::Preset::Emberveil) ||
        !ember.exposureEnabled ||
        !ember.autoExposureEnabled ||
        !ember.bloomEnabled ||
        !ember.colorEnabled ||
        !Has(emberMask, Effect::ExposureAuto) ||
        !Has(emberMask, Effect::Bloom) ||
        !Has(emberMask, Effect::Color))
    {
        return 13;
    }

    if (!gzfx::settings::SetNormalized(
            gzfx::settings::Editable::BloomStrength,
            0.731f) ||
        !gzfx::settings::SetNormalized(
            gzfx::settings::Editable::ColorTemperature,
            0.187f) ||
        !gzfx::settings::SaveCustom(2))
    {
        return 14;
    }

    const float savedBloom =
        gzfx::settings::Get().bloomStrength;

    const float savedTemperature =
        gzfx::settings::Get().colorTemperature;

    if (!gzfx::settings::SetNormalized(
            gzfx::settings::Editable::BloomStrength,
            0.05f) ||
        !gzfx::settings::SetNormalized(
            gzfx::settings::Editable::ColorTemperature,
            0.95f) ||
        !gzfx::settings::LoadCustom(2))
    {
        return 15;
    }

    const auto loaded =
        gzfx::settings::Get();

    if (!Near(
            loaded.bloomStrength,
            savedBloom,
            1e-3f) ||
        !Near(
            loaded.colorTemperature,
            savedTemperature,
            1e-3f) ||
        loaded.productCustomSlot != 2 ||
        loaded.productPreset !=
            static_cast<std::uint32_t>(
                gzfx::settings::Preset::Custom))
    {
        return 16;
    }

    if (!gzfx::settings::SetNormalized(
            gzfx::settings::Editable::ExposureMinEv,
            1.0f) ||
        !gzfx::settings::SetNormalized(
            gzfx::settings::Editable::ExposureMaxEv,
            0.0f))
    {
        return 17;
    }

    const auto normalized =
        gzfx::settings::Get();

    if (normalized.exposureMaxEv <=
        normalized.exposureMinEv)
    {
        return 18;
    }

    if (!gzfx::settings::ApplyPreset(
            gzfx::settings::Preset::Moonforge) ||
        !gzfx::settings::Toggle(
            gzfx::settings::Editable::ProductEnabled))
    {
        return 19;
    }

    const auto masterOff =
        gzfx::settings::Get();

    if (masterOff.productEnabled ||
        masterOff.productPreset !=
            static_cast<std::uint32_t>(
                gzfx::settings::Preset::Moonforge) ||
        BuildEffectMask(masterOff) != 0)
    {
        return 20;
    }

    if (!gzfx::settings::Toggle(
            gzfx::settings::Editable::ProductEnabled))
    {
        return 21;
    }

    const auto masterOn =
        gzfx::settings::Get();

    if (!masterOn.productEnabled ||
        masterOn.productPreset !=
            static_cast<std::uint32_t>(
                gzfx::settings::Preset::Moonforge))
    {
        return 22;
    }

    if (!gzfx::settings::SetNormalized(
            gzfx::settings::Editable::BloomStrength,
            0.611f))
    {
        return 23;
    }

    const float customBloomBefore =
        gzfx::settings::Get().bloomStrength;

    if (!gzfx::settings::ApplyPreset(
            gzfx::settings::Preset::Custom))
    {
        return 24;
    }

    const auto customApplied =
        gzfx::settings::Get();

    if (customApplied.productPreset !=
            static_cast<std::uint32_t>(
                gzfx::settings::Preset::Custom) ||
        !Near(
            customApplied.bloomStrength,
            customBloomBefore,
            1e-4f))
    {
        return 25;
    }

    std::wcout
        << L"Unified product renderer contracts passed.\n";

    return 0;
}
