#include "gzfx/render/scene_effects.hpp"

#include "gzfx/paths.hpp"
#include "gzfx/render/ambient_depth_stack.hpp"
#include "gzfx/render/ambient_visibility_v2.hpp"
#include "gzfx/render/product_control_mapping.hpp"
#include "gzfx/render/runtime_state.hpp"
#include "gzfx/render/unified_product_math.hpp"
#include "gzfx/render/unified_product_renderer.hpp"
#include "gzfx/settings.hpp"
#include "gzfx/version.hpp"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace gzfx::render::scene_effects
{
    namespace fs = std::filesystem;

    namespace
    {
        std::mutex g_mutex;
        std::uint64_t g_injectionCount = 0;
        std::uint64_t g_lastFrame = 0;
        std::uint64_t g_lastControlProofFrame = 0;

        void EnsureRoot(
            fs::path& root)
        {
            root =
                gzfx::paths::DiagnosticsRoot();

            if (root.empty())
            {
                return;
            }

            std::error_code error;
            fs::create_directories(root, error);

            if (error)
            {
                root.clear();
            }
        }

        void WriteLog(
            const std::uint64_t frame,
            const std::uint64_t drawOrdinal,
            const char* status,
            const bool indirectApplied,
            const bool unifiedApplied,
            const std::string& detail = {})
        {
            fs::path root;
            EnsureRoot(root);

            if (root.empty())
            {
                return;
            }

            std::ofstream out(
                root / L"pre_ui_effects.txt",
                std::ios::out |
                    std::ios::trunc);

            if (!out)
            {
                return;
            }

            const auto values =
                gzfx::settings::Get();

            const auto unifiedStatus =
                gzfx::render::
                    unified_product_renderer::
                    GetStatus();

            out
                << "GZFX integrated HDR scene stack\n"
                << "version=" << gzfx::kVersion << "\n"
                << "status=" << status << "\n"
                << "frame=" << frame << "\n"
                << "draw_ordinal=" << drawOrdinal << "\n"
                << "injections=" << g_injectionCount << "\n"
                << "mode="
                << gzfx::render::state::
                    GetDevelopmentMode()
                << "\n"
                << "settings_revision="
                << gzfx::settings::Revision() << "\n"
                << "product_enabled="
                << (values.productEnabled
                    ? "true"
                    : "false")
                << "\n"
                << "ambient_contact_strength="
                << values.ambientContactStrength << "\n"
                << "ambient_contact_radius="
                << values.ambientContactRadiusPixels << "\n"
                << "local_glow_enabled="
                << (values.localGlowEnabled
                    ? "true"
                    : "false")
                << "\n"
                << "local_glow_strength="
                << values.localGlowStrength << "\n"
                << "local_glow_threshold="
                << values.localGlowThreshold << "\n"
                << "local_glow_radius="
                << values.localGlowRadiusPixels << "\n"
                << "indirect_stack_applied="
                << (indirectApplied
                    ? "true"
                    : "false")
                << "\n"
                << "unified_product_renderer_applied="
                << (unifiedApplied
                    ? "true"
                    : "false")
                << "\n"
                << "unified_effect_mask="
                << unifiedStatus.activeEffectMask << "\n"
                << "unified_settings_revision="
                << unifiedStatus.settingsRevision << "\n"
                << "source_copy_count="
                << (unifiedStatus.sourceCopyCountOne
                    ? 1
                    : 0)
                << "\n"
                << "scene_writeback_count="
                << (unifiedStatus.sceneWritebackCountOne
                    ? 1
                    : 0)
                << "\n";

            if (!detail.empty())
            {
                out << "detail=" << detail << "\n";
            }
        }

        void WriteProductControlSubmission(
            const std::uint64_t frame,
            const bool wave4OwnsAo,
            const bool indirectApplied,
            const bool unifiedApplied)
        {
            if (g_lastControlProofFrame != 0 &&
                frame > g_lastControlProofFrame &&
                frame - g_lastControlProofFrame < 120)
            {
                return;
            }

            fs::path root;
            EnsureRoot(root);

            if (root.empty())
            {
                return;
            }

            std::ofstream out(
                root /
                    L"product_control_submission.txt",
                std::ios::out |
                    std::ios::trunc);

            if (!out)
            {
                return;
            }

            const auto values =
                gzfx::settings::Get();

            const auto quality =
                gzfx::render::product_control::
                    HorizonBudget(
                        values.
                            horizonOcclusionQuality);

            const auto unifiedStatus =
                gzfx::render::
                    unified_product_renderer::
                    GetStatus();

            out
                << "GZFX product control submission\n"
                << "version=" << gzfx::kVersion << "\n"
                << "frame=" << frame << "\n"
                << "mode="
                << gzfx::render::state::
                    GetDevelopmentMode()
                << "\n"
                << "settings_revision="
                << gzfx::settings::Revision() << "\n"
                << "product_enabled="
                << (values.productEnabled
                    ? "true"
                    : "false")
                << "\n"
                << "product_quality="
                << values.productQuality << "\n"
                << "product_preset="
                << gzfx::settings::PresetName(
                    static_cast<gzfx::settings::Preset>(
                        values.productPreset))
                << "\n"
                << "ambient_contact_strength="
                << values.ambientContactStrength << "\n"
                << "ambient_contact_radius="
                << values.ambientContactRadiusPixels << "\n"
                << "horizon_enabled="
                << (values.horizonOcclusionEnabled
                    ? "true"
                    : "false")
                << "\n"
                << "horizon_strength_setting="
                << values.horizonOcclusionStrength << "\n"
                << "horizon_strength_multiplier="
                << gzfx::render::product_control::
                    HorizonStrengthMultiplier(
                        values.
                            horizonOcclusionStrength)
                << "\n"
                << "horizon_radius_setting="
                << values.horizonOcclusionRadiusPixels << "\n"
                << "horizon_radius_world="
                << gzfx::render::product_control::
                    HorizonRadiusWorld(
                        values.
                            horizonOcclusionRadiusPixels)
                << "\n"
                << "horizon_quality="
                << values.horizonOcclusionQuality << "\n"
                << "horizon_slices="
                << quality.slices << "\n"
                << "horizon_steps="
                << quality.steps << "\n"
                << "indirect_enabled="
                << (values.indirectBounceEnabled
                    ? "true"
                    : "false")
                << "\n"
                << "indirect_strength="
                << values.indirectBounceStrength << "\n"
                << "indirect_radius="
                << values.indirectBounceRadiusPixels << "\n"
                << "indirect_saturation="
                << values.indirectBounceSaturation << "\n"
                << "local_glow_enabled="
                << (values.localGlowEnabled
                    ? "true"
                    : "false")
                << "\n"
                << "local_glow_strength="
                << values.localGlowStrength << "\n"
                << "local_glow_threshold="
                << values.localGlowThreshold << "\n"
                << "local_glow_radius="
                << values.localGlowRadiusPixels << "\n"
                << "wave4_owns_ao="
                << (wave4OwnsAo
                    ? "true"
                    : "false")
                << "\n"
                << "av2_applied_current_frame="
                << (gzfx::render::
                        ambient_visibility_v2::
                        WasAppliedFrame(frame)
                    ? "true"
                    : "false")
                << "\n"
                << "indirect_stack_applied="
                << (indirectApplied
                    ? "true"
                    : "false")
                << "\n"
                << "unified_product_renderer_applied="
                << (unifiedApplied
                    ? "true"
                    : "false")
                << "\n"
                << "unified_effect_mask="
                << unifiedStatus.activeEffectMask << "\n"
                << "source_copy_count="
                << (unifiedStatus.sourceCopyCountOne
                    ? 1
                    : 0)
                << "\n"
                << "scene_writeback_count="
                << (unifiedStatus.sceneWritebackCountOne
                    ? 1
                    : 0)
                << "\n"
                << "scene_effects_injected="
                << (unifiedApplied
                    ? "true"
                    : "false")
                << "\n";

            g_lastControlProofFrame = frame;
        }
    }

    bool WasInjectedFrame(
        const std::uint64_t frame) noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);
            return g_lastFrame == frame;
        }
        catch (...)
        {
            return false;
        }
    }

    bool ApplyPreUi(
        const std::uint64_t frame,
        const std::uint64_t drawOrdinal,
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        const gzfx::diagnostics::
            resource_intelligence::
            LiveInputs& inputs) noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);

            const auto values =
                gzfx::settings::Get();

            const std::uint32_t mode =
                gzfx::render::state::
                    GetDevelopmentMode();

            const bool wave4OwnsAo =
                gzfx::render::
                    ambient_visibility_v2::
                    OwnsDevelopmentMode(mode);

            if (mode != 1 &&
                mode != 2)
            {
                WriteLog(
                    frame,
                    drawOrdinal,
                    "skipped_mode",
                    false,
                    false);

                return false;
            }

            if (!values.productEnabled)
            {
                WriteLog(
                    frame,
                    drawOrdinal,
                    "skipped_product_disabled",
                    false,
                    false);

                return false;
            }

            if (device == nullptr ||
                context == nullptr ||
                inputs.sceneColorTexture == nullptr ||
                inputs.depthTexture == nullptr ||
                inputs.cameraBuffer == nullptr)
            {
                WriteLog(
                    frame,
                    drawOrdinal,
                    "skipped_missing_inputs",
                    false,
                    false);

                return false;
            }

            const bool ambientDepthApplied =
                gzfx::render::ambient_depth::Apply(
                    frame,
                    device,
                    context,
                    inputs,
                    !wave4OwnsAo);

            const bool unifiedApplied =
                gzfx::render::
                    unified_product_renderer::Apply(
                        {
                            frame,
                            drawOrdinal,
                            device,
                            context,
                            &inputs,
                            ambientDepthApplied,
                            gzfx::render::
                                ambient_visibility_v2::
                                WasAppliedFrame(frame)
                        });

            if (!unifiedApplied)
            {
                WriteLog(
                    frame,
                    drawOrdinal,
                    "skipped_unified_renderer",
                    ambientDepthApplied,
                    false);

                WriteProductControlSubmission(
                    frame,
                    wave4OwnsAo,
                    ambientDepthApplied,
                    false);

                return false;
            }

            ++g_injectionCount;
            g_lastFrame = frame;

            WriteLog(
                frame,
                drawOrdinal,
                "pre_ui_injected",
                ambientDepthApplied,
                true);

            WriteProductControlSubmission(
                frame,
                wave4OwnsAo,
                ambientDepthApplied,
                true);

            return true;
        }
        catch (...)
        {
            WriteLog(
                frame,
                drawOrdinal,
                "exception",
                false,
                false);

            return false;
        }
    }
}
