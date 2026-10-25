#pragma once

namespace gzfx::render::wave3
{
    bool Initialize() noexcept;
    bool IsInitialized() noexcept;
    void RequestManualCapture() noexcept;
}