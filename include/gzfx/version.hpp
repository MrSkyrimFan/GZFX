#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.33-skse-loader-rescue";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
