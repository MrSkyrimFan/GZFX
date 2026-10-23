#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.12-ao-artifact-performance-hardening";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
