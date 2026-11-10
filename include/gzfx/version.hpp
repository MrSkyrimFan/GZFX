#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.38-calibrated-unified-renderer";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
