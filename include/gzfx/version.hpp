#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.34-directinput-menu-capture";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
