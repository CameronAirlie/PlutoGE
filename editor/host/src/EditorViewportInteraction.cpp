#include "EditorViewportInteraction.h"

#include "EditorSession.h"
#include "PlutoGE/platform/InputState.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"

#include <glad/glad.h>
#ifdef _WIN32
#ifdef APIENTRY
#undef APIENTRY
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#undef GLM_ENABLE_EXPERIMENTAL

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

namespace
{
    constexpr float kHandlePixels = 78.0f;
    constexpr float kRingPixels = 64.0f;
    constexpr float kHitPixels = 9.0f;
    constexpr int kRingSegments = 128;
    constexpr std::array<glm::vec3, 3> kAxisColors{
        glm::vec3(0.95f, 0.23f, 0.20f),
        glm::vec3(0.30f, 0.86f, 0.36f),
        glm::vec3(0.25f, 0.50f, 1.0f),
    };

    struct ProjectedPoint
    {
        glm::vec2 position{0.0f};
        bool visible = false;
    };

    struct HandleHit
    {
        int axis = -1;
        float distance = std::numeric_limits<float>::max();
        glm::vec2 direction{1.0f, 0.0f};
    };

    struct LineVertex
    {
        glm::vec3 position{0.0f};
        glm::vec3 color{1.0f};
    };

    void CollectEntities(PlutoGE::scene::Entity *entity, std::vector<PlutoGE::scene::Entity *> &entities)
    {
        if (!entity) return;
        entities.push_back(entity);
        for (auto *child : entity->GetChildren()) CollectEntities(child, entities);
    }

    glm::vec3 CameraPosition(const PlutoGE::render::CameraData &camera)
    {
        return glm::vec3(glm::inverse(camera.view)[3]);
    }

    float WorldUnitsPerPixel(const PlutoGE::render::CameraData &camera, const glm::vec3 &position, int viewportHeight)
    {
        const float distance = std::max(glm::length(position - CameraPosition(camera)), 0.05f);
        const float projectionY = std::max(std::abs(camera.projection[1][1]), 0.0001f);
        return (2.0f * distance) / (projectionY * static_cast<float>(std::max(viewportHeight, 1)));
    }

    ProjectedPoint Project(const glm::vec3 &point, const PlutoGE::render::CameraData &camera, int width, int height)
    {
        const glm::vec4 clip = camera.projection * camera.view * glm::vec4(point, 1.0f);
        if (clip.w <= 0.00001f) return {};
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        return {
            .position = {(ndc.x * 0.5f + 0.5f) * static_cast<float>(width),
                         (0.5f - ndc.y * 0.5f) * static_cast<float>(height)},
            .visible = ndc.x >= -1.2f && ndc.x <= 1.2f && ndc.y >= -1.2f && ndc.y <= 1.2f,
        };
    }

    std::array<glm::vec3, 3> GizmoAxes(const EditorSession &session, const PlutoGE::scene::Entity &entity)
    {
        if (session.GetGizmoSpace() == EditorSession::GizmoSpace::World)
        {
            return {glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)};
        }
        const glm::mat4 world = entity.GetWorldTransform();
        return {
            glm::normalize(glm::vec3(world[0])),
            glm::normalize(glm::vec3(world[1])),
            glm::normalize(glm::vec3(world[2])),
        };
    }

    glm::mat4 RotationMatrix(const glm::vec3 &rotationDegrees)
    {
        const glm::vec3 rotation = glm::radians(rotationDegrees);
        return glm::eulerAngleXYZ(rotation.x, rotation.y, rotation.z);
    }

    glm::mat4 ExtractRotationMatrix(const glm::mat4 &transform)
    {
        glm::vec3 basisX(transform[0]);
        glm::vec3 basisY(transform[1]);
        glm::vec3 basisZ(transform[2]);
        if (glm::dot(basisX, basisX) <= 0.0000001f ||
            glm::dot(basisY, basisY) <= 0.0000001f ||
            glm::dot(basisZ, basisZ) <= 0.0000001f)
        {
            return glm::mat4(1.0f);
        }
        basisX = glm::normalize(basisX);
        basisY = glm::normalize(basisY);
        basisZ = glm::normalize(basisZ);
        if (glm::dot(glm::cross(basisX, basisY), basisZ) < 0.0f) basisX = -basisX;

        glm::mat4 rotation(1.0f);
        rotation[0] = glm::vec4(basisX, 0.0f);
        rotation[1] = glm::vec4(basisY, 0.0f);
        rotation[2] = glm::vec4(basisZ, 0.0f);
        return rotation;
    }

    glm::vec3 ExtractEulerDegrees(const glm::mat4 &rotation)
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        glm::extractEulerAngleXYZ(rotation, x, y, z);
        return glm::degrees(glm::vec3(x, y, z));
    }

    float PointSegmentDistance(const glm::vec2 &point, const glm::vec2 &start, const glm::vec2 &end, float *segmentT = nullptr)
    {
        const glm::vec2 edge = end - start;
        const float lengthSquared = glm::dot(edge, edge);
        const float t = lengthSquared > 0.0001f ? std::clamp(glm::dot(point - start, edge) / lengthSquared, 0.0f, 1.0f) : 0.0f;
        if (segmentT) *segmentT = t;
        return glm::length(point - (start + edge * t));
    }

    HandleHit HitTestGizmo(const EditorSession &session,
                           const PlutoGE::scene::Entity &entity,
                           const PlutoGE::render::CameraData &camera,
                           int width,
                           int height,
                           const glm::vec2 &mouse)
    {
        HandleHit hit;
        const glm::vec3 origin = entity.GetWorldPosition();
        const auto projectedOrigin = Project(origin, camera, width, height);
        if (!projectedOrigin.visible) return hit;
        const auto axes = GizmoAxes(session, entity);
        const float unitsPerPixel = WorldUnitsPerPixel(camera, origin, height);

        if (session.GetGizmoOperation() != EditorSession::GizmoOperation::Rotate)
        {
            const float length = unitsPerPixel * kHandlePixels;
            for (int axis = 0; axis < 3; ++axis)
            {
                const auto endpoint = Project(origin + axes[axis] * length, camera, width, height);
                if (!endpoint.visible) continue;
                float segmentT = 0.0f;
                const float distance = PointSegmentDistance(mouse, projectedOrigin.position, endpoint.position, &segmentT);
                if (segmentT > 0.12f && distance < kHitPixels && distance < hit.distance)
                {
                    const glm::vec2 screenAxis = endpoint.position - projectedOrigin.position;
                    hit = {axis, distance, glm::length(screenAxis) > 0.001f ? glm::normalize(screenAxis) : glm::vec2(1.0f, 0.0f)};
                }
            }
            return hit;
        }

        const float radius = unitsPerPixel * kRingPixels;
        for (int axis = 0; axis < 3; ++axis)
        {
            const glm::vec3 u = axes[(axis + 1) % 3];
            const glm::vec3 v = axes[(axis + 2) % 3];
            ProjectedPoint previous = Project(origin + u * radius, camera, width, height);
            for (int segment = 1; segment <= kRingSegments; ++segment)
            {
                const float angle = glm::two_pi<float>() * static_cast<float>(segment) / static_cast<float>(kRingSegments);
                const ProjectedPoint current = Project(origin + (u * std::cos(angle) + v * std::sin(angle)) * radius, camera, width, height);
                if (previous.visible && current.visible)
                {
                    const float distance = PointSegmentDistance(mouse, previous.position, current.position);
                    if (distance < kHitPixels && distance < hit.distance)
                    {
                        const glm::vec2 tangent = current.position - previous.position;
                        hit = {axis, distance, glm::length(tangent) > 0.001f ? glm::normalize(tangent) : glm::vec2(1.0f, 0.0f)};
                    }
                }
                previous = current;
            }
        }
        return hit;
    }

    std::optional<float> RaySphere(const glm::vec3 &origin, const glm::vec3 &direction, const glm::vec3 &center, float radius)
    {
        const glm::vec3 offset = origin - center;
        const float projected = glm::dot(offset, direction);
        const float discriminant = projected * projected - (glm::dot(offset, offset) - radius * radius);
        if (discriminant < 0.0f) return std::nullopt;
        const float root = std::sqrt(discriminant);
        const float nearDistance = -projected - root;
        const float farDistance = -projected + root;
        if (nearDistance > 0.0f) return nearDistance;
        return farDistance > 0.0f ? std::optional<float>(farDistance) : std::nullopt;
    }

    std::optional<float> RayTriangle(const glm::vec3 &origin,
                                     const glm::vec3 &direction,
                                     const glm::vec3 &a,
                                     const glm::vec3 &b,
                                     const glm::vec3 &c)
    {
        const glm::vec3 edge1 = b - a;
        const glm::vec3 edge2 = c - a;
        const glm::vec3 p = glm::cross(direction, edge2);
        const float determinant = glm::dot(edge1, p);
        if (std::abs(determinant) < 0.00000001f) return std::nullopt;
        const float inverseDeterminant = 1.0f / determinant;
        const glm::vec3 offset = origin - a;
        const float u = glm::dot(offset, p) * inverseDeterminant;
        if (u < 0.0f || u > 1.0f) return std::nullopt;
        const glm::vec3 q = glm::cross(offset, edge1);
        const float v = glm::dot(direction, q) * inverseDeterminant;
        if (v < 0.0f || u + v > 1.0f) return std::nullopt;
        const float distance = glm::dot(edge2, q) * inverseDeterminant;
        return distance > 0.000001f ? std::optional<float>(distance) : std::nullopt;
    }

    std::pair<glm::vec3, glm::vec3> BuildPickRay(const PlutoGE::render::CameraData &camera, const glm::vec2 &mouse, int width, int height)
    {
        const float clipX = (mouse.x / static_cast<float>(std::max(width, 1))) * 2.0f - 1.0f;
        const float clipY = 1.0f - (mouse.y / static_cast<float>(std::max(height, 1))) * 2.0f;
        const glm::mat4 inverseViewProjection = glm::inverse(camera.projection * camera.view);
        glm::vec4 farPoint = inverseViewProjection * glm::vec4(clipX, clipY, -1.0f, 1.0f);
        farPoint /= farPoint.w;
        const glm::vec3 origin = CameraPosition(camera);
        return {origin, glm::normalize(glm::vec3(farPoint) - origin)};
    }

    PlutoGE::scene::Entity *PickEntity(PlutoGE::scene::Scene *scene,
                                       const PlutoGE::render::CameraData &camera,
                                       const glm::vec2 &mouse,
                                       int width,
                                       int height)
    {
        if (!scene) return nullptr;
        const auto [rayOrigin, rayDirection] = BuildPickRay(camera, mouse, width, height);
        std::vector<PlutoGE::scene::Entity *> entities;
        for (auto *root : scene->GetRootEntities()) CollectEntities(root, entities);
        PlutoGE::scene::Entity *picked = nullptr;
        float nearest = std::numeric_limits<float>::max();
        for (auto *entity : entities)
        {
            if (!entity || !entity->IsActive()) continue;
            bool hasGeometry = false;
            if (auto *terrain = entity->GetComponent<PlutoGE::scene::TerrainComponent>(); terrain && terrain->IsEnabled())
            {
                hasGeometry = true;
                glm::vec3 hitPoint(0.0f);
                if (terrain->Raycast(rayOrigin, rayDirection, hitPoint))
                {
                    const float distance = glm::length(hitPoint - rayOrigin);
                    if (distance < nearest) { nearest = distance; picked = entity; }
                }
            }
            if (auto *meshComponent = entity->GetComponent<PlutoGE::scene::MeshComponent>(); meshComponent && meshComponent->IsEnabled() && meshComponent->IsVisible() && meshComponent->GetMesh())
            {
                hasGeometry = true;
                const auto &bounds = meshComponent->GetMesh()->GetBounds();
                const glm::mat4 transform = entity->GetWorldTransform() * meshComponent->GetMeshOffsetTransform();
                const glm::vec3 center = glm::vec3(transform * glm::vec4(bounds.center, 1.0f));
                const float scale = std::max({glm::length(glm::vec3(transform[0])), glm::length(glm::vec3(transform[1])), glm::length(glm::vec3(transform[2]))});
                const auto boundsDistance = RaySphere(rayOrigin, rayDirection, center, std::max(bounds.radius * scale, 0.05f));
                if (!boundsDistance || *boundsDistance >= nearest) continue;
                float meshDistance = *boundsDistance;
                const auto &meshData = meshComponent->GetMesh()->GetMeshData();
                constexpr std::size_t maxExactTriangleCount = 250000;
                const bool exactPick = !meshComponent->GetMesh()->HasSkeleton() &&
                                       meshData.indices.size() >= 3 &&
                                       meshData.indices.size() / 3 <= maxExactTriangleCount;
                if (exactPick)
                {
                    const glm::mat4 inverseTransform = glm::inverse(transform);
                    const glm::vec3 localOrigin = glm::vec3(inverseTransform * glm::vec4(rayOrigin, 1.0f));
                    glm::vec3 localDirection = glm::vec3(inverseTransform * glm::vec4(rayDirection, 0.0f));
                    if (glm::dot(localDirection, localDirection) <= 0.0000001f) continue;
                    localDirection = glm::normalize(localDirection);
                    bool triangleHit = false;
                    float closestTriangle = std::numeric_limits<float>::max();
                    for (std::size_t index = 0; index + 2 < meshData.indices.size(); index += 3)
                    {
                        const unsigned int ia = meshData.indices[index];
                        const unsigned int ib = meshData.indices[index + 1];
                        const unsigned int ic = meshData.indices[index + 2];
                        if (ia >= meshData.vertices.size() || ib >= meshData.vertices.size() || ic >= meshData.vertices.size()) continue;
                        const auto position = [&](unsigned int vertexIndex)
                        {
                            const auto &value = meshData.vertices[vertexIndex].position;
                            return glm::vec3(value[0], value[1], value[2]);
                        };
                        if (const auto distance = RayTriangle(localOrigin, localDirection, position(ia), position(ib), position(ic)); distance && *distance < closestTriangle)
                        {
                            closestTriangle = *distance;
                            triangleHit = true;
                        }
                    }
                    if (!triangleHit) continue;
                    const glm::vec3 worldHit = glm::vec3(transform * glm::vec4(localOrigin + localDirection * closestTriangle, 1.0f));
                    meshDistance = glm::length(worldHit - rayOrigin);
                }
                if (meshDistance < nearest) { nearest = meshDistance; picked = entity; }
            }
            if (!hasGeometry)
            {
                const float markerRadius = WorldUnitsPerPixel(camera, entity->GetWorldPosition(), height) * 11.0f;
                if (const auto distance = RaySphere(rayOrigin, rayDirection, entity->GetWorldPosition(), markerRadius); distance && *distance < nearest)
                {
                    nearest = *distance;
                    picked = entity;
                }
            }
        }
        return picked;
    }

    unsigned int CompileShader(unsigned int type, const char *source)
    {
        const unsigned int shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        int compiled = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (compiled == GL_TRUE) return shader;
        glDeleteShader(shader);
        return 0;
    }

    void *LoadEditorOpenGlProcedure(const char *name)
    {
#ifdef _WIN32
        static HMODULE openGlModule = LoadLibraryA("opengl32.dll");
        PROC procedure = wglGetProcAddress(name);
        if (procedure == nullptr || procedure == reinterpret_cast<PROC>(1) ||
            procedure == reinterpret_cast<PROC>(2) || procedure == reinterpret_cast<PROC>(3) ||
            procedure == reinterpret_cast<PROC>(-1))
        {
            procedure = openGlModule ? GetProcAddress(openGlModule, name) : nullptr;
        }
        return reinterpret_cast<void *>(procedure);
#else
        (void)name;
        return nullptr;
#endif
    }

    void AddLine(std::vector<LineVertex> &vertices, const glm::vec3 &start, const glm::vec3 &end, const glm::vec3 &color)
    {
        vertices.push_back({start, color});
        vertices.push_back({end, color});
    }
}

bool EditorViewportInteraction::Update(EditorSession &session,
                                       const PlutoGE::render::CameraData &camera,
                                       const PlutoGE::platform::InputState &input,
                                       int viewportWidth,
                                       int viewportHeight,
                                       bool runtimeRunning)
{
    if (viewportWidth <= 0 || viewportHeight <= 0) return false;
    bool snapshotChanged = false;
    if (!runtimeRunning && !input.IsMouseButtonDown(1))
    {
        if (input.keys['W'] && !input.previousKeys['W']) { session.SetGizmoOperation(EditorSession::GizmoOperation::Translate); snapshotChanged = true; }
        if (input.keys['E'] && !input.previousKeys['E']) { session.SetGizmoOperation(EditorSession::GizmoOperation::Rotate); snapshotChanged = true; }
        if (input.keys['R'] && !input.previousKeys['R']) { session.SetGizmoOperation(EditorSession::GizmoOperation::Scale); snapshotChanged = true; }
    }

    const glm::vec2 mouse(static_cast<float>(input.mouseState.x), static_cast<float>(input.mouseState.y));
    auto *selected = session.GetSelectedEntity();
    m_hoveredAxis = selected && !runtimeRunning ? HitTestGizmo(session, *selected, camera, viewportWidth, viewportHeight, mouse).axis : -1;

    if (runtimeRunning)
    {
        if (m_drag.active) { m_drag = {}; snapshotChanged = session.EndGizmoEdit() || snapshotChanged; }
        return snapshotChanged;
    }

    if (input.IsMouseButtonPressed(0))
    {
        const HandleHit handle = selected ? HitTestGizmo(session, *selected, camera, viewportWidth, viewportHeight, mouse) : HandleHit{};
        if (selected && handle.axis >= 0 && session.BeginGizmoEdit())
        {
            const auto axes = GizmoAxes(session, *selected);
            m_drag = {
                .entityId = selected->GetID(),
                .axis = handle.axis,
                .startMouse = mouse,
                .screenDirection = handle.direction,
                .axisWorld = axes[handle.axis],
                .position = selected->GetPosition(),
                .localRotation = RotationMatrix(selected->GetRotation()),
                .worldRotation = ExtractRotationMatrix(selected->GetWorldTransform()),
                .scale = selected->GetScale(),
                .worldUnitsPerPixel = WorldUnitsPerPixel(camera, selected->GetWorldPosition(), viewportHeight),
                .active = true,
            };
        }
        else
        {
            session.SetSelectedEntity(PickEntity(session.GetScene(), camera, mouse, viewportWidth, viewportHeight));
            snapshotChanged = true;
        }
    }

    if (m_drag.active)
    {
        auto *entity = session.GetScene() ? session.GetScene()->FindEntityByID(m_drag.entityId) : nullptr;
        if (!entity)
        {
            m_drag = {};
            session.EndGizmoEdit();
            return true;
        }
        if (input.IsMouseButtonDown(0))
        {
            const float pixels = glm::dot(mouse - m_drag.startMouse, m_drag.screenDirection);
            const bool snap = input.keys[341] || input.keys[345];
            if (session.GetGizmoOperation() == EditorSession::GizmoOperation::Translate)
            {
                float distance = pixels * m_drag.worldUnitsPerPixel;
                if (snap) distance = std::round(distance);
                const glm::vec3 worldDelta = m_drag.axisWorld * distance;
                glm::vec3 localDelta = worldDelta;
                if (auto *parent = entity->GetParent()) localDelta = glm::vec3(glm::inverse(parent->GetWorldTransform()) * glm::vec4(worldDelta, 0.0f));
                entity->SetPosition(m_drag.position + localDelta);
            }
            else if (session.GetGizmoOperation() == EditorSession::GizmoOperation::Scale)
            {
                glm::vec3 scale = m_drag.scale;
                float delta = pixels * 0.01f;
                if (snap) delta = std::round(delta * 10.0f) * 0.1f;
                scale[m_drag.axis] = std::max(0.001f, scale[m_drag.axis] + delta);
                entity->SetScale(scale);
            }
            else
            {
                float angle = pixels * 0.5f;
                if (snap) angle = std::round(angle / 15.0f) * 15.0f;
                glm::vec3 axis(0.0f);
                axis[m_drag.axis] = 1.0f;
                const glm::mat4 delta = glm::rotate(glm::mat4(1.0f), glm::radians(angle), axis);
                glm::mat4 localRotation(1.0f);
                if (session.GetGizmoSpace() == EditorSession::GizmoSpace::Local)
                {
                    // Local rotations are post-multiplied, so the delta is
                    // applied around the handle axis shown at drag start.
                    localRotation = m_drag.localRotation * delta;
                }
                else
                {
                    // World rotations are pre-multiplied. Convert the desired
                    // world orientation back into the entity's parent space.
                    const glm::mat4 worldRotation = delta * m_drag.worldRotation;
                    localRotation = entity->GetParent()
                        ? glm::inverse(ExtractRotationMatrix(entity->GetParent()->GetWorldTransform())) * worldRotation
                        : worldRotation;
                }
                entity->SetRotation(ExtractEulerDegrees(localRotation));
            }
        }
        if (!input.IsMouseButtonDown(0))
        {
            m_drag = {};
            snapshotChanged = session.EndGizmoEdit() || snapshotChanged;
        }
    }
    return snapshotChanged;
}

bool EditorViewportInteraction::EnsureRenderer()
{
    if (m_program) return true;
    // PlutoGE.dll and this executable each link the static GLAD target in
    // shared-engine builds. The engine initializes its own dispatch table;
    // initialize the host's distinct table from the already-current WGL
    // context before the overlay makes its first OpenGL call.
    if (!glad_glCreateShader && !gladLoadGLLoader(&LoadEditorOpenGlProcedure)) return false;
    if (!glad_glCreateShader || !glad_glGenVertexArrays || !glad_glBindFramebuffer) return false;
    static constexpr const char *vertexSource = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;
uniform mat4 uViewProjection;
out vec3 vColor;
void main() { vColor = aColor; gl_Position = uViewProjection * vec4(aPosition, 1.0); }
)GLSL";
    static constexpr const char *geometrySource = R"GLSL(#version 330 core
layout(lines) in;
layout(triangle_strip, max_vertices = 4) out;
in vec3 vColor[];
out vec3 gColor;
noperspective out float gAcross;
uniform vec2 uViewportSize;
uniform float uLineWidth;
void EmitLineVertex(int endpoint, vec2 offset, float across)
{
    vec4 position = gl_in[endpoint].gl_Position;
    position.xy += offset * position.w;
    gl_Position = position;
    gColor = vColor[endpoint];
    gAcross = across;
    EmitVertex();
}
void main()
{
    vec2 first = (gl_in[0].gl_Position.xy / gl_in[0].gl_Position.w * 0.5 + 0.5) * uViewportSize;
    vec2 second = (gl_in[1].gl_Position.xy / gl_in[1].gl_Position.w * 0.5 + 0.5) * uViewportSize;
    vec2 direction = second - first;
    float lengthSquared = dot(direction, direction);
    if (lengthSquared < 0.0001) return;
    direction *= inversesqrt(lengthSquared);
    vec2 normal = vec2(-direction.y, direction.x);
    float outerHalfWidth = uLineWidth * 0.5 + 1.0;
    vec2 offset = normal * outerHalfWidth * 2.0 / uViewportSize;
    EmitLineVertex(0, -offset, -outerHalfWidth);
    EmitLineVertex(0,  offset,  outerHalfWidth);
    EmitLineVertex(1, -offset, -outerHalfWidth);
    EmitLineVertex(1,  offset,  outerHalfWidth);
    EndPrimitive();
}
)GLSL";
    static constexpr const char *fragmentSource = R"GLSL(#version 330 core
in vec3 gColor;
noperspective in float gAcross;
uniform float uLineWidth;
out vec4 FragColor;
void main()
{
    float halfWidth = uLineWidth * 0.5;
    float alpha = 1.0 - smoothstep(max(halfWidth - 1.0, 0.0), halfWidth + 1.0, abs(gAcross));
    FragColor = vec4(gColor, alpha);
}
)GLSL";
    const unsigned int vertexShader = CompileShader(GL_VERTEX_SHADER, vertexSource);
    const unsigned int geometryShader = CompileShader(GL_GEOMETRY_SHADER, geometrySource);
    const unsigned int fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!vertexShader || !geometryShader || !fragmentShader)
    {
        if (vertexShader) glDeleteShader(vertexShader);
        if (geometryShader) glDeleteShader(geometryShader);
        if (fragmentShader) glDeleteShader(fragmentShader);
        return false;
    }
    m_program = glCreateProgram();
    glAttachShader(m_program, vertexShader);
    glAttachShader(m_program, geometryShader);
    glAttachShader(m_program, fragmentShader);
    glLinkProgram(m_program);
    glDeleteShader(vertexShader);
    glDeleteShader(geometryShader);
    glDeleteShader(fragmentShader);
    int linked = GL_FALSE;
    glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) { glDeleteProgram(m_program); m_program = 0; return false; }
    glGenVertexArrays(1, &m_vertexArray);
    glGenBuffers(1, &m_vertexBuffer);
    glBindVertexArray(m_vertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void *>(offsetof(LineVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), reinterpret_cast<void *>(offsetof(LineVertex, color)));
    glBindVertexArray(0);
    return true;
}

void EditorViewportInteraction::Render(const EditorSession &session,
                                       const PlutoGE::render::CameraData &camera,
                                       int viewportWidth,
                                       int viewportHeight)
{
    const auto *entity = session.GetSelectedEntity();
    if (!entity || !entity->IsActive() || viewportWidth <= 0 || viewportHeight <= 0 || !EnsureRenderer()) return;
    const glm::vec3 origin = entity->GetWorldPosition();
    const auto axes = GizmoAxes(session, *entity);
    const float unitsPerPixel = WorldUnitsPerPixel(camera, origin, viewportHeight);
    std::vector<LineVertex> vertices;
    const glm::vec3 highlight(1.0f, 0.86f, 0.20f);

    if (session.GetGizmoOperation() == EditorSession::GizmoOperation::Rotate)
    {
        const float radius = unitsPerPixel * kRingPixels;
        for (int axis = 0; axis < 3; ++axis)
        {
            const glm::vec3 color = axis == m_hoveredAxis || (m_drag.active && axis == m_drag.axis) ? highlight : kAxisColors[axis];
            const glm::vec3 u = axes[(axis + 1) % 3];
            const glm::vec3 v = axes[(axis + 2) % 3];
            for (int segment = 0; segment < kRingSegments; ++segment)
            {
                const float angleA = glm::two_pi<float>() * static_cast<float>(segment) / static_cast<float>(kRingSegments);
                const float angleB = glm::two_pi<float>() * static_cast<float>(segment + 1) / static_cast<float>(kRingSegments);
                AddLine(vertices, origin + (u * std::cos(angleA) + v * std::sin(angleA)) * radius,
                        origin + (u * std::cos(angleB) + v * std::sin(angleB)) * radius, color);
            }
        }
    }
    else
    {
        const float length = unitsPerPixel * kHandlePixels;
        for (int axis = 0; axis < 3; ++axis)
        {
            const glm::vec3 color = axis == m_hoveredAxis || (m_drag.active && axis == m_drag.axis) ? highlight : kAxisColors[axis];
            const glm::vec3 endpoint = origin + axes[axis] * length;
            AddLine(vertices, origin, endpoint, color);
            const glm::vec3 sideA = axes[(axis + 1) % 3];
            const glm::vec3 sideB = axes[(axis + 2) % 3];
            if (session.GetGizmoOperation() == EditorSession::GizmoOperation::Translate)
            {
                AddLine(vertices, endpoint, endpoint - axes[axis] * length * 0.16f + sideA * length * 0.07f, color);
                AddLine(vertices, endpoint, endpoint - axes[axis] * length * 0.16f - sideA * length * 0.07f, color);
            }
            else
            {
                AddLine(vertices, endpoint - sideA * length * 0.055f, endpoint + sideA * length * 0.055f, color);
                AddLine(vertices, endpoint - sideB * length * 0.055f, endpoint + sideB * length * 0.055f, color);
            }
        }
    }

    GLint previousProgram = 0, previousVertexArray = 0, previousArrayBuffer = 0;
    GLint previousBlendSourceRgb = GL_ONE, previousBlendDestinationRgb = GL_ZERO;
    GLint previousBlendSourceAlpha = GL_ONE, previousBlendDestinationAlpha = GL_ZERO;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
    glGetIntegerv(GL_BLEND_SRC_RGB, &previousBlendSourceRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &previousBlendDestinationRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &previousBlendSourceAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &previousBlendDestinationAlpha);
    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(m_program);
    const glm::mat4 viewProjection = camera.projection * camera.view;
    glUniformMatrix4fv(glGetUniformLocation(m_program, "uViewProjection"), 1, GL_FALSE, glm::value_ptr(viewProjection));
    glUniform2f(glGetUniformLocation(m_program, "uViewportSize"), static_cast<float>(viewportWidth), static_cast<float>(viewportHeight));
    glUniform1f(glGetUniformLocation(m_program, "uLineWidth"), 3.0f);
    glBindVertexArray(m_vertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(LineVertex)), vertices.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertices.size()));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousArrayBuffer));
    glBindVertexArray(static_cast<GLuint>(previousVertexArray));
    glUseProgram(static_cast<GLuint>(previousProgram));
    glBlendFuncSeparate(static_cast<GLenum>(previousBlendSourceRgb), static_cast<GLenum>(previousBlendDestinationRgb),
                        static_cast<GLenum>(previousBlendSourceAlpha), static_cast<GLenum>(previousBlendDestinationAlpha));
    if (!blendEnabled) glDisable(GL_BLEND);
    if (depthEnabled) glEnable(GL_DEPTH_TEST);
}

void EditorViewportInteraction::Shutdown()
{
    if (m_vertexBuffer) glDeleteBuffers(1, &m_vertexBuffer);
    if (m_vertexArray) glDeleteVertexArrays(1, &m_vertexArray);
    if (m_program) glDeleteProgram(m_program);
    m_vertexBuffer = 0;
    m_vertexArray = 0;
    m_program = 0;
}
