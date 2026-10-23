#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.14-gtao-visibility-ssil-calibration";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
