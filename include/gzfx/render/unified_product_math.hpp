#pragma once

#include "gzfx/settings.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace gzfx::render::unified_product
{
    enum class Effect : std::uint64_t
    {
        ExposureManual = 1ull << 0,
        ExposureAuto = 1ull << 1,
        Bloom = 1ull << 2,
        LensDirt = 1ull << 3,
        Color = 1ull << 4,
        ToneShaping = 1ull << 5,
        DepthOfField = 1ull << 6,
        MotionBlur = 1ull << 7,
        DepthFog = 1ull << 8,
        Vignette = 1ull << 9,
        FilmGrain = 1ull << 10,
        ChromaticAberration = 1ull << 11,
        Denoise = 1ull << 12,
        Sharpen = 1ull << 13,
        Bars = 1ull << 14,
        AmbientContact = 1ull << 15,
        HorizonOcclusion = 1ull << 16,
        IndirectBounce = 1ull << 17,
        LocalGlow = 1ull << 18,
        ShadowRecovery = 1ull << 19,
        HighlightRolloff = 1ull << 20,
        Clarity = 1ull << 21,
        Halation = 1ull << 22,
        SplitToning = 1ull << 23,
        FineDither = 1ull << 24
    };

    constexpr std::uint64_t Bit(
        const Effect effect) noexcept
    {
        return static_cast<std::uint64_t>(effect);
    }

    struct QualityBudget
    {
        std::uint32_t bloomLevels = 4;
        std::uint32_t dofSamples = 8;
        std::uint32_t motionSamples = 6;
        std::uint32_t denoiseSamples = 4;
    };

    constexpr QualityBudget Budget(
        const std::uint32_t quality) noexcept
    {
        return quality == 0
            ? QualityBudget{4, 8, 6, 4}
            : QualityBudget{6, 16, 12, 8};
    }

    constexpr std::uint64_t BuildEffectMask(
        const settings::Values& values) noexcept
    {
        if (!values.productEnabled)
        {
            return 0;
        }

        std::uint64_t mask = 0;

        if (values.exposureEnabled)
        {
            mask |= Bit(Effect::ExposureManual);

            if (values.autoExposureEnabled)
            {
                mask |= Bit(Effect::ExposureAuto);
            }
        }

        if (values.bloomEnabled)
        {
            mask |= Bit(Effect::Bloom);

            if (values.lensDirtEnabled)
            {
                mask |= Bit(Effect::LensDirt);
            }
        }

        if (values.colorEnabled)
        {
            mask |= Bit(Effect::Color);
        }

        if (values.toneToe > 0.0f ||
            values.toneShoulder > 0.0f ||
            values.toneWhiteClip < 15.999f)
        {
            mask |= Bit(Effect::ToneShaping);
        }

        if (values.dofEnabled)
        {
            mask |= Bit(Effect::DepthOfField);
        }

        if (values.motionBlurEnabled)
        {
            mask |= Bit(Effect::MotionBlur);
        }

        if (values.depthFogEnabled)
        {
            mask |= Bit(Effect::DepthFog);
        }

        if (values.vignetteEnabled)
        {
            mask |= Bit(Effect::Vignette);
        }

        if (values.filmGrainEnabled)
        {
            mask |= Bit(Effect::FilmGrain);
        }

        if (values.chromaticAberrationEnabled)
        {
            mask |= Bit(Effect::ChromaticAberration);
        }

        if (values.denoiseEnabled)
        {
            mask |= Bit(Effect::Denoise);
        }

        if (values.sharpenEnabled)
        {
            mask |= Bit(Effect::Sharpen);
        }

        if (values.barsEnabled)
        {
            mask |= Bit(Effect::Bars);
        }

        if (values.ambientContactStrength > 0.0f)
        {
            mask |= Bit(Effect::AmbientContact);
        }

        if (values.horizonOcclusionEnabled)
        {
            mask |= Bit(Effect::HorizonOcclusion);
        }

        if (values.indirectBounceEnabled)
        {
            mask |= Bit(Effect::IndirectBounce);
        }

        if (values.localGlowEnabled)
        {
            mask |= Bit(Effect::LocalGlow);
        }

        if (values.shadowRecovery > 0.0f)
        {
            mask |= Bit(Effect::ShadowRecovery);
        }

        if (values.highlightRolloff > 0.0f)
        {
            mask |= Bit(Effect::HighlightRolloff);
        }

        if (values.clarityStrength > 0.0f)
        {
            mask |= Bit(Effect::Clarity);
        }

        if (values.halationStrength > 0.0f)
        {
            mask |= Bit(Effect::Halation);
        }

        if (values.shadowWarmth != 0.0f ||
            values.highlightWarmth != 0.0f)
        {
            mask |= Bit(Effect::SplitToning);
        }

        if (values.ditherStrength > 0.0f)
        {
            mask |= Bit(Effect::FineDither);
        }

        return mask;
    }

    inline float ExposureAlpha(
        const float speed,
        const float deltaSeconds) noexcept
    {
        return 1.0f -
            std::exp(
                -std::max(speed, 0.0f) *
                std::max(deltaSeconds, 0.0f));
    }

    inline float AdaptExposure(
        const float previous,
        const float target,
        const float speedUp,
        const float speedDown,
        const float deltaSeconds) noexcept
    {
        const float speed =
            target > previous
            ? speedUp
            : speedDown;

        const float alpha =
            std::clamp(
                ExposureAlpha(
                    speed,
                    deltaSeconds),
                0.0f,
                1.0f);

        return previous +
            (target - previous) *
            alpha;
    }

    inline float BloomSoftKneeWeight(
        const float luminance,
        const float threshold,
        const float softKnee) noexcept
    {
        const float knee =
            std::max(
                threshold * softKnee,
                1e-5f);

        const float soft =
            std::clamp(
                luminance - threshold + knee,
                0.0f,
                2.0f * knee);

        const float softContribution =
            (soft * soft) /
            (4.0f * knee + 1e-5f);

        const float hardContribution =
            std::max(
                luminance - threshold,
                0.0f);

        return std::max(
            hardContribution,
            softContribution) /
            std::max(luminance, 1e-5f);
    }

    inline constexpr std::array<
        const char*,
        6>
        kGraphStages
    {{
        "source_snapshot",
        "luminance_bloom",
        "pyramid_mips",
        "exposure_history",
        "product_composite",
        "scene_writeback"
    }};
}
