#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.9-directinput-menu";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
