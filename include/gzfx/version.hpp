#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.26-renderer-identity-engine-epochs";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
