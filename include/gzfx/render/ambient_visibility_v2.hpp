#pragma once

#include <cstdint>

namespace gzfx::render::ambient_visibility_v2
{
    bool Initialize() noexcept;
    bool IsInitialized() noexcept;

    bool WasAppliedFrame(
        std::uint64_t frame) noexcept;

    bool OwnsDevelopmentMode(
        std::uint32_t mode) noexcept;

    void RequestManualCapture() noexcept;
}
