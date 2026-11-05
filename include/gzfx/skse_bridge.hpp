#pragma once

#include "gzfx/skse_api.hpp"

namespace gzfx::skse_bridge
{
    skse::PluginHandle Plugin() noexcept;
    const skse::MessagingInterface* Messaging() noexcept;
    const skse::TrampolineInterface* Trampoline() noexcept;
    bool EarlyGraphicsArmed() noexcept;
}
