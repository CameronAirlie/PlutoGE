#include "PlutoGE/render/RmlFontRasterScale.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using PlutoGE::render::RmlFontRasterScale;

namespace
{
    void Require(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    constexpr float kFrame = 1.0f / 60.0f;
}

int main() try
{
    {
        RmlFontRasterScale scale;
        Require(scale.Update(2.0f, 0.0f) == 2, "The first requirement should set the density");
        Require(scale.Update(2.4f, kFrame) == 3, "Visibly magnified text should raise the density at once");
    }
    {
        // A distance-scaled world label hovering around a bucket boundary
        // previously released every font atlas each time it crossed.
        RmlFontRasterScale scale;
        scale.Update(2.0f, 0.0f);
        int changes = 0, previous = scale.Current();
        for (int frame = 0; frame < 600; ++frame)
        {
            const int current = scale.Update(frame % 2 ? 2.08f : 1.94f, kFrame);
            changes += current != previous;
            previous = current;
        }
        Require(changes == 0, "Small oscillations around a bucket boundary must not change the density");
    }
    {
        RmlFontRasterScale scale;
        scale.Update(3.0f, 0.0f);
        float elapsed = 0.0f;
        while (elapsed + kFrame < RmlFontRasterScale::kFallDelaySeconds - 0.1f)
        {
            Require(scale.Update(2.0f, kFrame) == 3, "Density should not fall before the delay");
            elapsed += kFrame;
        }
        // A brief return to the higher requirement restarts the delay.
        scale.Update(2.9f, kFrame);
        for (int frame = 0; frame < 10; ++frame)
            Require(scale.Update(2.0f, kFrame) == 3, "A renewed need should restart the fall delay");
        for (int frame = 0; frame < int(RmlFontRasterScale::kFallDelaySeconds / kFrame) + 2; ++frame)
            scale.Update(2.0f, kFrame);
        Require(scale.Current() == 2, "A persistently lower requirement should release the denser atlas");
    }
    {
        RmlFontRasterScale scale;
        Require(scale.Update(std::numeric_limits<float>::quiet_NaN(), 0.0f) == 1, "Invalid requirements should use 1x");
        Require(scale.Update(40.0f, kFrame) == RmlFontRasterScale::kMaximum, "Density should be bounded");
    }
    std::cout << "RmlUi font raster scale policy tests passed\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
