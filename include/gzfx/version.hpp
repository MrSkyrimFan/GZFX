#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.17-renderer-pipeline-cartography";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
