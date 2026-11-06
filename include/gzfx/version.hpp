#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.37-native-domain-anchors";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
