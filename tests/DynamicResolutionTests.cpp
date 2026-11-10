// DynamicResolutionController: decisions from simulated GPU costs (fixed + per-pixel), including the one-frame lag
// between choosing a scale and observing it.
#include "PlutoGE/render/DynamicResolution.h"

#include <cmath>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace PlutoGE::render;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    struct Simulation
    {
        float fixedMs = 2.0f, pixelMs = 20.0f; // cost at scale 1
        std::deque<float> inFlight;            // scales of frames whose results have not arrived
        std::uint32_t changes = 0;

        float Cost(float scale) const { return fixedMs + pixelMs * scale * scale; }

        // Runs `frames` frames, results arriving two frames late, and returns the final scale.
        float Run(DynamicResolutionController &controller, int frames)
        {
            float scale = controller.Scale();
            for (int frame = 0; frame < frames; ++frame)
            {
                inFlight.push_back(scale);
                if (inFlight.size() > 2)
                {
                    const float rendered = inFlight.front();
                    inFlight.pop_front();
                    const float next = controller.Observe(Cost(rendered), rendered);
                    if (next != scale)
                        ++changes;
                    scale = next;
                }
            }
            return scale;
        }
    };

    DynamicResolutionSettings Enabled(float target)
    {
        DynamicResolutionSettings settings;
        settings.enabled = true;
        settings.targetGpuMs = target;
        return settings;
    }

    void CheckDisabled()
    {
        DynamicResolutionSettings settings;
        DynamicResolutionController controller(settings);
        for (int i = 0; i < 100; ++i)
            Require(controller.Observe(100.0f, 1.0f) == 1.0f, "A disabled controller never changes the scale");
    }

    void CheckLowersToFit()
    {
        // 22 ms at full scale against a 16.6 ms target: about 0.83 fits.
        DynamicResolutionController controller(Enabled(16.6f));
        Simulation simulation;
        const float scale = simulation.Run(controller, 400);
        Require(simulation.Cost(scale) <= 16.6f, "The scale settles where the frame fits: " + std::to_string(scale));
        Require(scale >= 0.75f, "The scale is not lowered further than needed: " + std::to_string(scale));
        Require(simulation.changes <= 3, "Settling takes few changes: " + std::to_string(simulation.changes));
    }

    void CheckRaisesWhenLoadDrops()
    {
        DynamicResolutionController controller(Enabled(16.6f));
        Simulation simulation;
        simulation.pixelMs = 40.0f;
        const float heavy = simulation.Run(controller, 400);
        Require(heavy < 0.7f, "Heavy load lowers the scale: " + std::to_string(heavy));
        simulation.pixelMs = 8.0f;
        const float light = simulation.Run(controller, 4000);
        Require(light == 1.0f, "Light load returns to full scale: " + std::to_string(light));
    }

    void CheckStableUnderNoise()
    {
        DynamicResolutionController controller(Enabled(16.6f));
        // Sitting just under the target with +-8% noise: no oscillation once settled.
        Simulation simulation;
        simulation.pixelMs = 13.0f;
        simulation.Run(controller, 50);
        const auto before = simulation.changes;
        float scale = controller.Scale();
        for (int frame = 0; frame < 2000; ++frame)
        {
            const float noise = 1.0f + 0.08f * std::sin(frame * 1.7f);
            const float next = controller.Observe(simulation.Cost(scale) * noise, scale);
            if (next != scale)
                ++simulation.changes;
            scale = next;
        }
        Require(simulation.changes - before <= 2, "Noise near the target does not oscillate: " +
                                                      std::to_string(simulation.changes - before));
    }

    void CheckLimits()
    {
        auto settings = Enabled(1.0f);
        settings.minimumScale = 0.6f;
        DynamicResolutionController controller(settings);
        Simulation simulation;
        const float scale = simulation.Run(controller, 1000);
        Require(std::abs(scale - 0.6f) < 1e-4f, "The scale never drops below the minimum: " + std::to_string(scale));
        Require(ScaledRenderExtent(1920, 0.5f) == 960 && ScaledRenderExtent(1080, 0.73f) == 792 &&
                    ScaledRenderExtent(1080, 1.0f) == 1080 && ScaledRenderExtent(6, 0.5f) == 6,
                "Render extents are aligned and bounded");
    }
}

int main()
{
    try
    {
        CheckDisabled();
        CheckLowersToFit();
        CheckRaisesWhenLoadDrops();
        CheckStableUnderNoise();
        CheckLimits();
        std::cout << "Dynamic resolution tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
