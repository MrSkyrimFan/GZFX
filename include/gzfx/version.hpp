#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.29-hdr-branch-recovery";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
