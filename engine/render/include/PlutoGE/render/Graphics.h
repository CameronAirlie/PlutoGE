#pragma once

#include <glm/glm.hpp>

namespace PlutoGE::render
{
    class Mesh;
    class Material;
    struct CameraData;
    class Graphics
    {
    public:
        Graphics() = default;
        ~Graphics() = default;

        static void DrawMeshWithMaterial(Mesh *mesh, Material *material,
                                         const glm::mat4 &modelMatrix = glm::mat4(1.0f),
                                         CameraData *cameraData = nullptr);

    private:
        static void BindMesh(Mesh *mesh);
        static void BindMaterial(Material *material,
                                 const glm::mat4 &modelMatrix = glm::mat4(1.0f),
                                 CameraData *cameraData = nullptr);
        static void DrawMesh(Mesh *mesh);
    };
}