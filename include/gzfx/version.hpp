#pragma once

namespace gzfx
{
    inline constexpr char kVersion[] =
        "0.0.28-pre-field-lifecycle-hardening";
}

extern "C" __declspec(dllexport)
const char* GZFX_GetVersion() noexcept;
