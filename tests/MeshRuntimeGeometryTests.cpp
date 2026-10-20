#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/MeshComponent.h"

#include <iostream>
#include <memory>

namespace
{
    PlutoGE::render::MeshData Quad(float size)
    {
        PlutoGE::render::MeshData data;
        for (const auto &[x, z] : {std::pair{0.0f, 0.0f}, std::pair{size, 0.0f}, std::pair{size, size}, std::pair{0.0f, size}})
        {
            PlutoGE::render::MeshVertexData vertex{};
            vertex.position = {x, 0.0f, z};
            vertex.normal = {0.0f, 1.0f, 0.0f};
            vertex.uv = {x / size, z / size};
            vertex.tangent = {0.0f, 0.0f, 0.0f, 0.0f};
            data.vertices.push_back(vertex);
        }
        data.indices = {0, 2, 1, 0, 3, 2};
        return data;
    }

    int Fail(const char *message)
    {
        std::cerr << message << '\n';
        return 1;
    }
}

int main()
{
    using namespace PlutoGE::scene;

    Scene scene;
    auto *owner = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Vision cone"}));
    PlutoGE::render::Mesh authored(PlutoGE::render::MeshConfig{.data = Quad(1.0f)});
    auto *mesh = owner->CreateComponent<MeshComponent>(MeshComponentConfig{.mesh = &authored});

    // Invalid triangle lists are refused without touching the authored mesh.
    auto broken = Quad(1.0f);
    broken.indices = {0, 1, 7};
    if (mesh->SetRuntimeGeometry(broken) || mesh->HasRuntimeGeometry() || mesh->GetMesh() != &authored)
        return Fail("An invalid triangle list replaced the mesh.");
    broken.indices = {0, 1};
    if (mesh->SetRuntimeGeometry(broken))
        return Fail("A partial triangle was accepted.");

    if (!mesh->SetRuntimeGeometry(Quad(2.0f)) || !mesh->HasRuntimeGeometry())
        return Fail("Runtime geometry was not applied.");
    auto *runtime = mesh->GetMesh();
    if (!runtime || runtime == &authored || runtime->GetMeshData().indices.size() != 6 || runtime->GetSubmeshCount() != 1)
        return Fail("Runtime geometry is not the drawn mesh.");
    const auto firstRevision = runtime->GetContentRevision();

    // Updates reuse the same mesh object (renderer caches see a new revision, never a new address).
    auto larger = Quad(4.0f);
    larger.vertices.push_back(larger.vertices[0]);
    larger.vertices.back().position = {2.0f, 0.0f, -1.0f};
    larger.indices.insert(larger.indices.end(), {0, 1, 4});
    if (!mesh->SetRuntimeGeometry(larger) || mesh->GetMesh() != runtime)
        return Fail("Runtime geometry was not updated in place.");
    if (runtime->GetContentRevision() == firstRevision || runtime->GetMeshData().vertices.size() != 5 ||
        runtime->GetSubmesh(0).indexCount != 9)
        return Fail("The in-place update did not publish the new geometry.");
    if (runtime->GetBounds().radius <= 1.5f)
        return Fail("Bounds were not recomputed.");
    const auto &tangent = runtime->GetMeshData().vertices[0].tangent;
    if (tangent[0] == 0.0f && tangent[1] == 0.0f && tangent[2] == 0.0f)
        return Fail("Tangents were not generated.");

    // No triangles draws nothing; geometry comes back on the next update.
    if (!mesh->SetRuntimeGeometry({}) || mesh->GetMesh() != nullptr)
        return Fail("Empty runtime geometry should draw nothing.");
    if (!mesh->SetRuntimeGeometry(Quad(1.0f)) || mesh->GetMesh() != runtime)
        return Fail("Runtime geometry did not return after an empty update.");

    mesh->ClearRuntimeGeometry();
    if (mesh->HasRuntimeGeometry() || mesh->GetMesh() != &authored)
        return Fail("Clearing did not restore the authored mesh.");

    // Imported geometry stays immutable.
    PlutoGE::render::Mesh frozen(PlutoGE::render::MeshConfig{.data = Quad(1.0f)});
    frozen.FreezeGeometry();
    if (frozen.ReplaceGeometry(Quad(2.0f)))
        return Fail("Frozen geometry was replaced.");

    std::cout << "Mesh runtime geometry tests passed.\n";
    return 0;
}
