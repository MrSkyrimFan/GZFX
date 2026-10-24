#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.21-native-input-temporal-core";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
