#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] = "0.0.6-visible-pass";
}

extern "C" __declspec(dllexport) const char* GZFX_GetVersion() noexcept;
