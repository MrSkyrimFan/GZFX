#include "gzfx/render/runtime_state.hpp"
#include "gzfx/render/unified_product_math.hpp"
#include "gzfx/settings.hpp"

#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>

namespace
{
    bool Near(float x, float y, float eps = 0.001f)
    {
        return std::fabs(x - y) <= eps;
    }

    bool Has(std::uint64_t mask, gzfx::render::unified_product::Effect effect)
    {
        return (mask & gzfx::render::unified_product::Bit(effect)) != 0;
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2)
    {
        return 2;
    }

    const std::filesystem::path root(argv[1]);
    std::error_code error;
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root, error);
    if (error || !SetEnvironmentVariableW(
            L"GZFX_TEST_OUTPUT_ROOT", root.c_str()))
    {
        return 3;
    }

    using gzfx::settings::Editable;
    using gzfx::settings::Preset;
    using gzfx::render::state::GetDevelopmentMode;
    using gzfx::render::state::SetDevelopmentMode;
    using gzfx::render::state::CycleDevelopmentMode;
    using namespace gzfx::render::unified_product;

    if (!gzfx::settings::Reload())
    {
        return 4;
    }

    const auto defaults = gzfx::settings::Get();
    if (!defaults.productEnabled || defaults.defaultMode != 2 ||
        defaults.productQuality != 0 ||
        defaults.localGlowStrength > 0.11f ||
        defaults.indirectBounceStrength > 0.08f ||
        defaults.exposureMinEv < -4.0f ||
        defaults.exposureMaxEv > 4.0f ||
        GetDevelopmentMode() != 2)
    {
        return 5;
    }

    if (CycleDevelopmentMode() != 0 ||
        CycleDevelopmentMode() != 2)
    {
        return 6;
    }

    SetDevelopmentMode(1);
    if (GetDevelopmentMode() != 2) return 7;
    SetDevelopmentMode(4);
    if (GetDevelopmentMode() != 2) return 8;
    SetDevelopmentMode(0);
    if (GetDevelopmentMode() != 0 ||
        CycleDevelopmentMode() != 2) return 9;

    if (!gzfx::settings::SetNormalized(Editable::LocalGlowStrength, 1.0f) ||
        !gzfx::settings::SetNormalized(Editable::IndirectBounceStrength, 1.0f) ||
        !gzfx::settings::SetNormalized(Editable::HorizonOcclusionStrength, 1.0f) ||
        !gzfx::settings::SetNormalized(Editable::BloomStrength, 1.0f) ||
        !gzfx::settings::SetNormalized(Editable::ManualExposureEv, 1.0f))
    {
        return 10;
    }

    const auto bounded = gzfx::settings::Get();
    if (bounded.localGlowStrength > 0.401f ||
        bounded.indirectBounceStrength > 0.221f ||
        bounded.horizonOcclusionStrength > 0.351f ||
        bounded.bloomStrength > 0.651f ||
        bounded.manualExposureEv > 1.501f)
    {
        return 11;
    }

    const auto oldRevision = gzfx::settings::Revision();
    if (!gzfx::settings::SetNormalized(Editable::BloomStrength, 1.0f) ||
        gzfx::settings::Revision() != oldRevision)
    {
        return 12;
    }

    // A preset slider selection must actually apply a fresh full product
    // state rather than just change the selected label.
    if (!gzfx::settings::SetNormalized(Editable::ProductPreset, 0.40f))
    {
        return 13;
    }

    const auto ember = gzfx::settings::Get();
    if (ember.productPreset !=
            static_cast<std::uint32_t>(Preset::Emberveil) ||
        !ember.bloomEnabled || !ember.colorEnabled ||
        !Near(ember.localGlowStrength, defaults.localGlowStrength) ||
        !Near(ember.indirectBounceStrength, defaults.indirectBounceStrength) ||
        !Near(ember.halationStrength, 0.06f))
    {
        return 14;
    }

    if (!gzfx::settings::SetNormalized(Editable::HalationStrength, 0.5f) ||
        !gzfx::settings::SetNormalized(Editable::ShadowRecovery, 0.5f) ||
        !gzfx::settings::SetNormalized(Editable::HighlightRolloff, 0.5f) ||
        !gzfx::settings::SetNormalized(Editable::ClarityStrength, 0.5f) ||
        !gzfx::settings::SetNormalized(Editable::ShadowWarmth, 1.0f) ||
        !gzfx::settings::SetNormalized(Editable::HighlightWarmth, 0.0f) ||
        !gzfx::settings::SetNormalized(Editable::DitherStrength, 0.5f))
    {
        return 15;
    }

    const auto custom = gzfx::settings::Get();
    const std::uint64_t mask = BuildEffectMask(custom);
    if (custom.productPreset != static_cast<std::uint32_t>(Preset::Custom) ||
        !Has(mask, Effect::ShadowRecovery) ||
        !Has(mask, Effect::HighlightRolloff) ||
        !Has(mask, Effect::Clarity) ||
        !Has(mask, Effect::Halation) ||
        !Has(mask, Effect::SplitToning) ||
        !Has(mask, Effect::FineDither))
    {
        return 16;
    }

    if (!gzfx::settings::SaveCustom(2))
    {
        return 17;
    }

    const float savedRecovery = gzfx::settings::Get().shadowRecovery;
    if (!gzfx::settings::Activate(Editable::ResetDefaultsAction))
    {
        return 18;
    }

    const auto reset = gzfx::settings::Get();
    if (!Near(reset.shadowRecovery, 0.0f) ||
        !Near(reset.highlightRolloff, 0.0f) ||
        !Near(reset.clarityStrength, 0.0f) ||
        !Near(reset.halationStrength, 0.0f) ||
        !Near(reset.shadowWarmth, 0.0f) ||
        !Near(reset.highlightWarmth, 0.0f) ||
        !Near(reset.ditherStrength, 0.0f) ||
        !Near(reset.localGlowStrength, defaults.localGlowStrength) ||
        !Near(reset.indirectBounceStrength, defaults.indirectBounceStrength))
    {
        return 19;
    }

    if (!gzfx::settings::LoadCustom(2) ||
        !Near(gzfx::settings::Get().shadowRecovery, savedRecovery))
    {
        return 20;
    }

    if (!gzfx::settings::Reload() ||
        !Near(gzfx::settings::Get().shadowRecovery, savedRecovery))
    {
        return 21;
    }

    // A previously persisted out-of-range value is conservatively clamped.
    const auto ini = root / L"GZFX" / L"Settings.ini";
    if (!WritePrivateProfileStringW(
            L"LocalGlow", L"Strength", L"1.38333", ini.c_str()) ||
        !gzfx::settings::Reload() ||
        gzfx::settings::Get().localGlowStrength > 0.401f)
    {
        return 22;
    }

    std::wcout << L"0.0.38 product calibration + mode contracts passed.\n";
    return 0;
}
