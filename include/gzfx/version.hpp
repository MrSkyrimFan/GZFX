#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.20-foundation-epochs-stage-correctness";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
