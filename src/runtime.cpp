#include "gzfx/runtime.hpp"

#include "gzfx/diagnostics/d3d11_lifecycle.hpp"
#include "gzfx/diagnostics/renderer_forensics.hpp"
#include "gzfx/diagnostics/telemetry_listener.hpp"
#include "gzfx/paths.hpp"
#include "gzfx/version.hpp"
#include "gzfx/render/wave3_pipeline.hpp"
#include "gzfx/render/ambient_visibility_v2.hpp"

#include <Windows.h>

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>

namespace
{
    namespace fs = std::filesystem;

    std::once_flag g_initializeOnce;
    bool g_initializeResult = false;

    std::string CurrentTimestamp()
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t time = std::chrono::system_clock::to_time_t(now);
        std::tm localTime{};
        localtime_s(&localTime, &time);

        std::ostringstream stream;
        stream << std::put_time(&localTime, "%Y-%m-%dT%H:%M:%S");
        return stream.str();
    }

    void InitializeRuntime()
    {
        const fs::path executable = gzfx::paths::ExecutablePath();
        if (executable.empty() || !gzfx::paths::IsAllowedProcess(executable))
        {
            g_initializeResult = false;
            return;
        }

        const fs::path runtimeRoot = gzfx::paths::RuntimeRoot();
        const fs::path diagnosticsRoot = gzfx::paths::DiagnosticsRoot();

        if (runtimeRoot.empty() || diagnosticsRoot.empty())
        {
            g_initializeResult = false;
            return;
        }

        std::error_code error;
        fs::create_directories(diagnosticsRoot, error);
        if (error)
        {
            g_initializeResult = false;
            return;
        }

        const bool lifecycleProbeArmed =
            gzfx::diagnostics::ArmD3D11LifecycleProbe();
        const bool rendererForensicsArmed =
            gzfx::diagnostics::ArmRendererForensics();

        const bool wave3Initialized =
            gzfx::render::wave3::Initialize();

        const bool wave4Initialized =
            gzfx::render::ambient_visibility_v2::
                Initialize();

        const bool telemetryInitialized =
            gzfx::diagnostics::telemetry_listener::
                Initialize();

        if (telemetryInitialized &&
            !gzfx::paths::TestOverrideActive())
        {
            std::atexit(
                []()
                {
                    gzfx::diagnostics::
                        telemetry_listener::
                        Shutdown();
                });
        }

        const fs::path logPath = runtimeRoot / L"GZFX.log";
        std::ofstream log(logPath, std::ios::out | std::ios::app);
        if (!log)
        {
            g_initializeResult = false;
            return;
        }

        log << "=== GZFX runtime initialization ===\n";
        log << "timestamp=" << CurrentTimestamp() << "\n";
        log << "version=" << gzfx::kVersion << "\n";
        log << "pid=" << GetCurrentProcessId() << "\n";
        log << "tid=" << GetCurrentThreadId() << "\n";
        log << "executable=" << executable.string() << "\n";
        log << "runtime_root=" << runtimeRoot.string() << "\n";
        log << "d3d11_loaded="
            << (GetModuleHandleW(L"d3d11.dll") != nullptr ? "true" : "false")
            << "\n";
        log << "dxgi_loaded="
            << (GetModuleHandleW(L"dxgi.dll") != nullptr ? "true" : "false")
            << "\n";
        log << "d3d11_lifecycle_probe="
            << (lifecycleProbeArmed ? "armed" : "no_supported_imports")
            << "\n";
        log << "renderer_forensics="
            << (rendererForensicsArmed ? "armed" : "failed")
            << "\n";
        log << "wave3_pipeline="
            << (wave3Initialized ? "initialized" : "failed")
            << "\n";
        log << "ambient_visibility_v2="
            << (wave4Initialized ? "initialized" : "failed")
            << "\n";
        log << "telemetry_listener="
            << (telemetryInitialized ? "initialized" : "disabled_or_failed")
            << "\n";
        log << "status=initialized\n";
        log << "===============================\n";
        log.flush();

        g_initializeResult = static_cast<bool>(log);
    }
}

extern "C" __declspec(dllexport) bool GZFX_Initialize() noexcept
{
    try
    {
        std::call_once(g_initializeOnce, InitializeRuntime);
        return g_initializeResult;
    }
    catch (...)
    {
        return false;
    }
}


extern "C" __declspec(dllexport) void GZFX_Shutdown() noexcept
{
    gzfx::diagnostics::telemetry_listener::Shutdown();
}
