#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.25-late-stack-cartography-closure";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
