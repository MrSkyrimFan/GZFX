#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] = "0.0.5-resource-intelligence";
}

extern "C" __declspec(dllexport) const char* GZFX_GetVersion() noexcept;
