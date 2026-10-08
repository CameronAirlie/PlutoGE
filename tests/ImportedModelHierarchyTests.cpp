#include "PlutoGE/import/ImportedModelHierarchy.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    void Require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
}

int main()
try
{
    using namespace PlutoGE::assetimport;
    std::vector<ImportedModelNode> nodes(2);
    nodes[0].name = "Child";
    nodes[0].parentNodeIndex = 1;
    nodes[0].localTransform = glm::translate(glm::mat4(1), glm::vec3(1, 0, 0));
    nodes[1].name = "Root";
    nodes[1].localTransform = glm::translate(glm::mat4(1), glm::vec3(0, 2, 0));
    ImportedModelHierarchy hierarchy;
    std::string error;
    Require(BuildImportedModelHierarchy(nodes, hierarchy, &error), "Forward parent resolution failed");
    Require(hierarchy.sceneRoots == std::vector<int>{1} && glm::length(glm::vec3(hierarchy.nodes[0].worldTransform[3]) - glm::vec3(1, 2, 0)) < 0.0001f,
            "Hierarchy world transform or roots are incorrect");
    for (unsigned invalid = 0; invalid < 4; ++invalid)
    {
        auto bad = nodes;
        if (invalid == 0) bad[1].parentNodeIndex = 0;
        if (invalid == 1) bad[0].parentNodeIndex = 99;
        if (invalid == 2) bad[0].parentNodeIndex = -2;
        if (invalid == 3) bad[0].localTransform[0][0] = std::numeric_limits<float>::infinity();
        Require(!BuildImportedModelHierarchy(std::move(bad), hierarchy, &error) && hierarchy.nodes[0].name == "Child" && hierarchy.sceneRoots == std::vector<int>{1},
                "Invalid hierarchy was accepted or changed the previous result");
    }
    std::vector<ImportedModelNode> deep(10000);
    for (int index = 0; index < static_cast<int>(deep.size()); ++index)
    {
        deep[index].parentNodeIndex = index + 1 < static_cast<int>(deep.size()) ? index + 1 : -1;
        deep[index].localTransform = glm::translate(glm::mat4(1), glm::vec3(1, 0, 0));
    }
    Require(BuildImportedModelHierarchy(std::move(deep), hierarchy, &error) && hierarchy.nodes[0].worldTransform[3].x == 10000,
            "Deep hierarchy required recursive traversal or computed an incorrect transform");
    std::cout << "Imported model hierarchy tests passed\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
