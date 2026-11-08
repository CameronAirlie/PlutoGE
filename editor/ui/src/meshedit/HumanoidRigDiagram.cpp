#include "PlutoGE/ui/meshedit/HumanoidRigDiagram.h"

#include "PlutoGE/render/HumanoidRigAnalysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace PlutoGE::ui::meshedit
{
    namespace
    {
        constexpr float kEpsilon = 1e-5f;

        bool IsIdentity(const glm::mat4 &matrix)
        {
            const glm::mat4 identity(1.0f);
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    if (std::abs(matrix[column][row] - identity[column][row]) > kEpsilon)
                        return false;
            return true;
        }

        // Rigs are nearly always built along the mesh axes; snapping keeps a slightly leaning rest pose upright.
        glm::vec3 SnapToAxis(const glm::vec3 &direction)
        {
            const glm::vec3 magnitude = glm::abs(direction);
            const int axis = magnitude.x >= magnitude.y && magnitude.x >= magnitude.z ? 0 : (magnitude.y >= magnitude.z ? 1 : 2);
            if (magnitude[axis] < 0.85f)
                return direction;
            glm::vec3 snapped(0.0f);
            snapped[axis] = direction[axis] > 0.0f ? 1.0f : -1.0f;
            return snapped;
        }

        std::optional<glm::vec3> Normalized(const glm::vec3 &value)
        {
            const float length = glm::length(value);
            if (!(length > kEpsilon) || !std::isfinite(length))
                return std::nullopt;
            return value / length;
        }

        // The humanoid map's joint for `bone`, or the first joint named like it.
        int FindBoneJoint(const render::Skeleton &skeleton, render::HumanoidBone bone)
        {
            if (const int mapped = render::GetHumanoidTargetJoint(skeleton, bone); mapped >= 0)
                return mapped;
            for (std::size_t index = 0; index < skeleton.joints.size(); ++index)
                if (render::GuessHumanoidBone(skeleton.joints[index].name) == bone)
                    return static_cast<int>(index);
            return -1;
        }

        void MarkTriangle(RigSilhouette &silhouette, const std::array<glm::vec2, 3> &cells)
        {
            const auto mark = [&](int column, int row)
            {
                if (column >= 0 && row >= 0 && column < silhouette.columns && row < silhouette.rows)
                    silhouette.covered[static_cast<std::size_t>(row) * static_cast<std::size_t>(silhouette.columns) + static_cast<std::size_t>(column)] = 1;
            };
            // Triangles smaller than a cell would slip between cell centres, so their corners always count.
            for (const auto &corner : cells)
                mark(static_cast<int>(std::floor(corner.x)), static_cast<int>(std::floor(corner.y)));

            const int minColumn = (std::max)(0, static_cast<int>(std::floor((std::min)({cells[0].x, cells[1].x, cells[2].x}))));
            const int maxColumn = (std::min)(silhouette.columns - 1, static_cast<int>(std::floor((std::max)({cells[0].x, cells[1].x, cells[2].x}))));
            const int minRow = (std::max)(0, static_cast<int>(std::floor((std::min)({cells[0].y, cells[1].y, cells[2].y}))));
            const int maxRow = (std::min)(silhouette.rows - 1, static_cast<int>(std::floor((std::max)({cells[0].y, cells[1].y, cells[2].y}))));
            if (maxColumn - minColumn < 1 && maxRow - minRow < 1)
                return;

            const auto edge = [](const glm::vec2 &a, const glm::vec2 &b, const glm::vec2 &p) { return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x); };
            const float area = edge(cells[0], cells[1], cells[2]);
            if (std::abs(area) < kEpsilon)
                return;
            for (int row = minRow; row <= maxRow; ++row)
            {
                for (int column = minColumn; column <= maxColumn; ++column)
                {
                    const glm::vec2 centre(static_cast<float>(column) + 0.5f, static_cast<float>(row) + 0.5f);
                    const float w0 = edge(cells[1], cells[2], centre) * area;
                    const float w1 = edge(cells[2], cells[0], centre) * area;
                    const float w2 = edge(cells[0], cells[1], centre) * area;
                    if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
                        mark(column, row);
                }
            }
        }
    }

    std::vector<glm::vec3> ComputeJointBindPositions(const render::Skeleton &skeleton)
    {
        const auto &joints = skeleton.joints;
        std::vector<glm::vec3> positions(joints.size(), glm::vec3(0.0f));
        // Inverse bind matrices map the mesh's bind space into each joint, so they place joints exactly over the
        // vertices. A skeleton without skinning data falls back to its local bind transforms.
        const bool skinned = std::ranges::any_of(joints, [](const auto &joint) { return !IsIdentity(joint.inverseBindMatrix); });
        if (skinned)
        {
            for (std::size_t index = 0; index < joints.size(); ++index)
                positions[index] = glm::vec3(glm::inverse(joints[index].inverseBindMatrix)[3]);
            return positions;
        }

        std::vector<glm::mat4> globals(joints.size(), glm::mat4(1.0f));
        std::vector<std::uint8_t> resolved(joints.size(), 0);
        for (std::size_t start = 0; start < joints.size(); ++start)
        {
            // Resolve the chain above `start` first; parents may come after children in the array.
            std::vector<int> chain;
            for (int joint = static_cast<int>(start); render::IsValidSkeletonJoint(skeleton, joint) && !resolved[static_cast<std::size_t>(joint)] &&
                                                     chain.size() <= joints.size();
                 joint = joints[static_cast<std::size_t>(joint)].parentJointIndex)
                chain.push_back(joint);
            for (auto it = chain.rbegin(); it != chain.rend(); ++it)
            {
                const auto index = static_cast<std::size_t>(*it);
                const int parent = joints[index].parentJointIndex;
                globals[index] = render::IsValidSkeletonJoint(skeleton, parent) && resolved[static_cast<std::size_t>(parent)]
                                     ? globals[static_cast<std::size_t>(parent)] * joints[index].localBindTransform
                                     : joints[index].localBindTransform;
                resolved[index] = 1;
            }
        }
        for (std::size_t index = 0; index < joints.size(); ++index)
            positions[index] = glm::vec3(globals[index][3]);
        return positions;
    }

    CharacterFrame EstimateCharacterFrame(const render::Skeleton &skeleton, std::span<const glm::vec3> positions)
    {
        using Bone = render::HumanoidBone;
        const auto position = [&](Bone bone) -> std::optional<glm::vec3>
        {
            const int joint = FindBoneJoint(skeleton, bone);
            if (joint < 0 || static_cast<std::size_t>(joint) >= positions.size())
                return std::nullopt;
            return positions[static_cast<std::size_t>(joint)];
        };

        CharacterFrame frame;
        std::optional<glm::vec3> bottom = position(Bone::Hips);
        if (!bottom)
        {
            const auto leftLeg = position(Bone::LeftUpperLeg);
            const auto rightLeg = position(Bone::RightUpperLeg);
            if (leftLeg && rightLeg)
                bottom = (*leftLeg + *rightLeg) * 0.5f;
        }
        auto top = position(Bone::Head);
        if (!top)
            top = position(Bone::Neck);
        if (bottom && top)
            if (const auto up = Normalized(*top - *bottom))
                frame.up = glm::normalize(SnapToAxis(*up));

        std::optional<glm::vec3> left;
        constexpr std::array<std::pair<Bone, Bone>, 5> sides = {{
            {Bone::LeftUpperLeg, Bone::RightUpperLeg},
            {Bone::LeftUpperArm, Bone::RightUpperArm},
            {Bone::LeftShoulder, Bone::RightShoulder},
            {Bone::LeftFoot, Bone::RightFoot},
            {Bone::LeftHand, Bone::RightHand},
        }};
        for (const auto &[leftBone, rightBone] : sides)
        {
            const auto a = position(leftBone);
            const auto b = position(rightBone);
            if (!a || !b)
                continue;
            const glm::vec3 across = *a - *b;
            if ((left = Normalized(across - glm::dot(across, frame.up) * frame.up)))
                break;
        }
        if (!left)
        {
            // Default glTF facing (+Z) has the character's left on +X.
            for (const glm::vec3 candidate : {glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f)})
                if ((left = Normalized(candidate - glm::dot(candidate, frame.up) * frame.up)))
                    break;
        }
        const glm::vec3 snappedLeft = SnapToAxis(*left);
        frame.left = Normalized(snappedLeft - glm::dot(snappedLeft, frame.up) * frame.up).value_or(*left);
        frame.forward = glm::normalize(glm::cross(frame.left, frame.up));
        return frame;
    }

    RigDiagram BuildRigDiagram(const render::MeshConfig &mesh, RigView view, int silhouetteRows)
    {
        RigDiagram diagram;
        diagram.view = view;
        const auto positions = ComputeJointBindPositions(mesh.skeleton);
        diagram.frame = EstimateCharacterFrame(mesh.skeleton, positions);
        const glm::vec3 right = view == RigView::Front ? diagram.frame.left : diagram.frame.forward;
        const glm::vec3 up = diagram.frame.up;
        const glm::vec3 towardViewer = view == RigView::Front ? diagram.frame.forward : -diagram.frame.left;
        const auto project = [&](const glm::vec3 &point) { return glm::vec2(glm::dot(point, right), glm::dot(point, up)); };

        glm::vec2 minimum(std::numeric_limits<float>::max());
        glm::vec2 maximum(std::numeric_limits<float>::lowest());
        const auto include = [&](const glm::vec2 &point)
        {
            minimum = glm::min(minimum, point);
            maximum = glm::max(maximum, point);
        };
        diagram.joints.reserve(positions.size());
        diagram.depth.reserve(positions.size());
        for (const auto &position : positions)
        {
            diagram.joints.push_back(project(position));
            diagram.depth.push_back(glm::dot(position, towardViewer));
            include(diagram.joints.back());
        }

        std::vector<glm::vec2> vertices;
        vertices.reserve(mesh.data.vertices.size());
        for (const auto &vertex : mesh.data.vertices)
        {
            vertices.push_back(project(glm::vec3(vertex.position[0], vertex.position[1], vertex.position[2])));
            include(vertices.back());
        }
        if (minimum.x > maximum.x)
            return diagram; // nothing to show
        diagram.min = minimum;
        diagram.max = maximum;

        const float height = maximum.y - minimum.y;
        if (silhouetteRows <= 0 || vertices.empty() || !(height > kEpsilon))
            return diagram;
        auto &silhouette = diagram.silhouette;
        const float cell = height / static_cast<float>(silhouetteRows);
        silhouette.rows = silhouetteRows;
        silhouette.columns = (std::max)(1, static_cast<int>(std::ceil((maximum.x - minimum.x) / cell)));
        silhouette.covered.assign(static_cast<std::size_t>(silhouette.rows) * static_cast<std::size_t>(silhouette.columns), 0);
        const auto toCell = [&](unsigned int index) { return glm::vec2((vertices[index].x - minimum.x) / cell, (maximum.y - vertices[index].y) / cell); };

        // The finest LOD only: coarser levels cover the same outline.
        const auto &indices = mesh.data.indices;
        const auto rasterize = [&](std::size_t first, std::size_t count)
        {
            const std::size_t end = (std::min)(indices.size(), first + count);
            for (std::size_t index = first; index + 2 < end; index += 3)
            {
                const unsigned int a = indices[index], b = indices[index + 1], c = indices[index + 2];
                if (a >= vertices.size() || b >= vertices.size() || c >= vertices.size())
                    continue;
                MarkTriangle(silhouette, {toCell(a), toCell(b), toCell(c)});
            }
        };
        if (mesh.submeshes.empty())
            rasterize(0, indices.size());
        for (const auto &submesh : mesh.submeshes)
        {
            const auto &range = submesh.lods.empty() ? render::Submesh::LodRange{.indexOffset = submesh.indexOffset, .indexCount = submesh.indexCount}
                                                     : submesh.lods.front();
            rasterize(range.indexOffset, range.indexCount);
        }
        return diagram;
    }

    std::vector<int> PickRigJoints(const RigDiagram &diagram, glm::vec2 point, float radius)
    {
        std::vector<std::pair<float, int>> hits;
        for (std::size_t index = 0; index < diagram.joints.size(); ++index)
        {
            const float distance = glm::distance(diagram.joints[index], point);
            if (distance <= radius)
                hits.emplace_back(distance, static_cast<int>(index));
        }
        std::ranges::sort(hits);
        std::vector<int> joints;
        joints.reserve(hits.size());
        for (const auto &hit : hits)
            joints.push_back(hit.second);
        return joints;
    }
}
