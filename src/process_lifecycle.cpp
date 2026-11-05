#include "gzfx/process_lifecycle.hpp"

#include "gzfx/diagnostics/startup_trace.hpp"
#include "gzfx/runtime.hpp"

#include <Windows.h>
#include <MinHook.h>

#include <atomic>
#include <cstdint>
#include <mutex>

namespace gzfx::process_lifecycle
{
    namespace
    {
        using ExitProcessFn = VOID(WINAPI*)(UINT);

        ExitProcessFn g_exitProcess = nullptr;
        std::once_flag g_armOnce;
        std::atomic<bool> g_shutdownStarted{false};
        bool g_armed = false;

        std::uint64_t QpcNow() noexcept
        {
            LARGE_INTEGER value{};
            QueryPerformanceCounter(&value);

            return
                static_cast<std::uint64_t>(
                    value.QuadPart);
        }

        VOID WINAPI HookExitProcess(
            const UINT exitCode)
        {
            if (!g_shutdownStarted.exchange(
                    true,
                    std::memory_order_acq_rel))
            {
                diagnostics::startup_trace::Record(
                    "process_exit_intercepted",
                    QpcNow(),
                    GetCurrentThreadId(),
                    static_cast<std::int32_t>(exitCode),
                    0,
                    0);

                if (GZFX_GetInitializationState() == 2)
                {
                    GZFX_Shutdown();
                }
            }

            if (g_exitProcess != nullptr)
            {
                g_exitProcess(exitCode);
            }

            TerminateProcess(
                GetCurrentProcess(),
                exitCode);
        }

        void ArmImpl() noexcept
        {
            const MH_STATUS initialized =
                MH_Initialize();

            if (initialized != MH_OK &&
                initialized != MH_ERROR_ALREADY_INITIALIZED)
            {
                return;
            }

            const HMODULE kernel32 =
                GetModuleHandleW(
                    L"kernel32.dll");

            if (kernel32 == nullptr)
            {
                return;
            }

            void* const target =
                reinterpret_cast<void*>(
                    GetProcAddress(
                        kernel32,
                        "ExitProcess"));

            if (target == nullptr)
            {
                return;
            }

            void* original = nullptr;

            if (MH_CreateHook(
                    target,
                    reinterpret_cast<void*>(
                        &HookExitProcess),
                    &original) != MH_OK)
            {
                return;
            }

            g_exitProcess =
                reinterpret_cast<ExitProcessFn>(
                    original);

            if (MH_EnableHook(target) != MH_OK)
            {
                g_exitProcess = nullptr;
                return;
            }

            g_armed = true;

            diagnostics::startup_trace::Record(
                "exit_process_hook_armed",
                QpcNow(),
                GetCurrentThreadId(),
                0,
                reinterpret_cast<std::uintptr_t>(
                    target),
                0);
        }
    }

    bool Arm() noexcept
    {
        try
        {
            std::call_once(
                g_armOnce,
                ArmImpl);

            return g_armed;
        }
        catch (...)
        {
            return false;
        }
    }
}
