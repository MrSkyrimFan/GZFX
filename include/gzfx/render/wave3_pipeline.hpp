#pragma once

#include <d3d11.h>

#include <cstdint>

namespace gzfx::render::wave3
{
    struct FrameView
    {
        std::uint64_t frame = 0;
        std::uint64_t historyGeneration = 0;

        UINT width = 0;
        UINT height = 0;
        UINT mipCount = 0;

        ID3D11ShaderResourceView* depthHierarchy = nullptr;
        ID3D11ShaderResourceView* geometryConfidence = nullptr;
        ID3D11ShaderResourceView* reprojectionMask = nullptr;

        bool historyValid = false;
        bool valid = false;
    };

    bool Initialize() noexcept;
    bool IsInitialized() noexcept;
    void RequestManualCapture() noexcept;

    bool AcquireFrameView(
        std::uint64_t frame,
        FrameView* outView) noexcept;

    void ReleaseFrameView(
        FrameView& view) noexcept;
}