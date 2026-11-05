#include <REL/Version.h>
#include <RE/R/Renderer.h>
#include <RE/S/Sky.h>
#include <RE/T/TESDataHandler.h>
#include <SKSE/Version.h>

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
