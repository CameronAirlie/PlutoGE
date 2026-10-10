#include "PlutoGE/ui/EntityTransformEditing.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/assets/AssetManager.h"
#include <algorithm>
#include <cmath>

namespace PlutoGE::ui
{
    namespace
    {
        bool Near(float a, float b)
        { return std::abs(a - b) <= .00001f * (std::max)({1.0f, std::abs(a), std::abs(b)}); }
        bool Near(const glm::vec3 &a, const glm::vec3 &b)
        { return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(.00001f))); }
        bool Near(const glm::mat4 &a, const glm::mat4 &b, int columns = 4)
        {
            for (int column = 0; column < columns; ++column)
                for (int row = 0; row < 4; ++row)
                    if (!Near(a[column][row],b[column][row])) return false;
            return true;
        }
    }

    bool PrepareWorldTransformEdit(scene::Entity &entity, const glm::mat4 &world,
        bool translationOnly, PreparedEntityTransformEdit &output, std::string &error)
    {
        PreparedEntityTransformEdit candidate;
        candidate.entity = &entity;
        candidate.local = entity.GetParent() ? glm::inverse(entity.GetParent()->GetWorldTransform()) * world : world;
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                if (!std::isfinite(candidate.local[column][row]))
                { error = "Selection contains an invalid or singular parent transform."; return false; }
        if (candidate.local[0][3] != 0 || candidate.local[1][3] != 0 ||
            candidate.local[2][3] != 0 || candidate.local[3][3] != 1)
        { error = "Selection transform must be affine."; return false; }
        candidate.position = glm::vec3(candidate.local[3]);
        if (translationOnly && Near(candidate.local, entity.GetLocalTransform(), 3))
        {
            output = candidate; error.clear(); return true;
        }
        scene::Transform controls;
        glm::mat4 correction;
        if (!scene::FactorLocalTransform(candidate.local, controls, correction))
        { error = "Selection transform cannot be represented as finite affine controls."; return false; }
        candidate.position = controls.position;
        candidate.rotation = controls.rotation;
        candidate.scale = controls.scale;
        candidate.mode = Near(correction, entity.GetLocalTransformCorrection()) &&
            Near(scene::ComposeLocalTransform(controls, entity.GetLocalTransformCorrection()), candidate.local)
            ? PreparedEntityTransformEdit::Mode::Controls : PreparedEntityTransformEdit::Mode::Affine;
        if (candidate.mode == PreparedEntityTransformEdit::Mode::Affine &&
            core::Engine::GetInstance().GetAssetManager().GetAssetPipelineVersion() < 4)
        { error = "This world transform requires affine scene support (project version 4 or later)."; return false; }
        output = candidate; error.clear(); return true;
    }

    bool ApplyPreparedTransformEdit(const PreparedEntityTransformEdit &edit)
    {
        if (!edit.entity) return false;
        auto &entity = *edit.entity;
        const auto position = entity.GetPosition(), rotation = entity.GetRotation(), scale = entity.GetScale();
        const auto correction = entity.GetLocalTransformCorrection();
        if (edit.mode == PreparedEntityTransformEdit::Mode::Affine)
        {
            if (!entity.SetLocalTransformMatrix(edit.local)) return false;
        }
        else
        {
            if (entity.GetPosition() != edit.position) entity.SetPosition(edit.position);
            if (edit.mode == PreparedEntityTransformEdit::Mode::Controls)
            {
                if (!Near(entity.GetRotation(),edit.rotation)) entity.SetRotation(edit.rotation);
                if (!Near(entity.GetScale(),edit.scale)) entity.SetScale(edit.scale);
            }
        }
        if (entity.GetPosition() != position) entity.AddPrefabOverride("Transform.Position");
        if (entity.GetRotation() != rotation) entity.AddPrefabOverride("Transform.Rotation");
        if (entity.GetScale() != scale) entity.AddPrefabOverride("Transform.Scale");
        if (entity.GetLocalTransformCorrection() != correction) entity.AddPrefabOverride("Transform.LinearCorrection");
        return true;
    }
}
