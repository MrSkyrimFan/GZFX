#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.10-pre-ui-effect-stack";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
