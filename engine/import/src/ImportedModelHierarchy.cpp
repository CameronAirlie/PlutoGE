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
            ImportedModelHierarchy candidate{.nodes=std::move(nodes)};
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
}
