#include "gzfx/input_bridge.hpp"

#include "gzfx/ui/settings_menu.hpp"

extern "C" __declspec(dllexport)
bool GZFX_IsSettingsMenuOpen() noexcept
{
    return gzfx::ui::IsMenuVisible();
}

extern "C" __declspec(dllexport)
void GZFX_SubmitMouseInput(
    const LONG deltaX,
    const LONG deltaY,
    const LONG wheel,
    const std::uint32_t buttonMask) noexcept
{
    gzfx::ui::SubmitDirectInputMouse(
        deltaX,
        deltaY,
        wheel,
        buttonMask);
}
