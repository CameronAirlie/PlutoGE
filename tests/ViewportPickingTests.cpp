#include "PlutoGE/ui/ViewportPicking.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>

int main()
try
{
    using namespace PlutoGE::ui;
    const auto require = [](bool condition, const char *message) {
        if (!condition) throw std::runtime_error(message);
    };
    // Aim at the center of a local-space square. Its world size must not
    // decide whether the picker can reach the bounds/triangle tests.
    for (float scale : {0.001f, 1.0f, 99.0f, 100.0f, 101.0f, 300.0f, 10000.0f, 1.0e10f})
        for (bool nonuniform : {false, true})
        {
            const glm::mat4 world = glm::translate(glm::mat4(1), glm::vec3(4, -2, 7)) *
                glm::rotate(glm::mat4(1), .7f, glm::normalize(glm::vec3(1, 2, 3))) *
                glm::scale(glm::mat4(1), nonuniform ? glm::vec3(-scale, scale * 2, scale * .5f) : glm::vec3(scale));
            const glm::vec3 target(world * glm::vec4(0, 0, 0, 1));
            const glm::vec3 origin(world * glm::vec4(.2f, .3f, 2, 1));
            const ViewportPickRay ray{origin, glm::normalize(target - origin)};
            const auto local = TransformViewportPickRay(ray, world);
            require(local.has_value(), "A valid scaled mesh became unpickable");
            require(std::abs(glm::length(local->direction) - 1) < 1e-5f, "Local picking ray is not normalized");
            const float distance = -local->origin.z / local->direction.z;
            const glm::vec3 hit = local->origin + distance * local->direction;
            require(distance > 0 && glm::length(hit) < .002f, "Scaled picking ray missed the local surface");
            const glm::vec3 worldHit(world * glm::vec4(hit, 1));
            require(glm::length(worldHit - target) < std::max(.002f, scale * .002f),
                "Picking hit did not transform back onto the world surface");
        }
    const ViewportPickRay ray{{0, 0, 2}, {0, 0, -1}};
    require(!TransformViewportPickRay(ray, glm::scale(glm::mat4(1), glm::vec3(1, 0, 1))),
        "Singular transforms should not produce picking hits");
    require(!TransformViewportPickRay({ray.origin, glm::vec3(0)}, glm::mat4(1)),
        "A zero direction was accepted");
    require(!TransformViewportPickRay({ray.origin, glm::vec3(std::numeric_limits<float>::infinity())}, glm::mat4(1)),
        "A nonfinite direction was accepted");
    std::cout << "Scaled viewport picking checks passed\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
