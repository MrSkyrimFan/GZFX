#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.36-unified-product-renderer";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
