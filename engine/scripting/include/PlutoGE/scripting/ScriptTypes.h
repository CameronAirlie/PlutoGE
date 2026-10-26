#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <algorithm>

#include <glm/glm.hpp>

#include "PlutoGE/assets/AssetType.h"

namespace PlutoGE::scripting
{
    // Values are part of the managed bridge protocol (ScriptBridge.MapFieldType):
    // append only, never reorder.
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
        // Superseded by Asset; still produced by older ScriptCore builds.
        MaterialAsset,
        InputMappingAsset,
        // A reference to a project asset of ScriptFieldDefinition::assetType.
        Asset,
    };

    enum class ScriptClassKind
    {
        Behaviour,
        ScriptableObject,
    };

    // The value of an array field: one asset reference per element, empty for an unassigned slot.
    using ScriptReferenceArray = std::vector<std::string>;

    using ScriptFieldValue = std::variant<std::monostate, bool, int32_t, float, double, std::string, glm::vec2, glm::vec3, uint32_t,
                                          ScriptReferenceArray>;

    struct ScriptFieldDefinition
    {
        std::string name;
        // For an array field, the type of each element.
        ScriptFieldType type = ScriptFieldType::None;
        bool serialized = true;
        // The managed full type name (of each element, for arrays) used to filter typed asset references.
        std::string referenceTypeName;
        ScriptFieldValue defaultValue{};
        bool defaultValueIsNull = false;
        // The referenced asset type of an Asset field; Unknown otherwise.
        assets::ProjectAssetType assetType = assets::ProjectAssetType::Unknown;
        // A managed T[] or List<T>; its value is a ScriptReferenceArray. Only asset element types are supported.
        bool isArray = false;
    };

    // The asset type a field (or each of its elements) references, for Asset and
    // the dedicated legacy asset field types; nullopt for non-asset fields. Asset
    // references are always stored as reference strings, whatever the managed type.
    [[nodiscard]] std::optional<assets::ProjectAssetType> GetFieldAssetType(const ScriptFieldDefinition &field);
    [[nodiscard]] bool IsAssetFieldType(ScriptFieldType type);

    // Definition-aware forms of IsFieldValueCompatible/MakeDefaultFieldValue that also understand arrays.
    [[nodiscard]] bool IsFieldValueCompatible(const ScriptFieldDefinition &field, const ScriptFieldValue &value);
    [[nodiscard]] ScriptFieldValue MakeDefaultFieldValue(const ScriptFieldDefinition &field);
    // The reflected default when it suits the field, otherwise the type's empty value.
    [[nodiscard]] ScriptFieldValue ResolveFieldDefault(const ScriptFieldDefinition &field);
    // Restores a value whose persisted text form lost its shape (scene properties and
    // scriptable assets store arrays as encoded text); other values pass through.
    [[nodiscard]] ScriptFieldValue NormalizeFieldValue(const ScriptFieldDefinition &field, ScriptFieldValue value);
    // The persisted text of an array value, and its inverse (nullopt when malformed).
    [[nodiscard]] std::string EncodeReferenceArray(const ScriptReferenceArray &references);
    [[nodiscard]] std::optional<ScriptReferenceArray> DecodeReferenceArray(std::string_view text);

    // Per-frame callbacks a class implements. Mirrors ScriptBridge.ScriptCallbacks.
    enum class ScriptCallback : std::uint8_t
    {
        Update = 1,
        FixedUpdate = 2,
        LateUpdate = 4,
    };
    inline constexpr std::uint8_t AllScriptCallbacks = 0x7;

    struct ScriptClassDefinition
    {
        std::string assemblyName;
        std::string namespaceName;
        std::string className;
        ScriptClassKind kind = ScriptClassKind::Behaviour;
        std::vector<std::string> assignableTypeNames;
        std::vector<ScriptFieldDefinition> fields;
        // Metadata without the mask (older bridges) conservatively implements everything.
        std::uint8_t callbacks = AllScriptCallbacks;
        [[nodiscard]] bool Implements(ScriptCallback callback) const
        {
            return (callbacks & static_cast<std::uint8_t>(callback)) != 0;
        }
        [[nodiscard]] bool IsEditorCommand() const
        {
            return std::find(assignableTypeNames.begin(), assignableTypeNames.end(),
                "PlutoGE.ScriptCore.Authoring.EditorCommand") != assignableTypeNames.end();
        }

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
