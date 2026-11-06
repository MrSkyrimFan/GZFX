// CommonLib public headers are intentionally PCH-dependent.
#include <SKSE/Impl/PCH.h>
using namespace std::literals;
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include "gzfx/engine/native_domain_anchors.hpp"

#include "gzfx/diagnostics/renderer_forensics.hpp"
#include "gzfx/diagnostics/telemetry_listener.hpp"
#include "gzfx/engine/commonlib_bridge.hpp"
#include "gzfx/paths.hpp"
#include "gzfx/skse_api.hpp"
#include "gzfx/version.hpp"

#include <Windows.h>
#include <MinHook.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace gzfx::engine::native_domain_anchors
{
    namespace fs = std::filesystem;

    namespace
    {
        using Anchor =
            gzfx::diagnostics::telemetry_listener::EngineAnchor;
        using Phase =
            gzfx::diagnostics::telemetry_listener::EnginePhase;

        using MainDepthFn = void(*)(bool, bool);
        using MainShadowMasksFn = void(*)(bool);
        using MainWorldFn = void(*)(bool);
        using MainWaterEffectsFn = void(*)();
        using RenderEffectsFn = void(*)(void*, std::uint32_t);

        using ImageRenderFn =
            void(*)(
                RE::ImageSpaceEffect*,
                RE::BSTriShape*,
                RE::ImageSpaceEffectParam*);

        using ImageDispatchFn =
            void(*)(
                RE::BSImagespaceShader*,
                std::uint32_t,
                std::uint32_t,
                std::uint32_t);

        using ShadowRenderFn =
            void(*)(
                RE::BSShadowLight*,
                std::uint32_t&);

        constexpr std::size_t kMaxEffectBindings = 24;
        constexpr std::size_t kMaxRenderTargets = 24;
        constexpr std::size_t kMaxDispatchTargets = 24;
        constexpr std::size_t kMaxShadowTargets = 8;

        struct EffectSpec
        {
            RE::ImageSpaceManager::ImageSpaceEffectEnum effect;
            Anchor anchor;
        };

        constexpr std::array<EffectSpec, 19> kEffects
        {{
            { RE::ImageSpaceManager::ISTemporalAA_Water, Anchor::WaterTaa },
            { RE::ImageSpaceManager::ISWaterBlend, Anchor::WaterBlend },
            { RE::ImageSpaceManager::ISUnderwaterMask, Anchor::UnderwaterMask },
            { RE::ImageSpaceManager::ISApplyReflections, Anchor::ApplyReflections },
            { RE::ImageSpaceManager::ISVolumetricLighting, Anchor::VolumetricLighting },
            { RE::ImageSpaceManager::ISApplyVolumetricLighting, Anchor::ApplyVolumetricLighting },
            { RE::ImageSpaceManager::ISVolumetricLightingBlurHCS, Anchor::VolumetricBlurH },
            { RE::ImageSpaceManager::ISVolumetricLightingBlurVCS, Anchor::VolumetricBlurV },
            { RE::ImageSpaceManager::ISCompositeVolumetricLighting, Anchor::CompositeVolumetricLighting },
            { RE::ImageSpaceManager::ISWaterFlow, Anchor::WaterFlow },
            { RE::ImageSpaceManager::ISWaterDisplacementClearSimulation, Anchor::WaterDisplacement },
            { RE::ImageSpaceManager::ISWaterDisplacementTexOffset, Anchor::WaterDisplacement },
            { RE::ImageSpaceManager::ISWaterDisplacementWadingRipple, Anchor::WaterDisplacement },
            { RE::ImageSpaceManager::ISWaterDisplacementRainRipple, Anchor::WaterDisplacement },
            { RE::ImageSpaceManager::ISWaterWadingHeightmap, Anchor::WaterDisplacement },
            { RE::ImageSpaceManager::ISWaterRainHeightmap, Anchor::WaterDisplacement },
            { RE::ImageSpaceManager::ISWaterBlendHeightmaps, Anchor::WaterDisplacement },
            { RE::ImageSpaceManager::ISWaterSmoothHeightmap, Anchor::WaterDisplacement },
            { RE::ImageSpaceManager::ISWaterDisplacementNormals, Anchor::WaterDisplacement }
        }};

        struct EffectBinding
        {
            RE::ImageSpaceEffect* effect = nullptr;
            RE::BSImagespaceShader* shader = nullptr;
            Anchor anchor = Anchor::PostProcessing;
        };

        struct RenderTargetBinding
        {
            void* target = nullptr;
            ImageRenderFn original = nullptr;
        };

        struct DispatchTargetBinding
        {
            void* target = nullptr;
            ImageDispatchFn original = nullptr;
        };

        struct ShadowTargetBinding
        {
            void* target = nullptr;
            ShadowRenderFn original = nullptr;
        };

        std::mutex g_mutex;

        std::atomic<bool> g_armed{false};
        std::atomic<bool> g_testBypass{false};
        std::atomic<std::uint64_t> g_events{0};
        std::atomic<std::uint64_t> g_lastFrame{0};

        std::uint32_t g_highLevelHooks = 0;
        std::uint32_t g_effectBindingCount = 0;
        std::uint32_t g_renderTargetCount = 0;
        std::uint32_t g_dispatchTargetCount = 0;
        std::uint32_t g_shadowTargetCount = 0;

        std::array<EffectBinding, kMaxEffectBindings>
            g_effectBindings{};
        std::array<RenderTargetBinding, kMaxRenderTargets>
            g_renderTargets{};
        std::array<DispatchTargetBinding, kMaxDispatchTargets>
            g_dispatchTargets{};
        std::array<ShadowTargetBinding, kMaxShadowTargets>
            g_shadowTargets{};

        std::string g_lastError;

        MainDepthFn g_mainDepth = nullptr;
        MainShadowMasksFn g_mainShadowMasks = nullptr;
        MainWorldFn g_mainWorld = nullptr;
        MainWaterEffectsFn g_mainWaterEffects = nullptr;
        RenderEffectsFn g_renderEffects = nullptr;

        void* g_mainDepthTarget = nullptr;
        void* g_mainShadowMasksTarget = nullptr;
        void* g_mainWorldTarget = nullptr;
        void* g_mainWaterEffectsTarget = nullptr;
        void* g_renderEffectsTarget = nullptr;

        std::uintptr_t ModuleBase() noexcept
        {
            try
            {
                return REL::Module::get().base();
            }
            catch (...)
            {
                return 0;
            }
        }

        std::uint64_t Rva(
            const void* const address) noexcept
        {
            const auto base = ModuleBase();
            const auto value =
                reinterpret_cast<std::uintptr_t>(
                    address);

            return base != 0 &&
                value >= base
                ? static_cast<std::uint64_t>(
                    value - base)
                : 0;
        }

        bool Executable(
            const void* const address) noexcept
        {
            if (address == nullptr)
            {
                return false;
            }

            MEMORY_BASIC_INFORMATION info{};

            if (VirtualQuery(
                    address,
                    &info,
                    sizeof(info)) == 0 ||
                info.State != MEM_COMMIT)
            {
                return false;
            }

            const DWORD protect =
                info.Protect &
                ~(PAGE_GUARD |
                  PAGE_NOCACHE |
                  PAGE_WRITECOMBINE);

            return
                protect == PAGE_EXECUTE ||
                protect == PAGE_EXECUTE_READ ||
                protect == PAGE_EXECUTE_READWRITE ||
                protect == PAGE_EXECUTE_WRITECOPY;
        }

        void Emit(
            const Anchor anchor,
            const Phase phase,
            const void* const target) noexcept
        {
            g_events.fetch_add(
                1,
                std::memory_order_relaxed);

            gzfx::diagnostics::telemetry_listener::
                OnEngineEvent(
                    gzfx::diagnostics::
                        CurrentRendererFrame(),
                    anchor,
                    phase,
                    GetCurrentThreadId(),
                    gzfx::skse::kRuntime_1_7_104,
                    Rva(target));
        }

        template <class Fn>
        bool HookTarget(
            void* const target,
            void* const detour,
            Fn& original)
        {
            if (!Executable(target) ||
                detour == nullptr)
            {
                return false;
            }

            void* trampoline = nullptr;

            const MH_STATUS created =
                MH_CreateHook(
                    target,
                    detour,
                    &trampoline);

            if (created != MH_OK)
            {
                return false;
            }

            const MH_STATUS enabled =
                MH_EnableHook(target);

            if (enabled != MH_OK &&
                enabled != MH_ERROR_ENABLED)
            {
                MH_RemoveHook(target);
                return false;
            }

            original =
                reinterpret_cast<Fn>(
                    trampoline);

            return original != nullptr;
        }

        void MainDepthThunk(
            const bool first,
            const bool second)
        {
            Emit(
                Anchor::MainDepth,
                Phase::Enter,
                g_mainDepthTarget);

            g_mainDepth(
                first,
                second);

            Emit(
                Anchor::MainDepth,
                Phase::Exit,
                g_mainDepthTarget);
        }

        void MainShadowMasksThunk(
            const bool value)
        {
            Emit(
                Anchor::MainShadowMasks,
                Phase::Enter,
                g_mainShadowMasksTarget);

            g_mainShadowMasks(value);

            Emit(
                Anchor::MainShadowMasks,
                Phase::Exit,
                g_mainShadowMasksTarget);
        }

        void MainWorldThunk(
            const bool value)
        {
            Emit(
                Anchor::MainWorld,
                Phase::Enter,
                g_mainWorldTarget);

            g_mainWorld(value);

            Emit(
                Anchor::MainWorld,
                Phase::Exit,
                g_mainWorldTarget);
        }

        void MainWaterEffectsThunk()
        {
            Emit(
                Anchor::MainWaterEffects,
                Phase::Enter,
                g_mainWaterEffectsTarget);

            g_mainWaterEffects();

            Emit(
                Anchor::MainWaterEffects,
                Phase::Exit,
                g_mainWaterEffectsTarget);
        }

        void RenderEffectsThunk(
            void* const accumulator,
            const std::uint32_t renderFlags)
        {
            Emit(
                Anchor::Effects,
                Phase::Enter,
                g_renderEffectsTarget);

            g_renderEffects(
                accumulator,
                renderFlags);

            Emit(
                Anchor::Effects,
                Phase::Exit,
                g_renderEffectsTarget);
        }

        const EffectBinding* FindEffect(
            const RE::ImageSpaceEffect* const effect) noexcept
        {
            for (std::uint32_t i = 0;
                 i < g_effectBindingCount;
                 ++i)
            {
                if (g_effectBindings[i].effect ==
                    effect)
                {
                    return &g_effectBindings[i];
                }
            }

            return nullptr;
        }

        const EffectBinding* FindShader(
            const RE::BSImagespaceShader* const shader) noexcept
        {
            for (std::uint32_t i = 0;
                 i < g_effectBindingCount;
                 ++i)
            {
                if (g_effectBindings[i].shader ==
                    shader)
                {
                    return &g_effectBindings[i];
                }
            }

            return nullptr;
        }

        ImageRenderFn FindRenderOriginal(
            const void* const target) noexcept
        {
            for (std::uint32_t i = 0;
                 i < g_renderTargetCount;
                 ++i)
            {
                if (g_renderTargets[i].target ==
                    target)
                {
                    return
                        g_renderTargets[i].original;
                }
            }

            return nullptr;
        }

        ImageDispatchFn FindDispatchOriginal(
            const void* const target) noexcept
        {
            for (std::uint32_t i = 0;
                 i < g_dispatchTargetCount;
                 ++i)
            {
                if (g_dispatchTargets[i].target ==
                    target)
                {
                    return
                        g_dispatchTargets[i].original;
                }
            }

            return nullptr;
        }

        ShadowRenderFn FindShadowOriginal(
            const void* const target) noexcept
        {
            for (std::uint32_t i = 0;
                 i < g_shadowTargetCount;
                 ++i)
            {
                if (g_shadowTargets[i].target ==
                    target)
                {
                    return
                        g_shadowTargets[i].original;
                }
            }

            return nullptr;
        }

        void ImageRenderThunk(
            RE::ImageSpaceEffect* const effect,
            RE::BSTriShape* const shape,
            RE::ImageSpaceEffectParam* const param)
        {
            auto** const vtable =
                effect != nullptr
                    ? *reinterpret_cast<void***>(
                        effect)
                    : nullptr;

            const void* const target =
                vtable != nullptr
                    ? vtable[1]
                    : nullptr;

            const auto original =
                FindRenderOriginal(target);

            const auto* const binding =
                FindEffect(effect);

            if (binding != nullptr)
            {
                Emit(
                    binding->anchor,
                    Phase::Enter,
                    target);
            }

            if (original != nullptr)
            {
                original(
                    effect,
                    shape,
                    param);
            }

            if (binding != nullptr)
            {
                Emit(
                    binding->anchor,
                    Phase::Exit,
                    target);
            }
        }

        void ImageDispatchThunk(
            RE::BSImagespaceShader* const shader,
            const std::uint32_t x,
            const std::uint32_t y,
            const std::uint32_t z)
        {
            auto** const vtable =
                shader != nullptr
                    ? *reinterpret_cast<void***>(
                        shader)
                    : nullptr;

            const void* const target =
                vtable != nullptr
                    ? vtable[0x0C]
                    : nullptr;

            const auto original =
                FindDispatchOriginal(target);

            const auto* const binding =
                FindShader(shader);

            if (binding != nullptr)
            {
                Emit(
                    binding->anchor,
                    Phase::Enter,
                    target);
            }

            if (original != nullptr)
            {
                original(
                    shader,
                    x,
                    y,
                    z);
            }

            if (binding != nullptr)
            {
                Emit(
                    binding->anchor,
                    Phase::Exit,
                    target);
            }
        }

        ShadowKind ClassifyShadow(
            RE::BSShadowLight* const light) noexcept
        {
            if (light == nullptr)
            {
                return ShadowKind::Unknown;
            }

            return ClassifyShadowFlags(
                light->GetIsDirectionalLight(),
                light->GetIsFrustumLight(),
                light->GetIsParabolicLight(),
                light->GetIsOmniLight());
        }

        Anchor ShadowAnchor(
            const ShadowKind kind) noexcept
        {
            switch (kind)
            {
            case ShadowKind::Directional:
                return Anchor::ShadowDirectional;
            case ShadowKind::Spot:
                return Anchor::ShadowSpot;
            case ShadowKind::Omni:
                return Anchor::ShadowOmni;
            default:
                return Anchor::ShadowUnknown;
            }
        }

        void ShadowRenderThunk(
            RE::BSShadowLight* const light,
            std::uint32_t& index)
        {
            auto** const vtable =
                light != nullptr
                    ? *reinterpret_cast<void***>(
                        light)
                    : nullptr;

            const void* const target =
                vtable != nullptr
                    ? vtable[0x0A]
                    : nullptr;

            const auto original =
                FindShadowOriginal(target);

            Emit(
                ShadowAnchor(
                    ClassifyShadow(light)),
                Phase::Enter,
                target);

            if (original != nullptr)
            {
                original(
                    light,
                    index);
            }

            Emit(
                ShadowAnchor(
                    ClassifyShadow(light)),
                Phase::Exit,
                target);
        }

        bool AddRenderTarget(
            void* const target)
        {
            if (!Executable(target))
            {
                return false;
            }

            for (std::uint32_t i = 0;
                 i < g_renderTargetCount;
                 ++i)
            {
                if (g_renderTargets[i].target ==
                    target)
                {
                    return true;
                }
            }

            if (g_renderTargetCount >=
                g_renderTargets.size())
            {
                return false;
            }

            ImageRenderFn original = nullptr;

            if (!HookTarget(
                    target,
                    reinterpret_cast<void*>(
                        &ImageRenderThunk),
                    original))
            {
                return false;
            }

            g_renderTargets[
                g_renderTargetCount++] =
            {
                target,
                original
            };

            return true;
        }

        bool AddDispatchTarget(
            void* const target)
        {
            if (!Executable(target))
            {
                return false;
            }

            for (std::uint32_t i = 0;
                 i < g_dispatchTargetCount;
                 ++i)
            {
                if (g_dispatchTargets[i].target ==
                    target)
                {
                    return true;
                }
            }

            if (g_dispatchTargetCount >=
                g_dispatchTargets.size())
            {
                return false;
            }

            ImageDispatchFn original = nullptr;

            if (!HookTarget(
                    target,
                    reinterpret_cast<void*>(
                        &ImageDispatchThunk),
                    original))
            {
                return false;
            }

            g_dispatchTargets[
                g_dispatchTargetCount++] =
            {
                target,
                original
            };

            return true;
        }

        bool AddShadowTarget(
            RE::BSShadowLight* const light)
        {
            if (light == nullptr)
            {
                return false;
            }

            auto** const vtable =
                *reinterpret_cast<void***>(
                    light);

            if (vtable == nullptr)
            {
                return false;
            }

            void* const target =
                vtable[0x0A];

            if (!Executable(target))
            {
                return false;
            }

            for (std::uint32_t i = 0;
                 i < g_shadowTargetCount;
                 ++i)
            {
                if (g_shadowTargets[i].target ==
                    target)
                {
                    return true;
                }
            }

            if (g_shadowTargetCount >=
                g_shadowTargets.size())
            {
                return false;
            }

            ShadowRenderFn original = nullptr;

            if (!HookTarget(
                    target,
                    reinterpret_cast<void*>(
                        &ShadowRenderThunk),
                    original))
            {
                return false;
            }

            g_shadowTargets[
                g_shadowTargetCount++] =
            {
                target,
                original
            };

            return true;
        }

        bool InstallEffect(
            const EffectSpec& spec)
        {
            auto* const manager =
                RE::ImageSpaceManager::
                    GetSingleton();

            if (manager == nullptr)
            {
                return false;
            }

            const auto index =
                RE::ImageSpaceManager::
                    GetCurrentIndex(
                        spec.effect);

            if (index ==
                    RE::ImageSpaceManager::
                        INVALID_INDEX ||
                index >= manager->effects.capacity())
            {
                return false;
            }

            auto* const effect =
                manager->effects[index];

            if (effect == nullptr)
            {
                return false;
            }

            for (std::uint32_t i = 0;
                 i < g_effectBindingCount;
                 ++i)
            {
                if (g_effectBindings[i].effect ==
                    effect)
                {
                    return true;
                }
            }

            if (g_effectBindingCount >=
                g_effectBindings.size())
            {
                return false;
            }

            auto** const effectVtable =
                *reinterpret_cast<void***>(
                    effect);

            if (effectVtable == nullptr ||
                !AddRenderTarget(
                    effectVtable[1]))
            {
                return false;
            }

            auto* const shader =
                RE::skyrim_cast<
                    RE::BSImagespaceShader*>(
                        effect);

            if (shader != nullptr)
            {
                auto** const shaderVtable =
                    *reinterpret_cast<void***>(
                        shader);

                if (shaderVtable != nullptr)
                {
                    AddDispatchTarget(
                        shaderVtable[0x0C]);
                }
            }

            g_effectBindings[
                g_effectBindingCount++] =
            {
                effect,
                shader,
                spec.anchor
            };

            return true;
        }

        void InstallImageEffects()
        {
            for (const auto& spec :
                 kEffects)
            {
                InstallEffect(spec);
            }
        }

        void InstallShadowTargets()
        {
            auto& world =
                RE::DrawWorld::GetSingleton();

            auto* const node =
                world.mainShadowSceneNode;

            if (node == nullptr)
            {
                return;
            }

            auto& data =
                node->GetRuntimeData();

            AddShadowTarget(
                data.sunShadowDirLight);

            for (auto& pointer :
                 data.activeShadowLights)
            {
                AddShadowTarget(
                    pointer.get());
            }
        }

        template <class Fn>
        bool InstallRelocationHook(
            const REL::RelocationID id,
            void* const detour,
            Fn& original,
            void*& targetOut)
        {
            try
            {
                REL::Relocation<Fn> target{id};

                targetOut =
                    reinterpret_cast<void*>(
                        target.address());

                if (!HookTarget(
                        targetOut,
                        detour,
                        original))
                {
                    return false;
                }

                ++g_highLevelHooks;
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        void InstallHighLevelHooks()
        {
            InstallRelocationHook(
                REL::RelocationID(
                    100421,
                    107139),
                reinterpret_cast<void*>(
                    &MainDepthThunk),
                g_mainDepth,
                g_mainDepthTarget);

            InstallRelocationHook(
                REL::RelocationID(
                    100422,
                    107140),
                reinterpret_cast<void*>(
                    &MainShadowMasksThunk),
                g_mainShadowMasks,
                g_mainShadowMasksTarget);

            InstallRelocationHook(
                REL::RelocationID(
                    100424,
                    107142),
                reinterpret_cast<void*>(
                    &MainWorldThunk),
                g_mainWorld,
                g_mainWorldTarget);

            InstallRelocationHook(
                REL::RelocationID(
                    35561,
                    36560),
                reinterpret_cast<void*>(
                    &MainWaterEffectsThunk),
                g_mainWaterEffects,
                g_mainWaterEffectsTarget);

            InstallRelocationHook(
                REL::RelocationID(
                    99940,
                    106585),
                reinterpret_cast<void*>(
                    &RenderEffectsThunk),
                g_renderEffects,
                g_renderEffectsTarget);
        }

        void EnsureRoot(
            fs::path& root)
        {
            root =
                gzfx::paths::
                    DiagnosticsRoot();

            if (root.empty())
            {
                return;
            }

            std::error_code error;
            fs::create_directories(
                root,
                error);

            if (error)
            {
                root.clear();
            }
        }

        void WriteStatus(
            const char* const event)
        {
            fs::path root;
            EnsureRoot(root);

            if (root.empty())
            {
                return;
            }

            std::ofstream out(
                root /
                    L"native_domain_anchors.txt",
                std::ios::out |
                    std::ios::trunc);

            if (!out)
            {
                return;
            }

            out
                << "GZFX native domain anchors\n"
                << "version="
                << gzfx::kVersion
                << "\n"
                << "event="
                << (event != nullptr
                    ? event
                    : "unknown")
                << "\n"
                << "armed="
                << (g_armed.load()
                    ? "true"
                    : "false")
                << "\n"
                << "test_bypass="
                << (g_testBypass.load()
                    ? "true"
                    : "false")
                << "\n"
                << "high_level_hooks="
                << g_highLevelHooks
                << "\n"
                << "image_effects_bound="
                << g_effectBindingCount
                << "\n"
                << "render_targets_hooked="
                << g_renderTargetCount
                << "\n"
                << "dispatch_targets_hooked="
                << g_dispatchTargetCount
                << "\n"
                << "shadow_targets_hooked="
                << g_shadowTargetCount
                << "\n"
                << "events="
                << g_events.load()
                << "\n"
                << "last_frame="
                << g_lastFrame.load()
                << "\n"
                << "last_error="
                << g_lastError
                << "\n";
        }

        void WriteSnapshot(
            const std::uint64_t frame)
        {
            fs::path root;
            EnsureRoot(root);

            if (root.empty())
            {
                return;
            }

            std::ofstream out(
                root /
                    L"native_domain_snapshot.txt",
                std::ios::out |
                    std::ios::trunc);

            if (!out)
            {
                return;
            }

            const auto engine =
                gzfx::engine::commonlib::
                    GetSnapshot();

            auto& world =
                RE::DrawWorld::GetSingleton();

            auto* const node =
                world.mainShadowSceneNode;

            std::uint32_t activeShadowLights = 0;
            std::uint32_t shadowLightsAccum = 0;
            std::uint32_t sunShadowMaps = 0;
            std::uint32_t sunShadowDescriptors = 0;
            std::uint32_t sunMaskIndex = 0;

            if (node != nullptr)
            {
                auto& data =
                    node->GetRuntimeData();

                activeShadowLights =
                    static_cast<std::uint32_t>(
                        data.activeShadowLights.size());

                shadowLightsAccum =
                    static_cast<std::uint32_t>(
                        data.shadowLightsAccum.size());

                if (data.sunShadowDirLight !=
                    nullptr)
                {
                    auto* const sun =
                        data.sunShadowDirLight;

                    sunShadowMaps =
                        sun->shadowMapCount;

                    auto& lightData =
                        sun->GetRuntimeData();

                    sunMaskIndex =
                        lightData.maskIndex;

                    sunShadowDescriptors =
                        static_cast<
                            std::uint32_t>(
                            lightData.
                                shadowmapDescriptors.
                                size());
                }
            }

            out
                << "GZFX native domain snapshot\n"
                << "version=" << gzfx::kVersion << "\n"
                << "frame=" << frame << "\n"
                << "events=" << g_events.load() << "\n"
                << "draw_world_active_shadow_lights="
                << world.activeShadowLightCount << "\n"
                << "draw_world_shadow_lights="
                << world.shadowLightCount << "\n"
                << "draw_world_disable_sun_shadows="
                << (world.disableSunShadows
                    ? "true"
                    : "false")
                << "\n"
                << "node_active_shadow_lights="
                << activeShadowLights << "\n"
                << "node_shadow_lights_accum="
                << shadowLightsAccum << "\n"
                << "sun_shadow_map_count="
                << sunShadowMaps << "\n"
                << "sun_shadow_descriptor_count="
                << sunShadowDescriptors << "\n"
                << "sun_shadow_mask_index="
                << sunMaskIndex << "\n"
                << "current_weather=0x"
                << std::hex << std::uppercase
                << engine.currentWeather << "\n"
                << "last_weather=0x"
                << engine.lastWeather << "\n"
                << "sun=0x"
                << engine.sun << "\n"
                << "clouds=0x"
                << engine.clouds << "\n"
                << "masser=0x"
                << engine.masser << "\n"
                << "secunda=0x"
                << engine.secunda << "\n"
                << "precipitation=0x"
                << engine.precipitation
                << std::dec << "\n"
                << "game_hour="
                << engine.gameHour << "\n"
                << "weather_blend="
                << engine.weatherBlend << "\n"
                << "wind_speed="
                << engine.windSpeed << "\n"
                << "wind_angle="
                << engine.windAngle << "\n"
                << "fog_near="
                << engine.fogNear << "\n"
                << "fog_far="
                << engine.fogFar << "\n"
                << "fog_power="
                << engine.fogPower << "\n"
                << "fog_clamp="
                << engine.fogClamp << "\n"
                << "sky_mode="
                << engine.skyMode << "\n";
        }
    }

    bool ArmAfterDataLoaded() noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (g_armed.load(
                    std::memory_order_acquire))
            {
                return true;
            }

            if (gzfx::paths::
                    TestOverrideActive())
            {
                g_testBypass.store(
                    true,
                    std::memory_order_release);

                g_armed.store(
                    true,
                    std::memory_order_release);

                WriteStatus(
                    "test_bypass");

                return true;
            }

            const auto engine =
                gzfx::engine::commonlib::
                    GetSnapshot();

            if (engine.status !=
                    gzfx::engine::commonlib::
                        Status::Bound ||
                !engine.addressLibraryPresent)
            {
                g_lastError =
                    "commonlib/address library not ready";

                WriteStatus(
                    "not_ready");

                return false;
            }

            const MH_STATUS init =
                MH_Initialize();

            if (init != MH_OK &&
                init != MH_ERROR_ALREADY_INITIALIZED)
            {
                g_lastError =
                    "minhook initialization failed";

                WriteStatus(
                    "minhook_failed");

                return false;
            }

            InstallHighLevelHooks();
            InstallImageEffects();
            InstallShadowTargets();

            g_armed.store(
                true,
                std::memory_order_release);

            WriteStatus(
                "armed");

            return true;
        }
        catch (...)
        {
            g_lastError =
                "arm exception";

            WriteStatus(
                "exception");

            return false;
        }
    }

    void OnPresent(
        const std::uint64_t frame) noexcept
    {
        try
        {
            if (!g_armed.load(
                    std::memory_order_acquire) ||
                g_testBypass.load(
                    std::memory_order_acquire))
            {
                return;
            }

            g_lastFrame.store(
                frame,
                std::memory_order_release);

            if (frame <= 3 ||
                (frame % 120) == 0)
            {
                std::scoped_lock lock(
                    g_mutex);

                InstallImageEffects();
                InstallShadowTargets();

                WriteSnapshot(frame);
                WriteStatus(
                    "present_snapshot");
            }
        }
        catch (...)
        {
        }
    }

    Status GetStatus() noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            Status status{};
            status.armed =
                g_armed.load();
            status.testBypass =
                g_testBypass.load();
            status.highLevelHooks =
                g_highLevelHooks;
            status.imageEffectsBound =
                g_effectBindingCount;
            status.renderTargetsHooked =
                g_renderTargetCount;
            status.dispatchTargetsHooked =
                g_dispatchTargetCount;
            status.shadowTargetsHooked =
                g_shadowTargetCount;
            status.events =
                g_events.load();
            status.lastFrame =
                g_lastFrame.load();

            return status;
        }
        catch (...)
        {
            return {};
        }
    }
}
