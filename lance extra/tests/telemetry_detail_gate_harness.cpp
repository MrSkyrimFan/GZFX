#include <Windows.h>

#include "gzfx/diagnostics/telemetry_listener.hpp"
#include "gzfx/render/stage_scheduler.hpp"

#include <cstdint>
#include <iostream>

using StageFn = std::uint32_t (*)(
    std::uint32_t,
    std::uint32_t);

using PresentFn = std::uint32_t (*)(
    std::uint32_t);

int wmain(
    int argc,
    wchar_t** argv)
{
    if (argc != 2)
    {
        return 2;
    }

    const HMODULE module =
        LoadLibraryW(argv[1]);

    if (module == nullptr)
    {
        return 3;
    }

    const auto stageAction =
        reinterpret_cast<StageFn>(
            GetProcAddress(
                module,
                "GZFX_TestTelemetryDetailGateStage"));

    const auto presentAction =
        reinterpret_cast<PresentFn>(
            GetProcAddress(
                module,
                "GZFX_TestTelemetryDetailGatePresent"));

    if (stageAction == nullptr ||
        presentAction == nullptr)
    {
        FreeLibrary(module);
        return 4;
    }

    using B =
        gzfx::render::stage_scheduler::Boundary;

    using P =
        gzfx::render::stage_scheduler::Phase;

    using PP =
        gzfx::diagnostics::telemetry_listener::
            PresentPhase;

    constexpr std::uint32_t none = 0;
    constexpr std::uint32_t start = 1;
    constexpr std::uint32_t stop = 2;

    const bool startsAtAmbient =
        stageAction(
            static_cast<std::uint32_t>(
                B::AfterAmbientScreenSpaceComposite),
            static_cast<std::uint32_t>(
                P::AfterExecution)) ==
            start;

    const bool uiPresentDoesNotStop =
        stageAction(
            static_cast<std::uint32_t>(
                B::UiPresent),
            static_cast<std::uint32_t>(
                P::Landmark)) ==
            none;

    const bool callbacksDoNotStop =
        presentAction(
            static_cast<std::uint32_t>(
                PP::CallbacksBegin)) ==
            none &&
        presentAction(
            static_cast<std::uint32_t>(
                PP::CallbacksEnd)) ==
            none;

    const bool physicalPresentStops =
        presentAction(
            static_cast<std::uint32_t>(
                PP::BeforeOriginalPresent)) ==
            stop;

    const bool afterPresentNoSecondStop =
        presentAction(
            static_cast<std::uint32_t>(
                PP::AfterOriginalPresent)) ==
            none;

    FreeLibrary(module);

    if (!startsAtAmbient ||
        !uiPresentDoesNotStop ||
        !callbacksDoNotStop ||
        !physicalPresentStops ||
        !afterPresentNoSecondStop)
    {
        return 5;
    }

    std::wcout
        << L"Telemetry physical-present detail gate contract passed.\n";

    return 0;
}
