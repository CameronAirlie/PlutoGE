#include "PlutoGE/ui/BrushStrokeSampler.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

int main()
try
{
    const auto require = [](bool value, const char *message) {
        if (!value) throw std::runtime_error(message);
    };
    const auto replay = [](int frames) {
        PlutoGE::ui::BrushStrokeSampler sampler;
        std::vector<glm::vec2> dabs;
        for (int i = 0; i <= frames; ++i)
            sampler.Sample({24.0f * i / frames, 0}, 0.5f,
                [&](glm::vec2 point) { dabs.push_back(point); });
        return dabs;
    };
    const auto baseline = replay(30);
    require(baseline.size() == 49, "Stroke missed a regularly spaced dab");
    for (int frames : {60, 144})
    {
        const auto dabs = replay(frames);
        require(dabs.size() == baseline.size(), "Dab count depends on input frequency");
        for (std::size_t i = 0; i < dabs.size(); ++i)
            require(glm::distance(dabs[i], baseline[i]) < 0.0001f, "Dab locations depend on input frequency");
    }
    PlutoGE::ui::BrushStrokeSampler sampler;
    std::vector<glm::vec2> dabs;
    const auto emit = [&](glm::vec2 point) { dabs.push_back(point); };
    for (int i = 0; i < 100; ++i) sampler.Sample({0, 0}, 1, emit);
    require(dabs.size() == 1, "Stationary input grows foliage");
    sampler.Sample({0.5f, 0}, 1, emit);
    sampler.Sample({0.5f, 0.5f}, 1, emit);
    require(dabs.size() == 2 && glm::distance(dabs.back(), glm::vec2(.5f, .5f)) < .0001f,
        "Stroke did not carry remaining distance around a corner");
    sampler.Reset();
    sampler.Sample({50, 50}, 1, emit);
    require(dabs.size() == 3 && dabs.back() == glm::vec2(50), "Reset bridged an invalid surface gap");
    sampler.Sample({10000, 50}, 1, emit);
    require(dabs.size() == 4 && dabs.back().x == 10000, "Discontinuous cursor motion exceeded the work budget");
    sampler.Sample({10000, 50}, 0, emit);
    sampler.Sample({10000, 50}, 1, emit);
    require(dabs.size() == 5, "Invalid spacing did not reset the stroke");
    sampler.Sample({std::numeric_limits<float>::quiet_NaN(), 0}, 1, emit);
    sampler.Sample({1, 1}, 1, emit);
    require(dabs.size() == 6, "Non-finite input did not reset the stroke");
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
