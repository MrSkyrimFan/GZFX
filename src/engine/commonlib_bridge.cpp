// CommonLib's public headers are intentionally PCH-dependent. Consumer
// translation units must establish the CommonLib standard-library environment
// and global string-view literals before entering RE/Skyrim.h. Windows API
// headers must still come only after CommonLib.
#include <SKSE/Impl/PCH.h>
using namespace std::literals;
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include "gzfx/engine/commonlib_bridge.hpp"
#include "gzfx/diagnostics/renderer_forensics.hpp"
#include "gzfx/paths.hpp"
#include "gzfx/render/renderer_execution_service.hpp"
#include "gzfx/skse_api.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

namespace gzfx::engine::commonlib
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr char kMitNotice[] =
            "MIT License\n"
            "\n"
            "Copyright (c) 2018 Ryan-rsm-McKenzie\n"
            "\n"
            "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
            "of this software and associated documentation files (the \"Software\"), to deal\n"
            "in the Software without restriction, including without limitation the rights\n"
            "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell\n"
            "copies of the Software, and to permit persons to whom the Software is\n"
            "furnished to do so, subject to the following conditions:\n"
            "\n"
            "The above copyright notice and this permission notice shall be included in all\n"
            "copies or substantial portions of the Software.\n"
            "\n"
            "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
            "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
            "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n"
            "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n"
            "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n"
            "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n"
            "SOFTWARE.\n";

        std::mutex g_mutex;
        Snapshot g_snapshot{};

        RE::BSGraphics::Renderer* g_renderer = nullptr;
        RE::Sky* g_sky = nullptr;
        RE::TESDataHandler* g_dataHandler = nullptr;

        bool g_moduleAttempted = false;
        bool g_skseApiAttempted = false;
        bool g_bindAttempted = false;

        fs::path AddressLibraryPath()
        {
            const fs::path executable =
                paths::ExecutablePath();

            if (executable.empty())
            {
                return {};
            }

            return
                executable.parent_path() /
                L"Data" /
                L"SKSE" /
                L"Plugins" /
                L"versionlib-1-7-104-0.bin";
        }

        std::uintptr_t Identity(
            IUnknown* const value) noexcept
        {
            if (value == nullptr)
            {
                return 0;
            }

            IUnknown* identity = nullptr;

            if (FAILED(
                    value->QueryInterface(
                        __uuidof(IUnknown),
                        reinterpret_cast<void**>(
                            &identity))) ||
                identity == nullptr)
            {
                return
                    reinterpret_cast<std::uintptr_t>(
                        value);
            }

            const auto result =
                reinterpret_cast<std::uintptr_t>(
                    identity);

            identity->Release();
            return result;
        }

        void WriteLicenseNotice() noexcept
        {
            try
            {
                const fs::path root =
                    paths::RuntimeRoot();

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
                    return;
                }

                std::ofstream out(
                    root /
                        L"CommonLibSSE-GG-MIT.txt",
                    std::ios::out |
                        std::ios::trunc);

                if (out)
                {
                    out << kMitNotice;
                }
            }
            catch (...)
            {
            }
        }

        void WriteDiagnostic(
            const char* const event) noexcept
        {
            try
            {
                const fs::path root =
                    paths::DiagnosticsRoot();

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
                    return;
                }

                std::ofstream out(
                    root /
                        L"commonlib_engine.txt",
                    std::ios::out |
                        std::ios::trunc);

                if (!out)
                {
                    return;
                }

                out
                    << "GZFX CommonLib engine access\n"
                    << "event="
                    << (event != nullptr
                        ? event
                        : "unknown")
                    << "\n"
                    << "status="
                    << StatusName(
                        g_snapshot.status)
                    << "\n"
                    << "module_ready="
                    << (g_snapshot.moduleReady
                        ? "true"
                        : "false")
                    << "\n"
                    << "address_library_present="
                    << (g_snapshot.addressLibraryPresent
                        ? "true"
                        : "false")
                    << "\n"
                    << "skse_api_ready="
                    << (g_snapshot.skseApiReady
                        ? "true"
                        : "false")
                    << "\n"
                    << "data_loaded="
                    << (g_snapshot.dataLoaded
                        ? "true"
                        : "false")
                    << "\n"
                    << "renderer_bound="
                    << (g_snapshot.rendererBound
                        ? "true"
                        : "false")
                    << "\n"
                    << "renderer_correlated="
                    << (g_snapshot.rendererCorrelated
                        ? "true"
                        : "false")
                    << "\n"
                    << "fallback_attach_attempted="
                    << (g_snapshot.fallbackAttachAttempted
                        ? "true"
                        : "false")
                    << "\n"
                    << "fallback_attach_succeeded="
                    << (g_snapshot.fallbackAttachSucceeded
                        ? "true"
                        : "false")
                    << "\n"
                    << "frame="
                    << g_snapshot.frame
                    << "\n"
                    << "renderer=0x"
                    << std::hex
                    << std::uppercase
                    << g_snapshot.renderer
                    << "\n"
                    << "sky=0x"
                    << g_snapshot.sky
                    << "\n"
                    << "data_handler=0x"
                    << g_snapshot.dataHandler
                    << "\n"
                    << "device=0x"
                    << g_snapshot.device
                    << "\n"
                    << "context=0x"
                    << g_snapshot.context
                    << "\n"
                    << "swap_chain=0x"
                    << g_snapshot.swapChain
                    << "\n"
                    << "current_weather=0x"
                    << g_snapshot.currentWeather
                    << "\n"
                    << "last_weather=0x"
                    << g_snapshot.lastWeather
                    << "\n"
                    << "sun=0x"
                    << g_snapshot.sun
                    << "\n"
                    << "clouds=0x"
                    << g_snapshot.clouds
                    << "\n"
                    << "masser=0x"
                    << g_snapshot.masser
                    << "\n"
                    << "secunda=0x"
                    << g_snapshot.secunda
                    << "\n"
                    << "precipitation=0x"
                    << g_snapshot.precipitation
                    << std::dec
                    << "\n"
                    << "sky_mode="
                    << g_snapshot.skyMode
                    << "\n"
                    << "game_hour="
                    << g_snapshot.gameHour
                    << "\n"
                    << "weather_blend="
                    << g_snapshot.weatherBlend
                    << "\n"
                    << "wind_speed="
                    << g_snapshot.windSpeed
                    << "\n"
                    << "wind_angle="
                    << g_snapshot.windAngle
                    << "\n"
                    << "fog_near="
                    << g_snapshot.fogNear
                    << "\n"
                    << "fog_far="
                    << g_snapshot.fogFar
                    << "\n"
                    << "fog_power="
                    << g_snapshot.fogPower
                    << "\n"
                    << "fog_clamp="
                    << g_snapshot.fogClamp
                    << "\n";
            }
            catch (...)
            {
            }
        }

        void UpdateRendererPointersLocked() noexcept
        {
            if (g_renderer == nullptr)
            {
                return;
            }

            auto* device =
                reinterpret_cast<ID3D11Device*>(
                    RE::BSGraphics::Renderer::
                        GetDevice());

            auto& rendererData =
                g_renderer->GetRuntimeData();

            auto* context =
                reinterpret_cast<
                    ID3D11DeviceContext*>(
                    rendererData.context);

            auto* window =
                RE::BSGraphics::Renderer::
                    GetCurrentRenderWindow();

            auto* swapChain =
                window != nullptr
                    ? reinterpret_cast<
                        IDXGISwapChain*>(
                        window->swapChain)
                    : nullptr;

            g_snapshot.device =
                Identity(device);

            g_snapshot.context =
                Identity(context);

            g_snapshot.swapChain =
                Identity(swapChain);
        }

        void CorrelateRendererLocked() noexcept
        {
            render::execution::ExecutionLease
                lease{};

            if (!render::execution::Acquire(
                    &lease))
            {
                g_snapshot.rendererCorrelated =
                    false;
                return;
            }

            const std::uintptr_t leaseDevice =
                Identity(lease.device);

            const std::uintptr_t leaseContext =
                Identity(lease.immediate);

            const std::uintptr_t leaseSwap =
                Identity(lease.swapChain);

            const bool deviceMatch =
                g_snapshot.device != 0 &&
                leaseDevice ==
                    g_snapshot.device;

            const bool contextMatch =
                g_snapshot.context != 0 &&
                leaseContext ==
                    g_snapshot.context;

            const bool swapMatch =
                g_snapshot.swapChain == 0 ||
                leaseSwap == 0 ||
                leaseSwap ==
                    g_snapshot.swapChain;

            g_snapshot.rendererCorrelated =
                deviceMatch &&
                contextMatch &&
                swapMatch;

            render::execution::Release(
                lease);
        }

        bool TryFallbackAttach() noexcept
        {
            RE::BSGraphics::Renderer*
                renderer = nullptr;

            {
                std::scoped_lock lock(
                    g_mutex);
                renderer = g_renderer;
            }

            if (renderer == nullptr)
            {
                return false;
            }

            render::execution::ExecutionLease
                current{};

            const bool alreadyAttached =
                render::execution::Acquire(
                    &current);

            if (alreadyAttached)
            {
                render::execution::Release(
                    current);
                return true;
            }

            auto* device =
                reinterpret_cast<ID3D11Device*>(
                    RE::BSGraphics::Renderer::
                        GetDevice());

            auto& rendererData =
                renderer->GetRuntimeData();

            auto* context =
                reinterpret_cast<
                    ID3D11DeviceContext*>(
                    rendererData.context);

            auto* window =
                RE::BSGraphics::Renderer::
                    GetCurrentRenderWindow();

            auto* swapChain =
                window != nullptr
                    ? reinterpret_cast<
                        IDXGISwapChain*>(
                        window->swapChain)
                    : nullptr;

            if (device == nullptr ||
                context == nullptr)
            {
                return false;
            }

            {
                std::scoped_lock lock(
                    g_mutex);
                g_snapshot.fallbackAttachAttempted =
                    true;
            }

            // Do not hold the CommonLib state mutex while entering
            // renderer-forensics. Present takes the renderer lock before
            // the CommonLib lock, so doing the reverse here would create
            // an ABBA deadlock.
            const bool attached =
                diagnostics::
                    AttachPrimaryRendererObjects(
                        swapChain,
                        device,
                        context);

            {
                std::scoped_lock lock(
                    g_mutex);
                g_snapshot.fallbackAttachSucceeded =
                    attached;
            }

            return attached;
        }
    }

    bool InitializeModule() noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (g_moduleAttempted)
            {
                return
                    g_snapshot.moduleReady ||
                    g_snapshot.status ==
                        Status::TestBypass;
            }

            g_moduleAttempted = true;

            WriteLicenseNotice();

            if (paths::TestOverrideActive())
            {
                g_snapshot.status =
                    Status::TestBypass;
                g_snapshot.moduleReady = true;
                WriteDiagnostic(
                    "test_bypass");
                return true;
            }

            const auto& module =
                REL::Module::get();

            const bool versionMatches =
                module.version() ==
                SKSE::RUNTIME_SSE_1_7_104;

            const bool runtimeMatches =
                REL::Module::IsAE();

            g_snapshot.moduleReady =
                versionMatches &&
                runtimeMatches;

            const fs::path addressLibrary =
                AddressLibraryPath();

            std::error_code error;

            g_snapshot.addressLibraryPresent =
                !addressLibrary.empty() &&
                fs::exists(
                    addressLibrary,
                    error) &&
                !error;

            if (!g_snapshot.moduleReady)
            {
                g_snapshot.status =
                    Status::BindFailed;
                WriteDiagnostic(
                    "module_runtime_mismatch");
                return false;
            }

            g_snapshot.status =
                g_snapshot.addressLibraryPresent
                    ? Status::
                        WaitingForDataLoaded
                    : Status::
                        AddressLibraryMissing;

            WriteDiagnostic(
                g_snapshot.addressLibraryPresent
                    ? "module_ready"
                    : "address_library_missing");

            return true;
        }
        catch (...)
        {
            std::scoped_lock lock(
                g_mutex);

            g_snapshot.status =
                Status::BindFailed;

            WriteDiagnostic(
                "module_exception");

            return false;
        }
    }

    bool InitializeSkseInterface(
        const gzfx::skse::Interface* const skse) noexcept
    {
        try
        {
            if (!InitializeModule())
            {
                return false;
            }

            {
                std::scoped_lock lock(
                    g_mutex);

                if (g_snapshot.status ==
                    Status::TestBypass)
                {
                    g_snapshot.skseApiReady = true;

                    WriteDiagnostic(
                        "skse_api_test_bypass");

                    return true;
                }

                if (g_skseApiAttempted)
                {
                    return
                        g_snapshot.skseApiReady;
                }

                g_skseApiAttempted = true;

                if (skse == nullptr ||
                    !g_snapshot.moduleReady ||
                    !g_snapshot.addressLibraryPresent)
                {
                    g_snapshot.skseApiReady = false;

                    WriteDiagnostic(
                        "skse_api_prerequisite_missing");

                    return false;
                }
            }

            // CommonLib's LoadInterface is intentionally a zero-data wrapper
            // over SKSE's raw interface pointer. The verified GZFX SKSE ABI
            // matches SKSE 2.3.1 exactly, so initialize CommonLib through its
            // supported public contract instead of touching REL state ad hoc.
            SKSE::Init(
                reinterpret_cast<
                    const SKSE::LoadInterface*>(
                    skse),
                false);

            {
                std::scoped_lock lock(
                    g_mutex);

                g_snapshot.skseApiReady = true;

                WriteDiagnostic(
                    "skse_api_ready");
            }

            return true;
        }
        catch (...)
        {
            std::scoped_lock lock(
                g_mutex);

            g_snapshot.skseApiReady = false;
            g_snapshot.status =
                Status::BindFailed;

            WriteDiagnostic(
                "skse_api_exception");

            return false;
        }
    }

    bool BindAfterDataLoaded() noexcept
    {
        try
        {
            if (!InitializeModule())
            {
                return false;
            }

            {
                std::scoped_lock lock(
                    g_mutex);

            g_snapshot.dataLoaded = true;

            if (g_snapshot.status ==
                Status::TestBypass)
            {
                WriteDiagnostic(
                    "data_loaded_test_bypass");
                return true;
            }

            if (g_bindAttempted)
            {
                return
                    g_snapshot.status ==
                    Status::Bound;
            }

            g_bindAttempted = true;

            if (!g_snapshot.addressLibraryPresent)
            {
                g_snapshot.status =
                    Status::
                        AddressLibraryMissing;

                WriteDiagnostic(
                    "data_loaded_without_address_library");

                return false;
            }

            if (!g_snapshot.skseApiReady)
            {
                g_snapshot.status =
                    Status::BindFailed;

                WriteDiagnostic(
                    "data_loaded_without_skse_api");

                return false;
            }

            g_renderer =
                RE::BSGraphics::Renderer::
                    GetSingleton();

            g_sky =
                RE::Sky::GetSingleton();

            g_dataHandler =
                RE::TESDataHandler::
                    GetSingleton();

            g_snapshot.renderer =
                reinterpret_cast<
                    std::uintptr_t>(
                    g_renderer);

            g_snapshot.sky =
                reinterpret_cast<
                    std::uintptr_t>(
                    g_sky);

            g_snapshot.dataHandler =
                reinterpret_cast<
                    std::uintptr_t>(
                    g_dataHandler);

            g_snapshot.rendererBound =
                g_renderer != nullptr;

            if (!g_snapshot.rendererBound ||
                g_dataHandler == nullptr)
            {
                g_snapshot.status =
                    Status::BindFailed;

                WriteDiagnostic(
                    "engine_singleton_missing");

                return false;
            }

            UpdateRendererPointersLocked();

            g_snapshot.status =
                Status::Bound;

            WriteDiagnostic(
                "engine_bound_pre_correlation");
        }

        TryFallbackAttach();

        {
            std::scoped_lock lock(
                g_mutex);

            UpdateRendererPointersLocked();
            CorrelateRendererLocked();

            WriteDiagnostic(
                "engine_bound");
        }

        return true;
        }
        catch (...)
        {
            std::scoped_lock lock(
                g_mutex);

            g_snapshot.status =
                Status::BindFailed;

            WriteDiagnostic(
                "bind_exception");

            return false;
        }
    }

    void OnPresent(
        const std::uint64_t frame) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            if (g_snapshot.status ==
                Status::TestBypass)
            {
                return;
            }

            if (g_snapshot.status !=
                    Status::Bound ||
                g_renderer == nullptr)
            {
                return;
            }

            g_snapshot.frame = frame;

            UpdateRendererPointersLocked();
            CorrelateRendererLocked();

            if (g_sky != nullptr)
            {
                g_snapshot.currentWeather =
                    reinterpret_cast<
                        std::uintptr_t>(
                        g_sky->currentWeather);

                g_snapshot.lastWeather =
                    reinterpret_cast<
                        std::uintptr_t>(
                        g_sky->lastWeather);

                g_snapshot.sun =
                    reinterpret_cast<
                        std::uintptr_t>(
                        g_sky->sun);

                g_snapshot.clouds =
                    reinterpret_cast<
                        std::uintptr_t>(
                        g_sky->clouds);

                g_snapshot.masser =
                    reinterpret_cast<
                        std::uintptr_t>(
                        g_sky->masser);

                g_snapshot.secunda =
                    reinterpret_cast<
                        std::uintptr_t>(
                        g_sky->secunda);

                g_snapshot.precipitation =
                    reinterpret_cast<
                        std::uintptr_t>(
                        g_sky->precip);

                g_snapshot.gameHour =
                    g_sky->currentGameHour;

                g_snapshot.weatherBlend =
                    g_sky->currentWeatherPct;

                g_snapshot.windSpeed =
                    g_sky->windSpeed;

                g_snapshot.windAngle =
                    g_sky->windAngle;

                g_snapshot.fogNear =
                    g_sky->fogNear;

                g_snapshot.fogFar =
                    g_sky->fogFar;

                g_snapshot.fogPower =
                    g_sky->fogPower;

                g_snapshot.fogClamp =
                    g_sky->fogClamp;

                g_snapshot.skyMode =
                    g_sky->mode.underlying();
            }

            if (frame <= 3 ||
                (frame % 120) == 0)
            {
                WriteDiagnostic(
                    "present_snapshot");
            }
        }
        catch (...)
        {
        }
    }

    Snapshot GetSnapshot() noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_mutex);

            return g_snapshot;
        }
        catch (...)
        {
            return {};
        }
    }

    const char* StatusName(
        const Status status) noexcept
    {
        switch (status)
        {
        case Status::Disabled:
            return "disabled";
        case Status::TestBypass:
            return "test_bypass";
        case Status::ModuleReady:
            return "module_ready";
        case Status::AddressLibraryMissing:
            return "address_library_missing";
        case Status::WaitingForDataLoaded:
            return "waiting_for_data_loaded";
        case Status::Bound:
            return "bound";
        case Status::BindFailed:
            return "bind_failed";
        default:
            return "unknown";
        }
    }

    const char* EmbeddedMitNotice() noexcept
    {
        return kMitNotice;
    }
}
