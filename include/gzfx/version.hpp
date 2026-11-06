#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.35-product-control-recovery";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
