#include "PlutoGE/import/ImportedModelHierarchy.h"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace PlutoGE::assetimport
{
    namespace
    {
        bool Finite(const glm::mat4 &matrix)
        {
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    if (!std::isfinite(matrix[column][row])) return false;
            return true;
        }
    }

    bool BuildImportedModelHierarchy(std::vector<ImportedModelNode> nodes, ImportedModelHierarchy &hierarchy,
                                     std::string *errorMessage)
    {
        try
        {
            if (nodes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                throw std::runtime_error("Model hierarchy exceeds the node index limit.");
            for (const auto &node : nodes)
            {
                if (node.parentNodeIndex < -1 || node.parentNodeIndex >= static_cast<int>(nodes.size()))
                    throw std::runtime_error("Model hierarchy contains an invalid parent index.");
                if (!Finite(node.localTransform)) throw std::runtime_error("Model hierarchy contains a non-finite local transform.");
            }
            std::vector<unsigned char> state(nodes.size(), 0);
            std::vector<int> chain;
            for (int start = 0; start < static_cast<int>(nodes.size()); ++start)
            {
                if (state[start] == 2) continue;
                chain.clear();
                int current = start;
                while (current >= 0 && state[current] != 2)
                {
                    if (state[current] == 1) throw std::runtime_error("Model hierarchy contains a parent cycle.");
                    state[current] = 1;
                    chain.push_back(current);
                    current = nodes[current].parentNodeIndex;
                }
                while (!chain.empty())
                {
                    const int index = chain.back();
                    chain.pop_back();
                    auto &node = nodes[index];
                    node.worldTransform = node.parentNodeIndex < 0 ? node.localTransform :
                        nodes[node.parentNodeIndex].worldTransform * node.localTransform;
                    if (!Finite(node.worldTransform)) throw std::runtime_error("Model hierarchy world transform is non-finite.");
                    state[index] = 2;
                }
            }
            ImportedModelHierarchy candidate;
            candidate.nodes = std::move(nodes);
            for (int index = 0; index < static_cast<int>(candidate.nodes.size()); ++index)
                if (candidate.nodes[index].parentNodeIndex < 0) candidate.sceneRoots.push_back(index);
            hierarchy = std::move(candidate);
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &error)
        {
            if (errorMessage) *errorMessage = error.what();
            return false;
        }
    }

    bool PrepareStaticModelBindingTransforms(const ImportedModelHierarchy &hierarchy,
        std::vector<StaticModelBindingTransform> &bindings, std::string *errorMessage)
    {
        try
        {
            ImportedModelHierarchy validated;
            if (!BuildImportedModelHierarchy(hierarchy.nodes, validated, errorMessage)) return false;
            std::vector<std::vector<int>> children(validated.nodes.size());
            for (int index = 0; index < static_cast<int>(validated.nodes.size()); ++index)
            {
                const auto &node = validated.nodes[index];
                const auto &matrix = node.localTransform;
                if (matrix[0][3] != 0 || matrix[1][3] != 0 || matrix[2][3] != 0 || matrix[3][3] != 1)
                    throw std::runtime_error("Static model hierarchy contains a non-affine local transform.");
                if (node.parentNodeIndex >= 0) children[node.parentNodeIndex].push_back(index);
            }
            std::vector<unsigned char> selected(validated.nodes.size());
            std::vector<int> pending;
            for (const int root : hierarchy.sceneRoots)
            {
                if (root < 0 || root >= static_cast<int>(selected.size()))
                    throw std::runtime_error("Static model hierarchy contains an invalid selected root.");
                pending.push_back(root);
                while (!pending.empty())
                {
                    const int index = pending.back();
                    pending.pop_back();
                    if (selected[index]) throw std::runtime_error("Static model hierarchy contains overlapping selected roots.");
                    selected[index] = 1;
                    pending.insert(pending.end(), children[index].begin(), children[index].end());
                }
            }
            std::vector<StaticModelBindingTransform> candidate;
            candidate.reserve(hierarchy.bindings.size());
            for (const auto &binding : hierarchy.bindings)
            {
                if (binding.nodeIndex < 0 || binding.nodeIndex >= static_cast<int>(selected.size()))
                    throw std::runtime_error("Static model hierarchy contains an invalid binding node.");
                if (!selected[binding.nodeIndex]) continue;
                if (binding.skinned || binding.transformNodeIndex != -1)
                    throw std::runtime_error("Selected model binding requires animation or skinning support.");
                const auto &baked = binding.bakedTransform;
                if (!Finite(baked) || baked[0][3] != 0 || baked[1][3] != 0 || baked[2][3] != 0 || baked[3][3] != 1)
                    throw std::runtime_error("Static model binding contains an invalid affine baked transform.");
                // Double precision avoids rejecting uniformly small but invertible
                // transforms using an arbitrary absolute determinant threshold.
                const glm::dmat4 precise(baked);
                if (glm::determinant(precise) == 0)
                    throw std::runtime_error("Static model binding has a singular baked transform.");
                const glm::mat4 inverse(glm::inverse(precise));
                if (!Finite(inverse)) throw std::runtime_error("Static model binding inverse is not representable.");
                // Validate the actual float correction consumed by scene rendering.
                const auto left = baked * inverse;
                const auto right = inverse * baked;
                for (int column = 0; column < 4; ++column)
                    for (int row = 0; row < 4; ++row)
                    {
                        const float expected = column == row ? 1.0f : 0.0f;
                        if (!std::isfinite(left[column][row]) || !std::isfinite(right[column][row]) ||
                            std::abs(left[column][row] - expected) > 0.001f ||
                            std::abs(right[column][row] - expected) > 0.001f)
                            throw std::runtime_error("Static model binding inverse is numerically unstable.");
                    }
                candidate.push_back({binding.nodeIndex, binding.submeshIndex,
                    validated.nodes[binding.nodeIndex].worldTransform, inverse});
            }
            bindings = std::move(candidate);
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &error)
        {
            if (errorMessage) *errorMessage = error.what();
            return false;
        }
    }
}
