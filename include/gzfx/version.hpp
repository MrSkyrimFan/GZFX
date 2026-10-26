#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.30-post-tonemap-preui-cartography";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
