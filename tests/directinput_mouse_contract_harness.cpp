#include "gzfx/ui/directinput_mouse.hpp"

#include <array>
#include <cstdint>
#include <iostream>

int main()
{
    using namespace
        gzfx::ui::directinput_mouse;

    DIMOUSESTATE state{};
    state.lX = 11;
    state.lY = -7;
    state.lZ = 120;
    state.rgbButtons[0] = 0x80;
    state.rgbButtons[2] = 0x80;

    MousePacket packet{};

    if (!detail::DecodeImmediateState(
            sizeof(state),
            &state,
            &packet) ||
        packet.x != 11 ||
        packet.y != -7 ||
        packet.wheel != 120 ||
        packet.buttons != 0x5u)
    {
        return 2;
    }

    DIMOUSESTATE2 state2{};
    state2.lX = -13;
    state2.lY = 9;
    state2.lZ = -120;
    state2.rgbButtons[1] = 0x80;
    state2.rgbButtons[7] = 0x80;

    if (!detail::DecodeImmediateState(
            sizeof(state2),
            &state2,
            &packet) ||
        packet.x != -13 ||
        packet.y != 9 ||
        packet.wheel != -120 ||
        packet.buttons !=
            ((1u << 1u) |
             (1u << 7u)))
    {
        return 3;
    }

    std::array<std::uint8_t, 256>
        keyboardLike{};

    if (detail::DecodeImmediateState(
            static_cast<DWORD>(
                keyboardLike.size()),
            keyboardLike.data(),
            &packet))
    {
        return 4;
    }

    std::array<DIDEVICEOBJECTDATA, 6>
        events{};

    events[0].dwOfs = DIMOFS_X;
    events[0].dwData =
        static_cast<DWORD>(15);

    events[1].dwOfs = DIMOFS_Y;
    events[1].dwData =
        static_cast<DWORD>(-6);

    events[2].dwOfs = DIMOFS_Z;
    events[2].dwData =
        static_cast<DWORD>(120);

    events[3].dwOfs = DIMOFS_BUTTON0;
    events[3].dwData = 0x80;

    events[4].dwOfs = DIMOFS_BUTTON1;
    events[4].dwData = 0x80;

    events[5].dwOfs = DIMOFS_BUTTON1;
    events[5].dwData = 0x00;

    std::uint32_t buttons = 0;

    if (!detail::DecodeBufferedEvents(
            sizeof(DIDEVICEOBJECTDATA),
            events.data(),
            static_cast<DWORD>(
                events.size()),
            0,
            &packet,
            &buttons) ||
        packet.x != 15 ||
        packet.y != -6 ||
        packet.wheel != 120 ||
        packet.buttons != 0x1u ||
        buttons != 0x1u)
    {
        return 5;
    }

    if (detail::DecodeBufferedEvents(
            sizeof(DIDEVICEOBJECTDATA) - 1,
            events.data(),
            static_cast<DWORD>(
                events.size()),
            0,
            &packet,
            &buttons))
    {
        return 6;
    }

    std::cout
        << "DirectInput mouse packet contract passed\n";

    return 0;
}
