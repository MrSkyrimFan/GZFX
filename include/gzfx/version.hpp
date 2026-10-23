#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.11-horizon-ao-ssil";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
