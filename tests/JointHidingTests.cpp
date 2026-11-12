// Joints hidden from cameras: which joints a list hides (JointHiding.h), how their palette matrices shrink, and that
// camera passes drop shadow-only commands while shadows keep them (CameraCommandSelection).
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/CameraCommandSelection.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/RenderSceneFrontend.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/JointHiding.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include <algorithm>
#include <array>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace PlutoGE;

namespace
{
    void Require(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    // hips -> spine -> neck -> head -> jaw, and spine -> arm: joint i sits 1 m above its parent (the arm 1 m to the side).
    render::Skeleton Body()
    {
        render::Skeleton skeleton;
        const char *names[] = {"hips", "spine", "neck", "head", "jaw", "arm"};
        const int parents[] = {-1, 0, 1, 2, 3, 1};
        const glm::vec3 bind[] = {{0, 0, 0}, {0, 1, 0}, {0, 2, 0}, {0, 3, 0}, {0, 3.2f, 0.1f}, {1, 1, 0}};
        for (int i = 0; i < 6; ++i)
            skeleton.joints.push_back({.name = names[i], .parentJointIndex = parents[i],
                                       .inverseBindMatrix = glm::translate(glm::mat4(1.0f), -bind[i])});
        skeleton.humanoidBoneMappings = {{.bone = render::HumanoidBone::Head, .targetJointIndex = 3}};
        return skeleton;
    }

    glm::vec3 Skin(const glm::mat4 &palette, glm::vec3 bindPosition) { return glm::vec3(palette * glm::vec4(bindPosition, 1.0f)); }
}

int main()
{
    try
    {
        const auto skeleton = Body();
        {
            const std::vector<std::string> head{"head"};
            const auto targets = scene::ResolveHiddenJoints(skeleton, head);
            Require(targets == std::vector<int>({-1, -1, -1, 3, 3, -1}), "the head and the jaw below it collapse onto the head");
            Require(scene::HidesAnyJoint(targets), "a resolved name hides joints");
            const std::vector<std::string> alias{"Head"};
            Require(scene::ResolveHiddenJoints(skeleton, alias) == targets, "humanoid aliases resolve");
            const std::vector<std::string> unknown{"tail", ""};
            Require(!scene::HidesAnyJoint(scene::ResolveHiddenJoints(skeleton, unknown)), "unknown names hide nothing");
            const std::vector<std::string> nested{"head", "neck"};
            Require(scene::ResolveHiddenJoints(skeleton, nested) == std::vector<int>({-1, -1, 2, 2, 2, -1}),
                    "a hidden joint under another collapses onto the highest one");
        }
        {
            // Posed: the whole body moved 5 m along x and the head turned; skinning matrices = pose * inverse bind.
            std::vector<glm::mat4> palette;
            const glm::mat4 move = glm::translate(glm::mat4(1.0f), glm::vec3(5, 0, 0));
            for (size_t i = 0; i < skeleton.joints.size(); ++i)
                palette.push_back(move);
            palette[3] = move * glm::translate(glm::mat4(1.0f), glm::vec3(0, 3, 0)) *
                         glm::rotate(glm::mat4(1.0f), 0.5f, glm::vec3(0, 1, 0)) * skeleton.joints[3].inverseBindMatrix;
            const auto original = palette;
            const std::vector<std::string> head{"head"};
            scene::CollapseHiddenJoints(skeleton, scene::ResolveHiddenJoints(skeleton, head), palette);
            const glm::vec3 headCentre(5, 3, 0);
            const auto nearHead = [&](glm::vec3 p) { return glm::length(p - headCentre) < 0.002f; };
            Require(nearHead(Skin(palette[3], {0.3f, 3.25f, 0.2f})), "head vertices shrink onto the head joint");
            Require(nearHead(Skin(palette[4], {0.0f, 3.4f, 0.3f})), "the jaw shrinks onto the head joint too");
            for (int shown : {0, 1, 2, 5})
                Require(palette[shown] == original[shown], "shown joints are untouched");
            // A vertex half on the neck, half on the head: halfway between its neck position and the head joint.
            const glm::vec3 seam(0, 2.8f, 0);
            const glm::vec3 blended = 0.5f * Skin(palette[2], seam) + 0.5f * Skin(palette[3], seam);
            Require(std::abs(blended.y - 0.5f * (2.8f + 3.0f)) < 0.01f, "a neck vertex weighted to the head closes part way");
            const glm::vec3 normal = glm::mat3(palette[3]) * glm::vec3(0, 0, 1);
            Require(glm::length(normal) > 0.0f && std::isfinite(normal.x), "hidden normals stay finite");
            std::vector<glm::mat4> wrongSize(2);
            scene::CollapseHiddenJoints(skeleton, scene::ResolveHiddenJoints(skeleton, head), wrongSize);
            Require(wrongSize[0] == glm::mat4(0.0f), "a palette of another skeleton is left alone");
        }
        {
            std::vector<render::RenderCommand> commands(3);
            render::CameraCommandSelection selection;
            const render::RenderCommandView all(commands);
            const auto plain = selection.Select(all);
            Require(plain.size() == 3 && &plain[0] == &commands[0], "without shadow-only commands the list passes through");
            commands[1].shadowOnly = true;
            const auto camera = selection.Select(all);
            Require(camera.size() == 2 && &camera[0] == &commands[0] && &camera[1] == &commands[2],
                    "cameras drop shadow-only commands, keeping the rest in order");
        }
        {
            // A skinned mesh hiding a joint from cameras submits its full pose shadow-only and a shadowless camera draw
            // whose palette hides the joint; clearing the joints restores the single ordinary draw.
            render::MeshConfig config;
            config.skeleton.joints.resize(3);
            config.animationNodes.resize(3);
            for (int i = 0; i < 3; ++i)
            {
                auto &joint = config.skeleton.joints[i];
                joint.name = std::array{"upper", "lower", "hand"}[i];
                joint.nodeIndex = i;
                joint.parentJointIndex = i - 1;
                joint.localBindTransform = glm::translate(glm::mat4(1), glm::vec3(i ? 1 : 0, 0, 0));
                joint.inverseBindMatrix = glm::translate(glm::mat4(1), glm::vec3(-i, 0, 0));
                config.animationNodes[i] = {.name = joint.name, .parentNodeIndex = i - 1, .localBindTransform = joint.localBindTransform};
            }
            render::Mesh mesh(config);
            render::Material material({.color = {1, 1, 1, 1}});
            scene::Scene scene;
            auto *owner = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Body"}));
            owner->CreateComponent<scene::AnimationComponent>();
            auto *meshEntity = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Mesh"}), owner);
            auto *component = meshEntity->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{.mesh = &mesh});
            component->SetMaterial(&material);
            auto &frontend = core::Engine::GetInstance().GetRenderScene();
            const auto submit = [&] {
                frontend.ClearRenderCommands();
                component->SubmitRenderCommands();
                return frontend.GetSceneRenderCommands();
            };
            const auto plain = submit();
            Require(plain.size() == 1 && !plain[0].shadowOnly && plain[0].castsShadow && plain[0].jointMatrices,
                    "a skinned mesh draws once, casting its shadow");
            component->SetCameraHiddenJoints({"hand"});
            const auto hidden = submit();
            Require(hidden.size() == 2, "hiding a joint adds the camera's draw");
            const auto &shadow = hidden[0].shadowOnly ? hidden[0] : hidden[1];
            const auto &camera = hidden[0].shadowOnly ? hidden[1] : hidden[0];
            Require(shadow.shadowOnly && shadow.castsShadow && !camera.shadowOnly && !camera.castsShadow,
                    "the full pose only casts the shadow; the camera's draw casts none");
            Require(camera.jointMatrices && shadow.jointMatrices && camera.jointMatrices != shadow.jointMatrices &&
                    (*camera.jointMatrices)[0] == (*shadow.jointMatrices)[0] &&
                    (*camera.jointMatrices)[2] != (*shadow.jointMatrices)[2],
                    "the camera's palette hides only the joint");
            const std::vector<std::string> expected{"hand"};
            Require(component->GetCameraHiddenJoints() == expected, "the hidden joints read back");
            const auto saved = component->Serialize();
            Require(std::ranges::any_of(saved, [](const auto &p) { return p.name == "Camera Hidden Joints" && p.value == "hand"; }),
                    "the hidden joints are saved");
            component->SetCameraHiddenJoints({});
            Require(submit().size() == 1, "clearing the joints restores the single draw");
            Require(std::ranges::none_of(component->Serialize(), [](const auto &p) { return p.name == "Camera Hidden Joints"; }),
                    "a mesh hiding nothing saves as before");
            frontend.ClearRenderCommands();
        }
        std::cout << "Joint hiding tests passed\n";
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "Joint hiding tests failed: " << exception.what() << '\n';
        return 1;
    }
}
