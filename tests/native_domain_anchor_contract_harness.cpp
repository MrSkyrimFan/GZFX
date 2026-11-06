#include "gzfx/engine/native_domain_anchors.hpp"
#include "gzfx/diagnostics/telemetry_listener.hpp"

#include <cstdint>
#include <iostream>

int main()
{
    using gzfx::engine::native_domain_anchors::
        ClassifyShadowFlags;
    using gzfx::engine::native_domain_anchors::
        ShadowKind;

    using Anchor =
        gzfx::diagnostics::telemetry_listener::
            EngineAnchor;

    static_assert(
        ClassifyShadowFlags(
            true,
            false,
            false,
            false) ==
        ShadowKind::Directional);

    static_assert(
        ClassifyShadowFlags(
            false,
            true,
            false,
            false) ==
        ShadowKind::Spot);

    static_assert(
        ClassifyShadowFlags(
            false,
            false,
            true,
            true) ==
        ShadowKind::Omni);

    static_assert(
        ClassifyShadowFlags(
            false,
            false,
            false,
            false) ==
        ShadowKind::Unknown);

    static_assert(
        static_cast<std::uint32_t>(
            Anchor::PostProcessing) == 0);

    static_assert(
        static_cast<std::uint32_t>(
            Anchor::PreUi) == 1);

    static_assert(
        static_cast<std::uint32_t>(
            Anchor::Count) >= 29);

    std::cout
        << "Native domain anchor contracts passed.\n";

    return 0;
}
