#pragma once

#include <cstdint>

namespace gzfx::diagnostics
{
    struct RendererForensicsStatus
    {
        bool shaderCreationHooksArmed = false;
        bool resourceCreationHooksArmed = false;
        std::uint64_t shaderCreationsObserved = 0;
        std::uint64_t resourceCreationsObserved = 0;
        std::uint64_t presentEpochs = 0;
    };

    bool ArmRendererForensics() noexcept;
    RendererForensicsStatus GetRendererForensicsStatus() noexcept;
    std::uint64_t CurrentRendererFrame() noexcept;
}
