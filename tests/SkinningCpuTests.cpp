#include "ReferenceSkinning.h"
#include "RhiSkinning.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

int main() try
{
    using namespace PlutoGE::render;
    std::vector<MeshVertexData> vertices(73683);
    std::array<glm::mat4, 4> joints;
    for (unsigned i = 0; i < joints.size(); ++i)
        joints[i] = glm::translate(glm::mat4(1), glm::vec3(.1f * i, .2f * i, -.05f * i)) *
            glm::rotate(glm::mat4(1), .17f * i, glm::vec3(0, 1, 0));
    for (std::size_t i = 0; i < vertices.size(); ++i)
    {
        auto &vertex = vertices[i];
        vertex.position = {float(i % 197) * .001f, .3f, -.2f};
        vertex.normal = {0, 1, 0};
        vertex.tangent = {1, 0, 0, 1};
        vertex.joints = {0, 1, 2, 3};
        vertex.weights = {.4f, .3f, .2f, .1f};
    }
    const auto reference = ReferenceSkinRhiVertices(vertices, joints);
    std::vector<BasicVertex> output;
    SkinRhiVerticesInto(vertices, joints, {}, output);
    const auto check = [&](const auto &actual)
    {
        for (std::size_t i = 0; i < vertices.size(); ++i)
            for (unsigned c = 0; c < 3; ++c)
                if (std::abs(actual[i].position[c] - reference[i].position[c]) > 1e-5f ||
                    std::abs(actual[i].normal[c] - reference[i].normal[c]) > 1e-5f ||
                    std::abs(actual[i].tangent[c] - reference[i].tangent[c]) > 1e-5f)
                    throw std::runtime_error("Skinning differs from independent reference");
    };
    check(output);
    RhiSkinningExecutor executor(4);
    executor.Deform(vertices, joints, output, output);
    check(output);
    for (std::size_t i = 0; i < output.size(); ++i)
        for (unsigned c = 0; c < 3; ++c)
            if (std::abs(output[i].previousPosition[c] - reference[i].position[c]) > 1e-5f)
                throw std::runtime_error("Aliased skinning history was not preserved");
    for (int run = 0; run < 3; ++run)
    {
        const auto start = std::chrono::steady_clock::now();
        for (int frame = 0; frame < 20; ++frame)
            SkinRhiVerticesInto(vertices, joints, output, output);
        std::cout << "CPU skinning 73683 vertices: " <<
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / 20
            << " ms/frame\n";
    }
    return 0;
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
