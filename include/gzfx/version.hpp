#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.32-commonlib-mit-engine-access";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
