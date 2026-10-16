#include "ParticleDrawOrder.h"

#include <iostream>
#include <stdexcept>

using namespace PlutoGE::render;

namespace
{
    void Require(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    // Emitter-local, already back-to-front, as RhiSceneRenderer prepares them.
    BasicParticleDraw Emitter(float tag, std::initializer_list<float> depths, std::uint32_t index,
                              std::vector<ParticleInstanceKey> &keys)
    {
        BasicParticleDraw draw;
        draw.parameters.values[0] = glm::vec4(tag);
        for (const float depth : depths)
        {
            keys.push_back({depth, index, static_cast<std::uint32_t>(draw.instances.size())});
            BasicParticleInstance instance;
            instance.centerRotation.z = -depth;
            draw.instances.push_back(instance);
        }
        return draw;
    }
}

int main() try
{
    std::vector<ParticleInstanceKey> keys;
    std::vector<BasicParticleDraw> emitters;
    // Two plumes interleaved in depth, and a separate trail draw.
    emitters.push_back(Emitter(1, {90, 50, 10}, 0, keys));
    emitters.push_back(Emitter(2, {70, 30}, 1, keys));
    BasicParticleDraw trail;
    trail.parameters.values[0] = glm::vec4(3);
    trail.vertices.resize(6);
    emitters.push_back(trail);

    std::vector<BasicParticleDraw> ordered;
    const auto count = OrderParticleDraws(emitters, keys, ordered);
    Require(count == 6, "Interleaved emitters must split into one draw per run plus the trail");
    const float expectedTags[] = {1, 2, 1, 2, 1, 3};
    const float expectedDepths[] = {90, 70, 50, 30, 10};
    for (std::size_t index = 0; index < count; ++index)
        Require(ordered[index].parameters.values[0].x == expectedTags[index], "A run lost its emitter parameters");
    for (std::size_t index = 0; index < 5; ++index)
        Require(ordered[index].instances.size() == 1 &&
                ordered[index].instances[0].centerRotation.z == -expectedDepths[index], "Billboards are not back-to-front");
    Require(ordered[5].vertices.size() == 6 && ordered[5].instances.empty(), "Trail vertices were not preserved");

    // Separated emitters stay one draw each, with reused output storage.
    keys.clear();
    emitters.clear();
    emitters.push_back(Emitter(1, {20, 10}, 0, keys));
    emitters.push_back(Emitter(2, {80, 60}, 1, keys));
    Require(OrderParticleDraws(emitters, keys, ordered) == 2, "Separated emitters must not be split");
    Require(ordered[0].parameters.values[0].x == 2 && ordered[0].instances.size() == 2 &&
            ordered[1].parameters.values[0].x == 1 && ordered[1].instances.size() == 2 &&
            ordered[1].vertices.empty(), "Separated emitters must draw far to near without stale data");

    keys.clear();
    Require(OrderParticleDraws({}, keys, ordered) == 0, "No particles must produce no draws");
    std::cout << "Particle draw order tests passed\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
