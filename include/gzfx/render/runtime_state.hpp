#pragma once

#include <cstdint>

namespace gzfx::render::state
{
    std::uint32_t GetDevelopmentMode() noexcept;
    void SetDevelopmentMode(std::uint32_t mode) noexcept;
    std::uint32_t CycleDevelopmentMode() noexcept;
}
