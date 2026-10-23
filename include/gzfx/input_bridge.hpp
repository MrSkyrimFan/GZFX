#pragma once

#include <Windows.h>

#include <cstdint>

extern "C" __declspec(dllexport)
bool GZFX_IsSettingsMenuOpen() noexcept;

extern "C" __declspec(dllexport)
void GZFX_SubmitMouseInput(
    LONG deltaX,
    LONG deltaY,
    LONG wheel,
    std::uint32_t buttonMask) noexcept;
