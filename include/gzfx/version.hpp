#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.27-universal-frontdoor-late-contracts";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
