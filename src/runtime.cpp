#include "gzfx/runtime.hpp"

#include "gzfx/diagnostics/d3d11_lifecycle.hpp"
#include "gzfx/diagnostics/engine_epochs.hpp"
#include "gzfx/diagnostics/renderer_forensics.hpp"
#include "gzfx/diagnostics/startup_trace.hpp"
#include "gzfx/diagnostics/telemetry_listener.hpp"
#include "gzfx/paths.hpp"
#include "gzfx/version.hpp"
#include "gzfx/render/wave3_pipeline.hpp"
#include "gzfx/render/ambient_visibility_v2.hpp"

#include <Windows.h>

#include <atomic>
#include <chrono>
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

    // 0 = not started, 1 = initializing, 2 = ready, 3 = failed.
    std::atomic<std::uint32_t> g_initializationState{0};
    std::atomic<bool> g_shutdownStarted{false};

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

        // Renderer hooks must exist before the real Skyrim device is accepted.
        // The lifecycle probe may already be armed by SKSEPlugin_Preload.
        const bool rendererForensicsArmed =
            gzfx::diagnostics::ArmRendererForensics();
        const bool lifecycleProbeArmed =
            gzfx::diagnostics::ArmD3D11LifecycleProbe();

        const bool engineEpochsArmed =
            gzfx::diagnostics::engine_epochs::Arm();

        const bool wave3Initialized =
            gzfx::render::wave3::Initialize();

        const bool wave4Initialized =
            gzfx::render::ambient_visibility_v2::
                Initialize();

        const bool telemetryInitialized =
            gzfx::diagnostics::telemetry_listener::
                Initialize();

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
        log << "engine_epochs="
            << (engineEpochsArmed ? "armed" : "unavailable_or_unverified")
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

namespace gzfx::runtime
{
    bool ArmEarlyGraphicsBootstrap() noexcept
    {
        try
        {
            return
                diagnostics::
                    ArmD3D11LifecycleProbe();
        }
        catch (...)
        {
            return false;
        }
    }
}

extern "C" __declspec(dllexport) bool GZFX_Initialize() noexcept
{
    try
    {
        const std::uint32_t current =
            g_initializationState.load(
                std::memory_order_acquire);

        if (current == 2)
        {
            return true;
        }

        if (current == 3)
        {
            return false;
        }

        std::uint32_t expected = 0;
        g_initializationState.compare_exchange_strong(
            expected,
            1,
            std::memory_order_acq_rel);

        std::call_once(
            g_initializeOnce,
            InitializeRuntime);

        g_initializationState.store(
            g_initializeResult ? 2u : 3u,
            std::memory_order_release);

        return g_initializeResult;
    }
    catch (...)
    {
        g_initializationState.store(
            3,
            std::memory_order_release);

        return false;
    }
}

extern "C" __declspec(dllexport)
std::uint32_t GZFX_GetInitializationState() noexcept
{
    return
        g_initializationState.load(
            std::memory_order_acquire);
}


extern "C" __declspec(dllexport) void GZFX_Shutdown() noexcept
{
    if (!g_shutdownStarted.exchange(
            true,
            std::memory_order_acq_rel))
    {
        gzfx::diagnostics::
            telemetry_listener::Shutdown();
    }
}


extern "C" __declspec(dllexport)
bool GZFX_AttachPrimaryD3D11(
    IDXGISwapChain* const swapChain,
    ID3D11Device* const device,
    ID3D11DeviceContext* const immediate) noexcept
{
    return
        gzfx::diagnostics::
            AttachPrimaryRendererObjects(
                swapChain,
                device,
                immediate);
}

extern "C" __declspec(dllexport)
void GZFX_RecordBootstrapEvent(
    const char* const event,
    const std::uint64_t qpc,
    const std::uint32_t threadId,
    const std::int32_t result,
    const std::uintptr_t pointerA,
    const std::uintptr_t pointerB) noexcept
{
    gzfx::diagnostics::startup_trace::Record(
        event,
        qpc,
        threadId,
        result,
        pointerA,
        pointerB);
}
