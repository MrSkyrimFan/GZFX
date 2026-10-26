#pragma once

#include <filesystem>

namespace gzfx::paths
{
    std::filesystem::path ExecutablePath();
    std::filesystem::path RuntimeRoot();
    std::filesystem::path DiagnosticsRoot();
    bool TestOverrideActive();
    bool IsAllowedProcess(const std::filesystem::path& executable);
}
