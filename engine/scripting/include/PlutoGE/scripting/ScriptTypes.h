#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

namespace PlutoGE::scripting
{
    enum class ScriptFieldType
    {
        None,
        Boolean,
        Int32,
        Float,
        Double,
        String,
        Vector2,
        Vector3,
        EntityId,
        GameObject,
        MeshComponent,
        CameraComponent,
        LightComponent,
        RigidbodyComponent,
        ColliderComponent,
        AnimationComponent,
        CanvasComponent,
        RectTransformComponent,
        UIImageComponent,
        UITextComponent,
        UIButtonComponent,
        ParticleSystemComponent,
        SoundEmitterComponent,
        PrefabAsset,
        ScriptableObjectAsset,
        MaterialAsset,
        InputMappingAsset,
    };

    enum class ScriptClassKind
    {
        Behaviour,
        ScriptableObject,
    };

    using ScriptFieldValue = std::variant<std::monostate, bool, int32_t, float, double, std::string, glm::vec2, glm::vec3, uint32_t>;

    struct ScriptFieldDefinition
    {
        std::string name;
        ScriptFieldType type = ScriptFieldType::None;
        bool serialized = true;
        // The managed full type name used to filter typed asset references.
        std::string referenceTypeName;
        ScriptFieldValue defaultValue{};
        bool defaultValueIsNull = false;
    };

    struct ScriptClassDefinition
    {
        std::string assemblyName;
        std::string namespaceName;
        std::string className;
        ScriptClassKind kind = ScriptClassKind::Behaviour;
        std::vector<std::string> assignableTypeNames;
        std::vector<ScriptFieldDefinition> fields;

        [[nodiscard]] std::string GetFullName() const
        {
            if (namespaceName.empty())
            {
                return className;
            }

            return namespaceName + "." + className;
        }
    };

    [[nodiscard]] bool IsFieldValueCompatible(ScriptFieldType type, const ScriptFieldValue &value);
    [[nodiscard]] ScriptFieldValue MakeDefaultFieldValue(ScriptFieldType type);
    [[nodiscard]] const ScriptFieldDefinition *FindFieldDefinition(const ScriptClassDefinition &scriptClass, std::string_view fieldName);
}
