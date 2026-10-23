#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.13-ambient-depth-functional-recovery";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
