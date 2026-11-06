#include "gzfx/render/product_control_mapping.hpp"

#include <cmath>
#include <iostream>

namespace
{
    bool Near(
        const float a,
        const float b,
        const float epsilon = 1e-5f)
    {
        return std::fabs(a - b) <= epsilon;
    }
}

int main()
{
    using namespace gzfx::render::product_control;

    if (!Near(
            HorizonStrengthMultiplier(0.25f),
            1.0f) ||
        !Near(
            HorizonStrengthMultiplier(0.0f),
            0.0f) ||
        !Near(
            HorizonStrengthMultiplier(1.0f),
            4.0f))
    {
        return 2;
    }

    if (!Near(
            HorizonRadiusWorld(28.0f),
            240.0f) ||
        !Near(
            HorizonRadiusWorld(0.0f),
            48.0f) ||
        !Near(
            HorizonRadiusWorld(200.0f),
            960.0f))
    {
        return 3;
    }

    const auto q0 = HorizonBudget(0);
    const auto q1 = HorizonBudget(1);
    const auto q2 = HorizonBudget(2);
    const auto q3 = HorizonBudget(3);
    const auto q9 = HorizonBudget(9);

    if (q0.slices != 2 || q0.steps != 4 ||
        q1.slices != 3 || q1.steps != 4 ||
        q2.slices != 3 || q2.steps != 6 ||
        q3.slices != 4 || q3.steps != 8 ||
        q9.slices != 4 || q9.steps != 8)
    {
        return 4;
    }

    if (!LegacyAoEnabled(true, false) ||
        LegacyAoEnabled(true, true) ||
        LegacyAoEnabled(false, false))
    {
        return 5;
    }

    std::cout
        << "Product control binding contract passed\n";

    return 0;
}
