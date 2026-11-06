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

    if (ClassifyShadowFlags(
            true,
            false,
            false,
            false) !=
        ShadowKind::Directional)
    {
        return 2;
    }

    if (ClassifyShadowFlags(
            false,
            true,
            false,
            false) !=
        ShadowKind::Spot)
    {
        return 3;
    }

    if (ClassifyShadowFlags(
            false,
            false,
            true,
            true) !=
        ShadowKind::Omni)
    {
        return 4;
    }

    if (ClassifyShadowFlags(
            false,
            false,
            false,
            false) !=
        ShadowKind::Unknown)
    {
        return 5;
    }

    if (static_cast<std::uint32_t>(
            Anchor::PostProcessing) != 0 ||
        static_cast<std::uint32_t>(
            Anchor::PreUi) != 1 ||
        static_cast<std::uint32_t>(
            Anchor::Count) < 22)
    {
        return 6;
    }

    std::cout
        << "Native domain anchor contracts passed.\n";

    return 0;
}
