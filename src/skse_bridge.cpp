#include "gzfx/skse_bridge.hpp"

#include "gzfx/diagnostics/startup_trace.hpp"
#include "gzfx/paths.hpp"
#include "gzfx/process_lifecycle.hpp"
#include "gzfx/runtime.hpp"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace
{
    namespace fs = std::filesystem;

    std::atomic<bool> g_preloadGraphicsArmed{false};
    std::atomic<bool> g_loadEntered{false};

    gzfx::skse::PluginHandle g_pluginHandle =
        gzfx::skse::kInvalidPluginHandle;

    const gzfx::skse::MessagingInterface*
        g_messaging = nullptr;

    const gzfx::skse::TrampolineInterface*
        g_trampoline = nullptr;

    std::mutex g_statusMutex;

    std::uint64_t QpcNow() noexcept
    {
        LARGE_INTEGER value{};
        QueryPerformanceCounter(&value);

        return
            static_cast<std::uint64_t>(
                value.QuadPart);
    }

    bool Compatible(
        const gzfx::skse::Interface* const skse) noexcept
    {
        return
            skse != nullptr &&
            skse->isEditor == 0 &&
            skse->runtimeVersion ==
                gzfx::skse::kRuntime_1_7_104;
    }

    const char* MessageName(
        const std::uint32_t type) noexcept
    {
        using Message =
            gzfx::skse::MessagingInterface;

        switch (type)
        {
        case Message::PostLoad:
            return "post_load";
        case Message::PostPostLoad:
            return "post_post_load";
        case Message::PreLoadGame:
            return "pre_load_game";
        case Message::PostLoadGame:
            return "post_load_game";
        case Message::SaveGame:
            return "save_game";
        case Message::DeleteGame:
            return "delete_game";
        case Message::InputLoaded:
            return "input_loaded";
        case Message::NewGame:
            return "new_game";
        case Message::DataLoaded:
            return "data_loaded";
        default:
            return "unknown";
        }
    }

    void AppendStatus(
        const std::string& line) noexcept
    {
        try
        {
            std::scoped_lock lock(
                g_statusMutex);

            const fs::path root =
                gzfx::paths::DiagnosticsRoot();

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
                root / L"skse_bridge.txt",
                std::ios::out |
                    std::ios::app);

            if (out)
            {
                out << line << "\n";
            }
        }
        catch (...)
        {
        }
    }

    void OnSkseMessage(
        gzfx::skse::MessagingInterface::Message*
            message)
    {
        if (message == nullptr)
        {
            return;
        }

        AppendStatus(
            std::string("message=") +
            MessageName(message->type));
    }
}

extern "C"
{
    __declspec(dllexport)
    gzfx::skse::PluginVersionData
        SKSEPlugin_Version =
    {
        gzfx::skse::PluginVersionData::kVersion,
        31,
        "GZFX",
        "Greenezone Software LLC",
        "",
        0,
        0,
        {
            gzfx::skse::kRuntime_1_7_104,
            0
        },
        0
    };

    __declspec(dllexport)
    bool SKSEPlugin_Preload(
        const gzfx::skse::Interface* const skse)
    {
        if (!Compatible(skse))
        {
            return false;
        }

        const bool armed =
            gzfx::runtime::
                ArmEarlyGraphicsBootstrap();

        g_preloadGraphicsArmed.store(
            armed,
            std::memory_order_release);

        // Once the early front door has been attempted, keep the DLL resident.
        // A partially installed import hook must never point into an unloaded
        // plugin even when the front door reports a degraded state.
        return true;
    }

    __declspec(dllexport)
    bool SKSEPlugin_Load(
        const gzfx::skse::Interface* const skse)
    {
        if (!Compatible(skse))
        {
            return false;
        }

        if (g_loadEntered.exchange(
                true,
                std::memory_order_acq_rel))
        {
            return true;
        }

        try
        {
            if (skse->GetPluginHandle != nullptr)
            {
                g_pluginHandle =
                    skse->GetPluginHandle();
            }

            if (skse->QueryInterface != nullptr)
            {
                g_messaging =
                    static_cast<
                        const gzfx::skse::
                            MessagingInterface*>(
                        skse->QueryInterface(
                            gzfx::skse::Messaging));

                g_trampoline =
                    static_cast<
                        const gzfx::skse::
                            TrampolineInterface*>(
                        skse->QueryInterface(
                            gzfx::skse::Trampoline));
            }

            bool messagingRegistered = false;

            if (g_messaging != nullptr &&
                g_messaging->interfaceVersion >= 2 &&
                g_messaging->RegisterListener != nullptr &&
                g_pluginHandle !=
                    gzfx::skse::kInvalidPluginHandle)
            {
                messagingRegistered =
                    g_messaging->RegisterListener(
                        g_pluginHandle,
                        "SKSE",
                        &OnSkseMessage);
            }

            const bool initialized =
                GZFX_Initialize();

            const bool processLifecycleArmed =
                gzfx::process_lifecycle::Arm();

            gzfx::diagnostics::
                startup_trace::Record(
                    "skse_plugin_load",
                    QpcNow(),
                    GetCurrentThreadId(),
                    initialized ? 0 : 1,
                    static_cast<std::uintptr_t>(
                        skse->skseVersion),
                    static_cast<std::uintptr_t>(
                        skse->runtimeVersion));

            AppendStatus(
                "target_skse=2.3.1");
            AppendStatus(
                "target_runtime=1.7.104");
            AppendStatus(
                std::string(
                    "early_graphics_armed=") +
                (g_preloadGraphicsArmed.load(
                    std::memory_order_acquire)
                    ? "true"
                    : "false"));
            AppendStatus(
                std::string(
                    "messaging_registered=") +
                (messagingRegistered
                    ? "true"
                    : "false"));
            AppendStatus(
                std::string(
                    "trampoline_available=") +
                (g_trampoline != nullptr &&
                 g_trampoline->interfaceVersion >= 1
                    ? "true"
                    : "false"));
            AppendStatus(
                std::string(
                    "runtime_initialized=") +
                (initialized
                    ? "true"
                    : "false"));
            AppendStatus(
                std::string(
                    "process_lifecycle_armed=") +
                (processLifecycleArmed
                    ? "true"
                    : "false"));

            // Keep the DLL resident after any initialization attempt. GZFX
            // installs native hooks and must never be unloaded underneath them.
            return true;
        }
        catch (...)
        {
            AppendStatus(
                "load_exception=true");

            return true;
        }
    }
}

namespace gzfx::skse_bridge
{
    skse::PluginHandle Plugin() noexcept
    {
        return g_pluginHandle;
    }

    const skse::MessagingInterface*
    Messaging() noexcept
    {
        return g_messaging;
    }

    const skse::TrampolineInterface*
    Trampoline() noexcept
    {
        return g_trampoline;
    }

    bool EarlyGraphicsArmed() noexcept
    {
        return
            g_preloadGraphicsArmed.load(
                std::memory_order_acquire);
    }
}
