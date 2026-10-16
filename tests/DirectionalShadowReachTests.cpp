#include "DirectionalShadowReach.h"

#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace PlutoGE::render;

namespace
{
    void Require(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }
}

int main() try
{
    const glm::vec3 camera(0, 2, 0);
    const auto projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 500.0f);
    // Sun shining straight down: casters are above receivers.
    const glm::vec3 down(0, -1, 0);
    const auto reach = DirectionalShadowReach::Create(camera, projection, down, 40.0f, 30.0f);
    Require(reach.has_value(), "Perspective reach was rejected");

    // Receivers within the shadow distance, including the frustum's far corners.
    Require(reach->Contains({0, 2, -40}, 1), "Caster at the shadow distance was culled");
    const float tanY = std::tan(glm::radians(30.0f)), tanX = tanY * 16.0f / 9.0f;
    Require(reach->Contains(camera + glm::vec3(40 * tanX, 40 * tanY, -40), 0.01f), "Far frustum corner was culled");
    // Upstream along the light (above) by up to the caster distance.
    Require(reach->Contains({0, 2 + 30 + 40, 0}, 0.5f), "Upstream caster was culled");
    // Beyond the receiver region and not upstream of it.
    Require(!reach->Contains({0, 2, -200}, 1), "Distant caster was kept");
    Require(!reach->Contains({0, 2 - 200, 0}, 1), "Downstream caster was kept");
    // A large radius reaches back into the region.
    Require(reach->Contains({0, 2, -200}, 160), "Bounds radius was ignored");

    // Unknown bounds are never culled.
    Require(reach->Contains({0, 2, -200}, 0), "Zero radius bounds were culled");
    Require(reach->Contains({std::numeric_limits<float>::quiet_NaN(), 0, 0}, 1), "Non-finite bounds were culled");

    // No safe bound exists for these inputs.
    Require(!DirectionalShadowReach::Create(camera, glm::orthoRH_ZO(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 100.0f), down, 40, 30),
        "Orthographic projection produced a reach");
    Require(!DirectionalShadowReach::Create(camera, projection, glm::vec3(0), 40, 30), "Zero light direction produced a reach");
    Require(!DirectionalShadowReach::Create(camera, projection, down, std::numeric_limits<float>::infinity(), 30),
        "Infinite shadow distance produced a reach");
    std::cout << "Directional shadow reach tests passed\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
