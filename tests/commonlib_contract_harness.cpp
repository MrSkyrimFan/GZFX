// CommonLib's own consumer contract requires its PCH plus global
// std::literals before public RE headers.
#include <SKSE/Impl/PCH.h>
using namespace std::literals;
#include <RE/Skyrim.h>

#include "gzfx/skse_api.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>

int main()
{
    static_assert(
        SKSE::RUNTIME_SSE_1_7_104.pack() ==
        0x01070680u);

    static_assert(
        sizeof(RE::Sky) ==
        0x2C8);

    static_assert(
        sizeof(gzfx::skse::Interface) ==
        sizeof(SKSE::detail::SKSEInterface));

    static_assert(
        offsetof(gzfx::skse::Interface, skseVersion) ==
        offsetof(SKSE::detail::SKSEInterface, skseVersion));
    static_assert(
        offsetof(gzfx::skse::Interface, runtimeVersion) ==
        offsetof(SKSE::detail::SKSEInterface, runtimeVersion));
    static_assert(
        offsetof(gzfx::skse::Interface, QueryInterface) ==
        offsetof(SKSE::detail::SKSEInterface, QueryInterface));
    static_assert(
        offsetof(gzfx::skse::Interface, GetPluginHandle) ==
        offsetof(SKSE::detail::SKSEInterface, GetPluginHandle));
    static_assert(
        offsetof(gzfx::skse::Interface, GetReleaseIndex) ==
        offsetof(SKSE::detail::SKSEInterface, GetReleaseIndex));
    static_assert(
        offsetof(gzfx::skse::Interface, GetPluginInfo) ==
        offsetof(SKSE::detail::SKSEInterface, GetPluginInfo));

    static_assert(
        sizeof(gzfx::skse::PluginVersionData) ==
        sizeof(SKSE::PluginVersionData));

    if (SKSE::RUNTIME_SSE_1_7_104 !=
        REL::Version{ 1, 7, 104, 0 })
    {
        return 2;
    }

    // Taking and printing the addresses forces the linker to resolve
    // real CommonLib object-code symbols without invoking Skyrim-only
    // relocation machinery in this standalone CI process.
    const auto rendererSymbol =
        reinterpret_cast<std::uintptr_t>(
            &RE::BSGraphics::Renderer::
                GetSingleton);

    const auto skySymbol =
        reinterpret_cast<std::uintptr_t>(
            &RE::Sky::GetSingleton);

    const auto dataHandlerSymbol =
        reinterpret_cast<std::uintptr_t>(
            &RE::TESDataHandler::
                GetSingleton);

    if (rendererSymbol == 0 ||
        skySymbol == 0 ||
        dataHandlerSymbol == 0)
    {
        return 3;
    }

    std::cout
        << "CommonLib MIT 1.7.104 contract passed"
        << " renderer=0x"
        << std::hex
        << rendererSymbol
        << " sky=0x"
        << skySymbol
        << " data=0x"
        << dataHandlerSymbol
        << "\n";

    return 0;
}
