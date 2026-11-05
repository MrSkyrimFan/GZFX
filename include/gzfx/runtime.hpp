#pragma once

#include <cstdint>

namespace gzfx::runtime
{
    bool ArmEarlyGraphicsBootstrap() noexcept;
}

extern "C" __declspec(dllexport)
bool GZFX_Initialize() noexcept;

extern "C" __declspec(dllexport)
std::uint32_t GZFX_GetInitializationState() noexcept;

extern "C" __declspec(dllexport)
void GZFX_Shutdown() noexcept;
