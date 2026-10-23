#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.16-native-surface-temporal-foundation";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
