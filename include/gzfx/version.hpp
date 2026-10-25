#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.22-temporal-continuity-repair";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
