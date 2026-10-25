#pragma once

#include <cstdint>

namespace gzfx::diagnostics::startup_trace
{
    void Record(
        const char* event,
        std::uint64_t qpc,
        std::uint32_t threadId,
        std::int32_t result,
        std::uintptr_t pointerA,
        std::uintptr_t pointerB) noexcept;
}
