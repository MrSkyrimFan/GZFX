#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.18-renderer-semantic-atlas";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
