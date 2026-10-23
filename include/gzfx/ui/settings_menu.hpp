#pragma once

#include <Windows.h>
#include <d3d11.h>

#include <cstdint>

namespace gzfx::ui
{
    struct MenuOverlay
    {
        ID3D11ShaderResourceView* srv = nullptr;
        UINT width = 0;
        UINT height = 0;
        bool visible = false;
    };

    void ProcessInput(
        std::uint64_t frame,
        HWND gameWindow) noexcept;

    bool ConsumeCycleModeRequest() noexcept;
    bool ConsumeCaptureRequest() noexcept;

    bool IsMenuVisible() noexcept;

    void SubmitDirectInputMouse(
        LONG deltaX,
        LONG deltaY,
        LONG wheel,
        std::uint32_t buttonMask) noexcept;

    bool GetMenuOverlay(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        MenuOverlay& overlay) noexcept;
}
