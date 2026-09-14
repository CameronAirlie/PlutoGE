#include "PlutoGE/scene/components/SplineComponent.h"
#include "PlutoGE/render/Mesh.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace PlutoGE::scene;
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
int main()
{
    try
    {
        SplineComponentConfig config;
        config.points = {{{0, 0, 0}, {0, 0, 30}}, {{0, 0, 20}, {0, 0, 30}}};
        config.closed = false;
        config.guardrailHeight = 1;
        SplineComponent road(config);
        road.Rebuild();
        Check(road.GetGeneratedMesh() && road.GetGeneratedCollisionMesh(), "Road generation failed");
        const auto visual = road.GetGeneratedMesh()->GetMeshData();
        const auto collision = road.GetGeneratedCollisionMesh()->GetMeshData();
        for (unsigned vertex = 0; vertex < 4; ++vertex)
            for (unsigned axis = 0; axis < 3; ++axis)
                Check(std::abs(visual.vertices[vertex].position[axis] - collision.vertices[vertex].position[axis]) < 0.0001f, "Collision lost road banking");
        Check(std::abs(collision.vertices[0].position[1] - collision.vertices[1].position[1]) > 1, "Bank was flattened");
        const auto samples = road.GetCollisionPathPoints().size();
        Check(collision.vertices.size() == samples * 8, "Guardrail vertices missing");
        Check(collision.indices.size() == (samples - 1) * 30, "Guardrail collision triangles missing");
        road.Rebuild();
        const auto &rebuilt = road.GetGeneratedCollisionMesh()->GetMeshData();
        Check(rebuilt.indices == collision.indices && rebuilt.vertices.size() == collision.vertices.size(), "Non-deterministic topology");
        for (std::size_t vertex = 0; vertex < rebuilt.vertices.size(); ++vertex)
            for (unsigned axis = 0; axis < 3; ++axis)
                Check(rebuilt.vertices[vertex].position[axis] == collision.vertices[vertex].position[axis], "Non-deterministic positions");
        SplineComponent restored;
        restored.Deserialize(road.Serialize());
        Check(restored.GetGuardrailHeight() == 1 && restored.GetGeneratedCollisionMesh()->GetMeshData().indices == collision.indices, "Road serialization lost guardrails");
        road.SetGuardrailHeight(0);
        road.Rebuild();
        Check(road.GetGeneratedCollisionMesh()->GetMeshData().vertices.size() == samples * 4, "Guardrail removal failed");
        std::cout << "Road banking, guardrail collision, determinism and serialization tests passed\n";
    }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
