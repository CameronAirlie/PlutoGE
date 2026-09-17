#include "PlutoGE/import/MeshImporter.h"
#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main()
{
    using namespace PlutoGE;
    const auto path = std::filesystem::temp_directory_path() / "plutoge-transformed-instances.fbx";
    std::ofstream(path) << R"FBX(; FBX 7.4.0 project file
FBXHeaderExtension: { FBXHeaderVersion: 1003
    FBXVersion: 7400
}
GlobalSettings: { Version: 1000
    Properties70: {
        P: "UpAxis", "int", "Integer", "",1
        P: "UpAxisSign", "int", "Integer", "",1
        P: "FrontAxis", "int", "Integer", "",2
        P: "FrontAxisSign", "int", "Integer", "",1
        P: "CoordAxis", "int", "Integer", "",0
        P: "CoordAxisSign", "int", "Integer", "",1
        P: "UnitScaleFactor", "double", "Number", "",100
    }
}
Objects: {
    Geometry: 1, "Geometry::Triangle", "Mesh" {
        Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }
        PolygonVertexIndex: *3 { a: 0,1,-3 }
    }
    Model: 2, "Model::InstanceA", "Mesh" {
        Version: 232
        Properties70: {
            P: "Lcl Translation", "Lcl Translation", "", "A",10,3,0
            P: "Lcl Rotation", "Lcl Rotation", "", "A",40,0,0
            P: "Lcl Scaling", "Lcl Scaling", "", "A",2,2,2
        }
    }
    Model: 3, "Model::InstanceB", "Mesh" {
        Version: 232
        Properties70: {
            P: "Lcl Translation", "Lcl Translation", "", "A",15,3,0
            P: "Lcl Rotation", "Lcl Rotation", "", "A",40,0,0
        }
    }
}
Connections: {
    C: "OO",1,2
    C: "OO",1,3
    C: "OO",2,0
    C: "OO",3,0
}
)FBX";
    Assimp::Importer reader;
    reader.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    const auto *reference =
        reader.ReadFile(path.string(), aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_GlobalScale);
    if (!reference)
        throw std::runtime_error(reader.GetErrorString());
    const auto imported = assetimport::MeshImporter{}.ImportMeshSourceAsset(path.string());
    size_t checked = 0;
    auto visit = [&](auto &&self, const aiNode *node, aiMatrix4x4 parent) -> void {
        const auto world = parent * node->mTransformation;
        for (unsigned slot = 0; slot < node->mNumMeshes; ++slot)
        {
            const auto *source = reference->mMeshes[node->mMeshes[slot]];
            const auto &submesh = imported.submeshes.at(checked++);
            glm::mat4 transform(1);
            int index = submesh.animatedNodeIndex;
            while (index >= 0)
            {
                const auto &n = imported.animationNodes.at(index);
                transform = n.localBindTransform * transform;
                index = n.parentNodeIndex;
            }
            for (unsigned corner = 0; corner < 3; ++corner)
            {
                const auto expected = world * source->mVertices[source->mFaces[0].mIndices[corner]];
                const auto &v =
                    imported.meshData.vertices.at(imported.meshData.indices.at(submesh.indexOffset + corner)).position;
                const auto actual = transform * glm::vec4(v[0], v[1], v[2], 1);
                if (glm::length(glm::vec3(actual) - glm::vec3(expected.x, expected.y, expected.z)) > 0.0001f)
                    throw std::runtime_error("FBX node transform applied incorrectly");
            }
        }
        for (unsigned i = 0; i < node->mNumChildren; ++i)
            self(self, node->mChildren[i], world);
    };
    visit(visit, reference->mRootNode, aiMatrix4x4{});
    if (checked != 2)
        throw std::runtime_error("Missing FBX instances");
    std::filesystem::remove(path);
    std::cout << "FBX transformed instances passed\n";
}
