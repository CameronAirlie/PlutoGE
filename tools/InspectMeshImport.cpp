#include "PlutoGE/import/MeshImporter.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>

int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;
    try
    {
        const auto start = std::chrono::steady_clock::now();
        const bool cook = argc > 2;
        auto asset = PlutoGE::assetimport::MeshImporter{}.ImportMeshSourceAsset(argv[1], {cook, cook, cook});
        glm::vec3 low(std::numeric_limits<float>::max()), high(-std::numeric_limits<float>::max());
        for (const auto &v : asset.meshData.vertices)
        {
            const glm::vec3 p(v.position[0], v.position[1], v.position[2]);
            low = glm::min(low, p);
            high = glm::max(high, p);
        }
        std::cout << "seconds=" << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
                  << " vertices=" << asset.meshData.vertices.size() << " indices=" << asset.meshData.indices.size()
                  << " submeshes=" << asset.submeshes.size() << " materials=" << asset.materials.size()
                  << " textures=" << asset.textures.size() << " animations=" << asset.animations.size() << '\n';
        std::cout << "local_bounds=" << low.x << ',' << low.y << ',' << low.z << " to " << high.x << ',' << high.y << ','
                  << high.z << '\n';
        low = glm::vec3(std::numeric_limits<float>::max());
        high = -low;
        for (const auto &submesh : asset.submeshes)
        {
            glm::mat4 transform(1);
            for (int node = submesh.animatedNodeIndex; node >= 0; node = asset.animationNodes[node].parentNodeIndex)
                transform = asset.animationNodes[node].localBindTransform * transform;
            for (size_t i = submesh.indexOffset; i < size_t(submesh.indexOffset) + submesh.indexCount; ++i)
            {
                const auto &p = asset.meshData.vertices[asset.meshData.indices[i]].position;
                const glm::vec3 world = transform * glm::vec4(p[0], p[1], p[2], 1);
                low = glm::min(low, world); high = glm::max(high, world);
            }
        }
        std::cout << "world_bounds=" << low.x << ',' << low.y << ',' << low.z << " to " << high.x << ',' << high.y << ',' << high.z << '\n';
        for (const auto &t : asset.textures)
            std::cout << "texture exists=" << std::filesystem::exists(t.sourcePath) << " " << t.sourcePath << '\n';
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
