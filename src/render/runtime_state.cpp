#include "gzfx/render/runtime_state.hpp"

#include "gzfx/settings.hpp"

#include <atomic>
#include <algorithm>
#include <mutex>

namespace gzfx::render::state
{
    namespace
    {
        std::atomic<std::uint32_t> g_mode{2};
        std::once_flag g_once;

        void Initialize()
        {
            g_mode.store(
                gzfx::settings::Get().defaultMode == 0u
                    ? 0u : 2u,
                std::memory_order_relaxed);
        }

        void EnsureInitialized()
        {
            std::call_once(
                g_once,
                Initialize);
        }
    }

    std::uint32_t GetDevelopmentMode() noexcept
    {
        try
        {
            EnsureInitialized();

            return g_mode.load(
                std::memory_order_relaxed);
        }
        catch (...)
        {
            return 2;
        }
    }

    void SetDevelopmentMode(
        const std::uint32_t mode) noexcept
    {
        try
        {
            EnsureInitialized();

            g_mode.store(
                mode == 0u ? 0u : 2u,
                std::memory_order_relaxed);
        }
        catch (...)
        {
        }
    }

    std::uint32_t CycleDevelopmentMode() noexcept
    {
        try
        {
            EnsureInitialized();

            const std::uint32_t current =
                g_mode.load(
                    std::memory_order_relaxed);

            // Shift+Numpad3: vanilla OFF <-> complete GZFX ON.
            // Diagnostic split/depth views are no longer public modes.
            const std::uint32_t next =
                current == 0u ? 2u : 0u;

            g_mode.store(
                next,
                std::memory_order_relaxed);

            return next;
        }
        catch (...)
        {
            return 2;
        }
    }
}
