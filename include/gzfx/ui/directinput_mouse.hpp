#pragma once

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif

#include <Windows.h>
#include <dinput.h>

#include <cstdint>

namespace gzfx::ui::directinput_mouse
{
    struct MousePacket
    {
        LONG x = 0;
        LONG y = 0;
        LONG wheel = 0;
        std::uint32_t buttons = 0;
    };

    struct Status
    {
        bool armed = false;
        bool directInput8CreateHooked = false;
        bool createDeviceHooked = false;
        bool systemMouseSeen = false;
        bool getDeviceStateHooked = false;
        bool getDeviceDataHooked = false;

        std::uintptr_t dinput8Module = 0;
        std::uintptr_t directInput8CreateTarget = 0;
        std::uintptr_t mouseDevice = 0;

        std::uint64_t directInput8CreateCalls = 0;
        std::uint64_t createDeviceCalls = 0;
        std::uint64_t stateReads = 0;
        std::uint64_t dataReads = 0;
        std::uint64_t menuPacketsForwarded = 0;
        std::uint64_t stateReadsSuppressed = 0;
        std::uint64_t dataReadsSuppressed = 0;

        LONG lastDeltaX = 0;
        LONG lastDeltaY = 0;
        LONG lastWheel = 0;
        std::uint32_t lastButtonMask = 0;
    };

    bool Arm() noexcept;
    Status GetStatus() noexcept;

    namespace detail
    {
        inline std::uint32_t MouseButtonMask(
            const BYTE* const buttons,
            const std::size_t count) noexcept
        {
            if (buttons == nullptr)
            {
                return 0;
            }

            std::uint32_t mask = 0;

            const std::size_t capped =
                count < 8u ? count : 8u;

            for (std::size_t i = 0;
                 i < capped;
                 ++i)
            {
                if ((buttons[i] & 0x80u) != 0u)
                {
                    mask |=
                        1u <<
                        static_cast<std::uint32_t>(i);
                }
            }

            return mask;
        }

        inline bool DecodeImmediateState(
            const DWORD byteCount,
            const void* const data,
            MousePacket* const outPacket) noexcept
        {
            if (data == nullptr ||
                outPacket == nullptr)
            {
                return false;
            }

            MousePacket packet{};

            if (byteCount ==
                sizeof(DIMOUSESTATE2))
            {
                const auto* const state =
                    static_cast<
                        const DIMOUSESTATE2*>(
                            data);

                packet.x = state->lX;
                packet.y = state->lY;
                packet.wheel = state->lZ;
                packet.buttons =
                    MouseButtonMask(
                        state->rgbButtons,
                        8u);
            }
            else if (byteCount ==
                     sizeof(DIMOUSESTATE))
            {
                const auto* const state =
                    static_cast<
                        const DIMOUSESTATE*>(
                            data);

                packet.x = state->lX;
                packet.y = state->lY;
                packet.wheel = state->lZ;
                packet.buttons =
                    MouseButtonMask(
                        state->rgbButtons,
                        4u);
            }
            else
            {
                return false;
            }

            *outPacket = packet;
            return true;
        }

        inline bool DecodeBufferedEvents(
            const DWORD objectDataSize,
            const DIDEVICEOBJECTDATA* const events,
            const DWORD count,
            const std::uint32_t initialButtons,
            MousePacket* const outPacket,
            std::uint32_t* const outButtons) noexcept
        {
            if (outPacket == nullptr ||
                outButtons == nullptr ||
                objectDataSize !=
                    sizeof(DIDEVICEOBJECTDATA))
            {
                return false;
            }

            MousePacket packet{};
            std::uint32_t buttons =
                initialButtons;

            if (events != nullptr)
            {
                for (DWORD i = 0;
                     i < count;
                     ++i)
                {
                    const auto& event =
                        events[i];

                    if (event.dwOfs ==
                        DIMOFS_X)
                    {
                        packet.x +=
                            static_cast<LONG>(
                                event.dwData);
                    }
                    else if (event.dwOfs ==
                             DIMOFS_Y)
                    {
                        packet.y +=
                            static_cast<LONG>(
                                event.dwData);
                    }
                    else if (event.dwOfs ==
                             DIMOFS_Z)
                    {
                        packet.wheel +=
                            static_cast<LONG>(
                                event.dwData);
                    }
                    else if (event.dwOfs >=
                                 DIMOFS_BUTTON0 &&
                             event.dwOfs <=
                                 DIMOFS_BUTTON7)
                    {
                        const std::uint32_t index =
                            static_cast<std::uint32_t>(
                                event.dwOfs -
                                DIMOFS_BUTTON0);

                        const std::uint32_t bit =
                            1u << index;

                        if ((event.dwData &
                             0x80u) != 0u)
                        {
                            buttons |= bit;
                        }
                        else
                        {
                            buttons &= ~bit;
                        }
                    }
                }
            }

            packet.buttons = buttons;
            *outPacket = packet;
            *outButtons = buttons;
            return true;
        }
    }
}
