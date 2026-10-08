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
    // Source vertices are already in baked space. Node placement must recover
    // local geometry once, including shear and mirrored nonuniform scale.
    ImportedModelHierarchy staticHierarchy;
    glm::mat4 baked = glm::translate(glm::mat4(1), glm::vec3(3, -2, 5));
    baked = glm::scale(baked, glm::vec3(-2, 3, 0.5f));
    baked[1][0] = 0.75f;
    staticHierarchy.nodes = {{"Root", -1, baked}, {"Unused", -1}};
    staticHierarchy.sceneRoots = {0};
    staticHierarchy.bindings = {{0, 7, baked}, {1, 8, glm::mat4(1), 0, true}};
    // Deliberately stale cached data must not affect preparation.
    staticHierarchy.nodes[0].worldTransform = glm::mat4(0);
    std::vector<StaticModelBindingTransform> prepared;
    Require(PrepareStaticModelBindingTransforms(staticHierarchy, prepared, &error) && error.empty() &&
        prepared.size() == 1 && prepared[0].nodeIndex == 0 && prepared[0].submeshIndex == 7 &&
        prepared[0].nodeWorldTransform == baked,
        "Static binding preparation did not respect selected roots");
    const glm::vec4 vertex(1, 2, -3, 1);
    const auto stored = baked * vertex;
    Require(glm::length(baked * prepared[0].geometryToNode * stored - stored) < 0.0001f,
        "Static hierarchy applies the baked transform twice");
    const auto edited = glm::translate(glm::mat4(1), glm::vec3(10, 0, 0)) * baked;
    Require(glm::length(edited * prepared[0].geometryToNode * stored - edited * vertex) < 0.0001f,
        "Static binding correction does not preserve node edits");
    const auto previous = prepared[0].geometryToNode;
    for (unsigned invalid = 0; invalid < 10; ++invalid)
    {
        auto bad = staticHierarchy;
        if (invalid == 0) bad.bindings[0].bakedTransform[0] = glm::vec4(0);
        if (invalid == 1) bad.bindings[0].bakedTransform[0][0] = std::numeric_limits<float>::infinity();
        if (invalid == 2) bad.bindings[0].bakedTransform[0][3] = 1;
        if (invalid == 3) bad.bindings[0].skinned = true;
        if (invalid == 4) bad.bindings[0].transformNodeIndex = 0;
        if (invalid == 5) bad.bindings[0].nodeIndex = 99;
        if (invalid == 6) bad.sceneRoots = {0, 0};
        if (invalid == 7) bad.sceneRoots = {-1};
        if (invalid == 8) bad.nodes[0].localTransform[3][3] = 2;
        if (invalid == 9) { bad.nodes[1].parentNodeIndex = 0; bad.sceneRoots = {0, 1}; }
        Require(!PrepareStaticModelBindingTransforms(bad, prepared, &error) && !error.empty() &&
            prepared.size() == 1 && prepared[0].geometryToNode == previous,
            "Invalid static binding partially replaced the previous result");
    }
    auto nested = staticHierarchy;
    nested.nodes[1].parentNodeIndex = 0;
    nested.nodes[1].localTransform = glm::translate(glm::mat4(1), glm::vec3(1, 2, 3));
    const auto childWorld = baked * nested.nodes[1].localTransform;
    nested.bindings = {{1, 3, childWorld}, {1, 4, childWorld}};
    Require(PrepareStaticModelBindingTransforms(nested, prepared, &error) && prepared.size() == 2 &&
        prepared[0].nodeWorldTransform == childWorld && prepared[1].submeshIndex == 4 &&
        glm::length(prepared[0].nodeWorldTransform * prepared[0].geometryToNode * (childWorld * vertex) - childWorld * vertex) < 0.0001f,
        "Nested or repeated static bindings lost exact source transforms");
    staticHierarchy.bindings.resize(1);
    staticHierarchy.bindings[0].bakedTransform = glm::scale(glm::mat4(1), glm::vec3(0.00001f));
    Require(PrepareStaticModelBindingTransforms(staticHierarchy, prepared, &error),
        "Uniform small invertible geometry was rejected by a determinant threshold");
    staticHierarchy.sceneRoots.clear();
    Require(PrepareStaticModelBindingTransforms(staticHierarchy, prepared, &error) && prepared.empty(),
        "Empty source scene unexpectedly selected every node");

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
