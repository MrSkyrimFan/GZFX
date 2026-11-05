#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.31-skse-single-dll-frontdoor";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
