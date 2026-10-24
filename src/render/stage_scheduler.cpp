#include "gzfx/render/stage_scheduler.hpp"

#include "gzfx/paths.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace gzfx::render::stage_scheduler
{
    namespace fs = std::filesystem;

    namespace
    {
        struct CallbackEntry
        {
            Callback callback = nullptr;
            void* user = nullptr;
        };

        struct CapturedEvent
        {
            Boundary boundary = Boundary::Unknown;
            Phase phase = Phase::Landmark;
            std::uint64_t frame = 0;
            std::uint64_t ordinal = 0;
            std::uint64_t sequence = 0;
            std::string reason;
        };

        std::mutex g_mutex;
        std::array<
            std::vector<CallbackEntry>,
            static_cast<std::size_t>(Boundary::Count)>
                g_callbacks{};
        std::array<
            std::uint64_t,
            static_cast<std::size_t>(Boundary::Count)>
                g_counts{};
        std::vector<CapturedEvent> g_captureEvents;
        std::uint64_t g_sequence = 0;
        std::uint64_t g_captureStart = 0;
        std::uint64_t g_captureEnd = 0;
        bool g_captureActive = false;

        std::size_t Index(const Boundary boundary)
        {
            return static_cast<std::size_t>(boundary);
        }

        void WriteCaptureLocked(
            const std::wstring& suffix)
        {
            const fs::path root =
                gzfx::paths::DiagnosticsRoot();

            if (root.empty())
            {
                return;
            }

            std::error_code error;
            fs::create_directories(root, error);

            if (error)
            {
                return;
            }

            std::ofstream out(
                root /
                    (
                        std::wstring(L"stage_schedule") +
                        suffix +
                        L".csv"
                    ),
                std::ios::out |
                    std::ios::trunc);

            if (!out)
            {
                return;
            }

            out
                << "sequence,frame,ordinal,boundary,phase,reason\n";

            for (const auto& event : g_captureEvents)
            {
                out
                    << event.sequence << ","
                    << event.frame << ","
                    << event.ordinal << ","
                    << BoundaryName(event.boundary) << ","
                    << PhaseName(event.phase) << ","
                    << event.reason << "\n";
            }
        }
    }

    const char* BoundaryName(
        const Boundary boundary) noexcept
    {
        switch (boundary)
        {
        case Boundary::AfterShadowProduction:
            return "after_shadow_production";
        case Boundary::AfterDepthPrepass:
            return "after_depth_prepass";
        case Boundary::AfterOpaqueGeometryFinalDepth:
            return "after_opaque_geometry_final_depth";
        case Boundary::AfterCameraZ:
            return "after_cameraz";
        case Boundary::AfterStockAO:
            return "after_stock_ao";
        case Boundary::BeforeIndirectLightingComposite:
            return "before_indirect_lighting_composite";
        case Boundary::AtLightingComposite:
            return "at_lighting_composite";
        case Boundary::AfterLightingComposite:
            return "after_lighting_composite";
        case Boundary::BeforeVolumetricComposite:
            return "before_volumetric_composite";
        case Boundary::AfterVolumetricComposite:
            return "after_volumetric_composite";
        case Boundary::WaterDomain:
            return "water_domain";
        case Boundary::BeforeGlobalTAA:
            return "before_global_taa";
        case Boundary::AfterGlobalTAA:
            return "after_global_taa";
        case Boundary::HDRReduction:
            return "hdr_reduction";
        case Boundary::BeforeTonemap:
            return "before_tonemap";
        case Boundary::PostTonemapPreUi:
            return "post_tonemap_pre_ui";
        case Boundary::UiPresent:
            return "ui_present";
        case Boundary::Unknown:
        case Boundary::Count:
        default:
            return "unknown";
        }
    }

    const char* PhaseName(
        const Phase phase) noexcept
    {
        switch (phase)
        {
        case Phase::BeforeExecution:
            return "before";
        case Phase::AfterExecution:
            return "after";
        case Phase::Landmark:
        default:
            return "landmark";
        }
    }

    bool RegisterCallback(
        const Boundary boundary,
        const Callback callback,
        void* const user) noexcept
    {
        try
        {
            if (boundary == Boundary::Unknown ||
                boundary == Boundary::Count ||
                callback == nullptr)
            {
                return false;
            }

            std::scoped_lock lock(g_mutex);

            g_callbacks[Index(boundary)].push_back(
                CallbackEntry{
                    callback,
                    user
                });

            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    void ClearCallbacks() noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);

            for (auto& callbacks : g_callbacks)
            {
                callbacks.clear();
            }
        }
        catch (...)
        {
        }
    }

    void Signal(
        const Boundary boundary,
        const Phase phase,
        const std::uint64_t frame,
        const std::uint64_t ordinal,
        const char* const reason) noexcept
    {
        try
        {
            if (boundary == Boundary::Unknown ||
                boundary == Boundary::Count)
            {
                return;
            }

            std::vector<CallbackEntry> callbacks;
            Event event{};

            {
                std::scoped_lock lock(g_mutex);

                event.boundary = boundary;
                event.phase = phase;
                event.frame = frame;
                event.ordinal = ordinal;
                event.sequence = ++g_sequence;
                event.reason = reason;

                ++g_counts[Index(boundary)];
                callbacks =
                    g_callbacks[Index(boundary)];

                if (g_captureActive &&
                    frame >= g_captureStart &&
                    frame <= g_captureEnd)
                {
                    CapturedEvent captured{};
                    captured.boundary = boundary;
                    captured.phase = phase;
                    captured.frame = frame;
                    captured.ordinal = ordinal;
                    captured.sequence = event.sequence;
                    captured.reason =
                        reason != nullptr
                            ? reason
                            : "";

                    g_captureEvents.push_back(
                        std::move(captured));
                }
            }

            for (const auto& callback : callbacks)
            {
                if (callback.callback != nullptr)
                {
                    callback.callback(
                        event,
                        callback.user);
                }
            }
        }
        catch (...)
        {
        }
    }

    void BeginEvidenceCapture(
        const std::uint64_t startFrame,
        const std::uint64_t endFrame) noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);

            g_captureStart = startFrame;
            g_captureEnd = endFrame;
            g_captureEvents.clear();
            g_captureActive = true;
        }
        catch (...)
        {
        }
    }

    void FinishEvidenceCapture(
        const std::wstring& suffix) noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);

            if (!g_captureActive)
            {
                return;
            }

            WriteCaptureLocked(suffix);
            g_captureActive = false;
            g_captureEvents.clear();
        }
        catch (...)
        {
        }
    }

    void Reset() noexcept
    {
        try
        {
            std::scoped_lock lock(g_mutex);

            g_counts.fill(0);
            g_captureEvents.clear();
            g_sequence = 0;
            g_captureStart = 0;
            g_captureEnd = 0;
            g_captureActive = false;
        }
        catch (...)
        {
        }
    }
}
