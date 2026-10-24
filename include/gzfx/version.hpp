#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.19-semantic-authority-foundation";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
