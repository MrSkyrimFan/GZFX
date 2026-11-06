#pragma once

#include <cstdint>
#include <string>

namespace gzfx::settings
{
    enum class Preset : std::uint32_t
    {
        Custom = 0,
        Clearfell,
        Emberveil,
        Moonforge,
        VerdantNorth,
        SableCinema,
        Count
    };

    struct Values
    {
        // Current ambient foundation.
        float ambientContactStrength = 0.62f;
        float ambientContactRadiusPixels = 4.0f;

        bool horizonOcclusionEnabled = true;
        float horizonOcclusionStrength = 0.25f;
        float horizonOcclusionRadiusPixels = 28.0f;
        std::uint32_t horizonOcclusionQuality = 2;

        bool indirectBounceEnabled = true;
        float indirectBounceStrength = 0.12f;
        float indirectBounceRadiusPixels = 36.0f;
        float indirectBounceSaturation = 1.00f;

        bool localGlowEnabled = true;
        float localGlowStrength = 0.18f;
        float localGlowThreshold = 1.10f;
        float localGlowRadiusPixels = 8.0f;

        // Unified product renderer.
        bool productEnabled = true;
        std::uint32_t productQuality = 1;
        std::uint32_t productPreset =
            static_cast<std::uint32_t>(Preset::Custom);
        std::uint32_t productCustomSlot = 1;

        bool exposureEnabled = false;
        bool autoExposureEnabled = false;
        float manualExposureEv = 0.0f;
        float exposureCompensationEv = 0.0f;
        float exposureKey = 0.18f;
        float exposureMinEv = -8.0f;
        float exposureMaxEv = 8.0f;
        float exposureSpeedUp = 2.5f;
        float exposureSpeedDown = 1.5f;

        bool bloomEnabled = false;
        float bloomStrength = 0.0f;
        float bloomThreshold = 1.0f;
        float bloomSoftKnee = 0.5f;
        float bloomRadius = 4.0f;
        bool lensDirtEnabled = false;
        float lensDirtStrength = 0.0f;

        bool colorEnabled = false;
        float colorContrast = 1.0f;
        float colorGamma = 1.0f;
        float colorSaturation = 1.0f;
        float colorVibrance = 0.0f;
        float colorTemperature = 0.0f;
        float colorTint = 0.0f;
        float colorLift = 0.0f;
        float colorGain = 1.0f;

        float toneToe = 0.0f;
        float toneShoulder = 0.0f;
        float toneWhiteClip = 16.0f;

        bool dofEnabled = false;
        float dofFocusDistance = 2500.0f;
        float dofFocusRange = 800.0f;
        float dofNearStrength = 0.0f;
        float dofFarStrength = 0.0f;
        float dofMaxRadius = 0.0f;

        bool motionBlurEnabled = false;
        float motionBlurStrength = 0.0f;
        float motionBlurMaxPixels = 24.0f;

        bool depthFogEnabled = false;
        float depthFogStart = 2000.0f;
        float depthFogEnd = 15000.0f;
        float depthFogDensity = 0.0f;
        float depthFogColorR = 0.55f;
        float depthFogColorG = 0.62f;
        float depthFogColorB = 0.70f;

        bool vignetteEnabled = false;
        float vignetteStrength = 0.0f;
        float vignetteRadius = 0.72f;
        float vignetteSoftness = 0.28f;

        bool filmGrainEnabled = false;
        float filmGrainStrength = 0.0f;
        float filmGrainScale = 1.0f;

        bool chromaticAberrationEnabled = false;
        float chromaticAberrationStrength = 0.0f;

        bool denoiseEnabled = false;
        float denoiseStrength = 0.0f;

        bool sharpenEnabled = false;
        float sharpenStrength = 0.0f;
        float sharpenClamp = 0.25f;

        bool barsEnabled = false;
        float letterboxAmount = 0.0f;
        float pillarboxAmount = 0.0f;

        // Development / diagnostics.
        std::uint32_t defaultMode = 1;
        std::uint64_t activationDrawIndexedThreshold = 1000;
        bool accessibleDiagnostics = true;
        bool captureOnModeChange = true;
    };

    enum class Editable : std::uint32_t
    {
        ProductEnabled = 0,
        ProductQuality,
        ProductPreset,
        ProductCustomSlot,
        ApplyPresetAction,
        LoadCustomAction,
        SaveCustomAction,

        AmbientContactStrength,
        AmbientContactRadiusPixels,
        HorizonOcclusionEnabled,
        HorizonOcclusionStrength,
        HorizonOcclusionRadiusPixels,
        HorizonOcclusionQuality,
        IndirectBounceEnabled,
        IndirectBounceStrength,
        IndirectBounceRadiusPixels,
        IndirectBounceSaturation,
        LocalGlowEnabled,
        LocalGlowStrength,
        LocalGlowThreshold,
        LocalGlowRadiusPixels,

        ExposureEnabled,
        AutoExposureEnabled,
        ManualExposureEv,
        ExposureCompensationEv,
        ExposureKey,
        ExposureMinEv,
        ExposureMaxEv,
        ExposureSpeedUp,
        ExposureSpeedDown,

        BloomEnabled,
        BloomStrength,
        BloomThreshold,
        BloomSoftKnee,
        BloomRadius,
        LensDirtEnabled,
        LensDirtStrength,

        ColorEnabled,
        ColorContrast,
        ColorGamma,
        ColorSaturation,
        ColorVibrance,
        ColorTemperature,
        ColorTint,
        ColorLift,
        ColorGain,
        ToneToe,
        ToneShoulder,
        ToneWhiteClip,

        DofEnabled,
        DofFocusDistance,
        DofFocusRange,
        DofNearStrength,
        DofFarStrength,
        DofMaxRadius,

        MotionBlurEnabled,
        MotionBlurStrength,
        MotionBlurMaxPixels,

        DepthFogEnabled,
        DepthFogStart,
        DepthFogEnd,
        DepthFogDensity,
        DepthFogColorR,
        DepthFogColorG,
        DepthFogColorB,

        VignetteEnabled,
        VignetteStrength,
        VignetteRadius,
        VignetteSoftness,
        FilmGrainEnabled,
        FilmGrainStrength,
        FilmGrainScale,
        ChromaticAberrationEnabled,
        ChromaticAberrationStrength,
        DenoiseEnabled,
        DenoiseStrength,
        SharpenEnabled,
        SharpenStrength,
        SharpenClamp,
        BarsEnabled,
        LetterboxAmount,
        PillarboxAmount,

        DefaultMode,
        ActivationDrawIndexedThreshold,
        AccessibleDiagnostics,
        CaptureOnModeChange,
        Count
    };

    const Values& Get() noexcept;
    std::uint64_t Revision() noexcept;
    bool Reload() noexcept;

    bool Adjust(Editable setting, int direction) noexcept;
    bool SetNormalized(Editable setting, float normalized) noexcept;
    float GetNormalized(Editable setting) noexcept;
    bool Toggle(Editable setting) noexcept;
    bool Activate(Editable setting) noexcept;

    bool IsToggle(Editable setting) noexcept;
    bool IsSlider(Editable setting) noexcept;
    bool IsAction(Editable setting) noexcept;

    bool ApplyPreset(Preset preset) noexcept;
    bool SaveCustom(std::uint32_t slot) noexcept;
    bool LoadCustom(std::uint32_t slot) noexcept;

    const char* PresetName(Preset preset) noexcept;
    const wchar_t* EditableName(Editable setting) noexcept;
    std::wstring EditableValue(Editable setting) noexcept;
}
