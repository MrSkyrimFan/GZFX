#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] = "0.0.3-d3d11-lifecycle";
}

extern "C" __declspec(dllexport) const char* GZFX_GetVersion() noexcept;
