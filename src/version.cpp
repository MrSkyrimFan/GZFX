#include "gzfx/version.hpp"

extern "C" __declspec(dllexport) const char* GZFX_GetVersion() noexcept
{
    return gzfx::kVersion;
}
