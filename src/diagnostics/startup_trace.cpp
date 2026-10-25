#include "gzfx/diagnostics/startup_trace.hpp"

#include "gzfx/paths.hpp"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <mutex>

namespace gzfx::diagnostics::startup_trace
{
    namespace fs = std::filesystem;

    namespace
    {
        std::mutex g_mutex;
        bool g_headerWritten = false;
    }

    void Record(
        const char* const event,
        const std::uint64_t qpc,
        const std::uint32_t threadId,
        const std::int32_t result,
        const std::uintptr_t pointerA,
        const std::uintptr_t pointerB) noexcept
    {
        try
        {
            if (event == nullptr ||
                *event == '\0')
            {
                return;
            }

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

            std::scoped_lock lock(g_mutex);

            const fs::path path =
                root / L"renderer_startup.csv";

            const bool exists =
                fs::exists(
                    path,
                    error);

            std::ofstream out(
                path,
                std::ios::out |
                    std::ios::app);

            if (!out)
            {
                return;
            }

            if (!g_headerWritten &&
                (!exists ||
                 fs::file_size(path, error) == 0))
            {
                out
                    << "qpc,thread,event,result,pointer_a,pointer_b\n";
            }

            g_headerWritten = true;

            out
                << qpc << ","
                << threadId << ","
                << event << ","
                << result << ","
                << "0x" << std::hex << std::uppercase
                << pointerA << ","
                << "0x" << pointerB
                << std::dec
                << "\n";
        }
        catch (...)
        {
        }
    }
}
