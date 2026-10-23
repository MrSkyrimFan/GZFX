#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.8-live-settings-menu";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
