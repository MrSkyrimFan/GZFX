#include "gzfx/render/wave3_math.hpp"

#include <cmath>
#include <iostream>

int main()
{
    using namespace gzfx::render::wave3_math;

    if (MipCount(2560, 1440) != 12 ||
        MipCount(1, 1) != 1 ||
        MipCount(3, 5) != 3)
    {
        return 1;
    }

    const Float2 current{0.25f, 0.75f};
    const Float2 motion{0.10f, -0.20f};
    const Float2 previous = PreviousUv(current, motion);

    if (std::abs(previous.x - 0.35f) > 1e-6f ||
        std::abs(previous.y - 0.55f) > 1e-6f)
    {
        return 2;
    }

    const Float2 previousDr =
        DynamicResolutionUv(previous, Float2{0.75f, 0.50f});

    if (std::abs(previousDr.x - 0.2625f) > 1e-6f ||
        std::abs(previousDr.y - 0.275f) > 1e-6f)
    {
        return 3;
    }

    if (!LogicalUvInBounds(Float2{0.0f, 1.0f}) ||
        LogicalUvInBounds(Float2{-0.001f, 0.5f}) ||
        LogicalUvInBounds(Float2{0.5f, 1.001f}))
    {
        return 4;
    }

    const float pixels =
        MotionPixels(Float2{0.01f, 0.02f}, 1000, 500);

    if (std::abs(pixels - std::sqrt(200.0f)) > 1e-4f)
    {
        return 5;
    }

    if (HistoryFrameCap(0.0f) != 16.0f ||
        HistoryFrameCap(64.0f) != 4.0f ||
        !(HistoryFrameCap(8.0f) < 16.0f &&
          HistoryFrameCap(8.0f) > 4.0f))
    {
        return 6;
    }

    const auto normalA =
        ContractToken(
            3,
            3,
            2560,
            1440,
            1,
            1,
            1,
            0,
            28,
            28);

    const auto normalB =
        ContractToken(
            3,
            3,
            2560,
            1440,
            1,
            1,
            1,
            0,
            28,
            28);

    const auto changedSize =
        ContractToken(
            3,
            3,
            1920,
            1080,
            1,
            1,
            1,
            0,
            28,
            28);

    const auto changedView =
        ContractToken(
            3,
            3,
            2560,
            1440,
            1,
            1,
            1,
            0,
            28,
            10);

    if (normalA == 0 ||
        normalA != normalB ||
        normalA == changedSize ||
        normalA == changedView)
    {
        return 7;
    }

    std::cout << "Wave 3 math contract passed.\n";
    return 0;
}