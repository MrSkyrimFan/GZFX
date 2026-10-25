#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.23-ambient-visibility-v2";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
