#include "PlutoGE/core/CpuTrace.h"
#include "PlutoGE/scene/components/ScriptComponent.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/assets/AssetManager.h"
#include <stdexcept>
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scripting/ScriptEngine.h"
#include "PlutoGE/scripting/ScriptRuntime.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <unordered_set>

namespace PlutoGE::scene
{
    namespace
    {
        scene::PropertyType ToPropertyType(scripting::ScriptFieldType fieldType)
        {
            if (scripting::IsAssetFieldType(fieldType))
            {
                return scene::PropertyType::String;
            }

            switch (fieldType)
            {
            case scripting::ScriptFieldType::Boolean:
                return scene::PropertyType::Bool;
            case scripting::ScriptFieldType::Int32:
                return scene::PropertyType::Int;
            case scripting::ScriptFieldType::Float:
                return scene::PropertyType::Float;
            case scripting::ScriptFieldType::Double:
                return scene::PropertyType::Double;
            case scripting::ScriptFieldType::String:
                return scene::PropertyType::String;
            case scripting::ScriptFieldType::Vector2:
                return scene::PropertyType::Vec2;
            case scripting::ScriptFieldType::Vector3:
                return scene::PropertyType::Vec3;
            case scripting::ScriptFieldType::Enum:
                return scene::PropertyType::String;
            case scripting::ScriptFieldType::Color:
            case scripting::ScriptFieldType::Vector4:
            case scripting::ScriptFieldType::Quaternion:
                return scene::PropertyType::Vec4;
            case scripting::ScriptFieldType::EntityId:
            case scripting::ScriptFieldType::GameObject:
            case scripting::ScriptFieldType::MeshComponent:
            case scripting::ScriptFieldType::CameraComponent:
            case scripting::ScriptFieldType::LightComponent:
            case scripting::ScriptFieldType::RigidbodyComponent:
            case scripting::ScriptFieldType::ColliderComponent:
            case scripting::ScriptFieldType::AnimationComponent:
            case scripting::ScriptFieldType::CanvasComponent:
            case scripting::ScriptFieldType::RectTransformComponent:
            case scripting::ScriptFieldType::UIImageComponent:
            case scripting::ScriptFieldType::UITextComponent:
            case scripting::ScriptFieldType::UIButtonComponent:
            case scripting::ScriptFieldType::ParticleSystemComponent:
            case scripting::ScriptFieldType::SoundEmitterComponent:
                return scene::PropertyType::Entity;
            case scripting::ScriptFieldType::None:
            default:
                return scene::PropertyType::String;
            }
        }

        scripting::ScriptFieldType GetScriptFieldType(const scripting::ScriptFieldValue &value)
        {
            return scripting::InferFieldType(value);
        }

        std::string SerializeFieldValue(const scripting::ScriptFieldValue &value)
        {
            return scripting::FormatFieldValueText(value);
        }

        // Scene properties carry a coarse type; the script class definition restores the exact shape later
        // (NormalizeFieldValue), so this only needs to read each property type's text.
        scripting::ScriptFieldValue DeserializeFieldValue(scene::PropertyType propertyType, const std::string &value)
        {
            switch (propertyType)
            {
            case scene::PropertyType::Bool:
                return scripting::ParseFieldValueText(scripting::ScriptFieldType::Boolean, value);
            case scene::PropertyType::Int:
                return scripting::ParseFieldValueText(scripting::ScriptFieldType::Int32, value);
            case scene::PropertyType::Float:
                return scripting::ParseFieldValueText(scripting::ScriptFieldType::Float, value);
            case scene::PropertyType::Double:
                return scripting::ParseFieldValueText(scripting::ScriptFieldType::Double, value);
            case scene::PropertyType::Vec2:
                return scripting::ParseFieldValueText(scripting::ScriptFieldType::Vector2, value);
            case scene::PropertyType::Vec3:
                return scripting::ParseFieldValueText(scripting::ScriptFieldType::Vector3, value);
            case scene::PropertyType::Vec4:
                return scripting::ParseFieldValueText(scripting::ScriptFieldType::Vector4, value);
            case scene::PropertyType::Entity:
                return scripting::ParseFieldValueText(scripting::ScriptFieldType::EntityId, value);
            case scene::PropertyType::String:
            case scene::PropertyType::Color:
            case scene::PropertyType::Enum:
            default:
                return value;
            }
        }
    }

    ScriptComponent::ScriptComponent(const ScriptComponentConfig &config)
        : m_scriptClass(config.source.empty() ? config.scriptClass : config.source), m_fieldValues(config.fieldValues)
    {
    }

    ScriptComponent::~ScriptComponent()
    {
        Stop();
    }

    bool ScriptComponent::HasLiveInstance() const
    {
        return m_instance && !m_instance->IsFaulted();
    }

    void ScriptComponent::Prepare()
    {
        if (!m_started && !m_scriptClass.empty())
            EnsureInstance();
    }

    namespace
    {
        // Enabled scripts on active entities, parents before children.
        std::vector<ScriptComponent *> CollectStartableScripts(Entity &root)
        {
            std::vector<ScriptComponent *> scripts;
            const auto collect = [&](Entity &entity, const auto &self) -> void
            {
                if (!entity.IsActive())
                    return;
                for (auto *script : entity.GetComponents<ScriptComponent>())
                    if (script->IsEnabled())
                        scripts.push_back(script);
                for (auto *child : entity.GetChildren())
                    if (child)
                        self(*child, self);
            };
            collect(root, collect);
            return scripts;
        }
    }

    void ScriptComponent::PrepareHierarchy(Entity &root)
    {
        for (auto *script : CollectStartableScripts(root))
            script->Prepare();
    }

    void ScriptComponent::StartHierarchy(Entity &root)
    {
        const auto scripts = CollectStartableScripts(root);
        for (auto *script : scripts)
            script->Prepare();
        // OnCreate may destroy entities in this hierarchy; skip scripts whose entity is gone.
        std::vector<EntityID> owners;
        owners.reserve(scripts.size());
        for (auto *script : scripts)
            owners.push_back(script->GetOwner() ? script->GetOwner()->GetID() : 0);
        auto *scene = root.GetScene();
        for (std::size_t i = 0; i < scripts.size(); ++i)
        {
            if (scene && owners[i] != 0 && !scene->FindEntityByID(owners[i]))
                continue;
            scripts[i]->Start();
        }
    }

    void ScriptComponent::Start()
    {
        if (m_started || m_scriptClass.empty())
        {
            return;
        }

        EnsureInstance();
        if (!m_instance)
        {
            return;
        }

        m_instance->OnCreate();
        m_started = true;
    }

    void ScriptComponent::Stop()
    {
        if (!m_instance && !m_started)
        {
            return;
        }

        if (m_instance && m_started)
        {
            m_instance->OnDestroy();
        }
        m_instance.reset();
        m_started = false;
    }

    void ScriptComponent::Update(float deltaTime)
    {
        if (!core::Engine::GetInstance().IsRuntimeRunning())
        {
            return;
        }

        if (m_scriptClass.empty())
        {
            return;
        }

        core::CpuScope traceScope(core::CpuTrace::current ? GetScriptClass() + ".OnUpdate" : std::string{},
                                  core::CpuCategory::Scripts,
                                  core::CpuTrace::current && GetOwner() ? GetOwner()->GetName() : std::string{});
        Start();
        if (!HasLiveInstance())
        {
            return;
        }

        m_instance->OnUpdate(deltaTime);
    }

    void ScriptComponent::LateUpdate(float deltaTime)
    {
        if (!core::Engine::GetInstance().IsRuntimeRunning())
        {
            return;
        }

        if (m_scriptClass.empty())
        {
            return;
        }

        Start();
        if (!HasLiveInstance() || !m_instance->Implements(scripting::ScriptCallback::LateUpdate))
        {
            return;
        }

        core::CpuScope traceScope(core::CpuTrace::current ? GetScriptClass() + ".OnLateUpdate" : std::string{},
                                  core::CpuCategory::Scripts,
                                  core::CpuTrace::current && GetOwner() ? GetOwner()->GetName() : std::string{});
        m_instance->OnLateUpdate(deltaTime);
    }

    void ScriptComponent::FixedUpdate(float fixedDeltaTime)
    {
        if (!core::Engine::GetInstance().IsRuntimeRunning() || m_scriptClass.empty())
        {
            return;
        }

        Start();
        if (!HasLiveInstance() || !m_instance->Implements(scripting::ScriptCallback::FixedUpdate))
        {
            return;
        }

        core::CpuScope traceScope(core::CpuTrace::current ? GetScriptClass() + ".OnFixedUpdate" : std::string{},
                                  core::CpuCategory::Scripts,
                                  core::CpuTrace::current && GetOwner() ? GetOwner()->GetName() : std::string{});
        m_instance->OnFixedUpdate(fixedDeltaTime);
    }

    void ScriptComponent::OnCollisionEnter(uint32_t otherEntityId)
    {
        if (!core::Engine::GetInstance().IsRuntimeRunning() || m_scriptClass.empty())
        {
            return;
        }

        Start();
        if (HasLiveInstance())
        {
            m_instance->OnCollisionEnter(otherEntityId);
        }
    }

    void ScriptComponent::OnCollisionExit(uint32_t otherEntityId)
    {
        if (!core::Engine::GetInstance().IsRuntimeRunning() || m_scriptClass.empty())
        {
            return;
        }

        Start();
        if (HasLiveInstance())
        {
            m_instance->OnCollisionExit(otherEntityId);
        }
    }

    bool ScriptComponent::OnServerCommand(std::string_view command)
    {
        return m_started && HasLiveInstance() && m_instance->OnServerCommand(command);
    }

    void ScriptComponent::OnAnimationEvent(const render::AnimationClip::Event &event)
    {
        if (!core::Engine::GetInstance().IsRuntimeRunning() || m_scriptClass.empty())
            return;
        Start();
        if (HasLiveInstance())
            m_instance->OnAnimationEvent(event.name, event.stringParameter, event.floatParameter, event.intParameter);
    }

    bool ScriptComponent::WritesManagedAssetFieldMetadata() const
    {
        const auto &context = core::Engine::GetInstance().GetAssetManager();
        if (context.GetProjectRootDirectory().empty() || context.GetAssetPipelineVersion() < 5) return false;
        return m_assetFieldMetadataDeclared || m_scriptClass.empty() ||
               core::Engine::GetInstance().GetScriptEngine().FindClass(m_scriptClass) != nullptr;
    }

    std::vector<Property> ScriptComponent::Serialize() const
    {
        std::vector<Property> properties;
        properties.push_back(Property{
            .name = "Source",
            .type = PropertyType::String,
            .value = m_scriptClass,
        });

        std::unordered_set<std::string> serializedFieldNames;
        const auto serializedFields = GetSerializedFields();
        properties.reserve(properties.size() + serializedFields.size() + m_fieldValues.size());

        auto assetKinds = m_assetFieldKinds;
        for (const auto &field : serializedFields)
        {
            serializedFieldNames.insert(field.name);
            if (const auto type = scripting::GetFieldAssetType(field))
                assetKinds.insert_or_assign(field.name, assets::ManagedAssetFieldRole{*type, field.isArray});
            else assetKinds.erase(field.name);

            auto fieldValue = m_fieldValues.contains(field.name)
                                  ? m_fieldValues.at(field.name)
                                  : scripting::ResolveFieldDefault(field);

            properties.push_back(Property{
                .name = field.name,
                .type = ToPropertyType(field.type),
                .value = SerializeFieldValue(fieldValue),
            });
        }

        // Fields the class does not declare, in name order: the map's own order changes between loads, and a save has
        // to be stable for scene history and source control.
        std::vector<const std::string *> undeclaredFields;
        for (const auto &[fieldName, fieldValue] : m_fieldValues)
            if (!serializedFieldNames.contains(fieldName))
                undeclaredFields.push_back(&fieldName);
        std::sort(undeclaredFields.begin(), undeclaredFields.end(), [](const std::string *a, const std::string *b) { return *a < *b; });
        for (const auto *fieldName : undeclaredFields)
        {
            const auto &fieldValue = m_fieldValues.at(*fieldName);
            properties.push_back(Property{
                .name = *fieldName,
                .type = ToPropertyType(GetScriptFieldType(fieldValue)),
                .value = SerializeFieldValue(fieldValue),
            });
        }

        const auto &context = core::Engine::GetInstance().GetAssetManager();
        if (m_assetFieldMetadataDeclared && (context.GetProjectRootDirectory().empty() || context.GetAssetPipelineVersion() < 5))
            throw std::runtime_error("Persisted typed managed asset fields require project format 5.");
        if (WritesManagedAssetFieldMetadata())
        {
            properties.insert(properties.begin() + 1, Property{.name=std::string(assets::kManagedAssetFieldSchema),
                .type=PropertyType::String, .value="1"});
            for (const auto &[name, kind] : assetKinds)
                properties.push_back(Property{.name=std::string(assets::kManagedAssetFieldPrefix) + name,
                    .type=PropertyType::String, .value=assets::ManagedAssetFieldRoleName(kind)});
            std::vector<assets::ManagedAssetFieldRecord> records;
            records.reserve(properties.size());
            for (const auto &property : properties)
                records.push_back({property.name, property.value, property.type == PropertyType::String});
            assets::ManagedAssetFieldMetadata metadata;
            std::string error;
            if (!assets::ReadManagedAssetFieldMetadata(records, metadata, &error)) throw std::runtime_error(error);
        }
        return properties;
    }

    void ScriptComponent::Deserialize(const std::vector<Property> &properties)
    {
        std::unordered_map<std::string, scripting::ScriptFieldValue> fieldValues;
        std::string source = m_scriptClass;
        std::vector<assets::ManagedAssetFieldRecord> records;
        records.reserve(properties.size());
        for (const auto &property : properties)
            records.push_back({property.name, property.value, property.type == PropertyType::String});
        assets::ManagedAssetFieldMetadata metadata;
        std::string error;
        if (!assets::ReadManagedAssetFieldMetadata(records, metadata, &error)) throw std::runtime_error(error);

        for (const auto &property : properties)
        {
            if (property.name == "Source")
            {
                source = property.value;
                continue;
            }

            if (assets::IsManagedAssetFieldMetadata(property.name)) continue;
            auto value = DeserializeFieldValue(property.type, property.value);
            // Array roles keep their shape even while the script class is unavailable.
            if (const auto role = metadata.fields.find(property.name); role != metadata.fields.end() && role->second.isArray)
                value = scripting::DecodeReferenceArray(property.value).value_or(scripting::ScriptReferenceArray{});
            fieldValues[property.name] = std::move(value);
        }

        // Values saved under a field's former name follow the field ([FormerlySerializedAs]).
        if (const auto *scriptClass = core::Engine::GetInstance().GetScriptEngine().FindClass(source))
            scripting::MigrateRenamedFields(scriptClass->fields, fieldValues);

        const bool sourceChanged = m_scriptClass != source;
        m_fieldValues = std::move(fieldValues);

        if (sourceChanged)
        {
            SetSource(source);
            m_assetFieldKinds = std::move(metadata.fields);
            m_assetFieldMetadataDeclared = metadata.declared;
            return;
        }

        m_assetFieldKinds = std::move(metadata.fields);
        m_assetFieldMetadataDeclared = metadata.declared;
        ApplySerializedFields();
    }

    void ScriptComponent::SetSource(const std::string &source)
    {
        SetScriptClass(source);
    }

    void ScriptComponent::SetScriptClass(const std::string &scriptClass)
    {
        if (m_scriptClass == scriptClass)
        {
            return;
        }

        m_scriptClass = scriptClass;
        m_assetFieldKinds.clear();
        m_assetFieldMetadataDeclared = false;
        Reload();
    }

    bool ScriptComponent::SetFieldValue(const std::string &fieldName, const scripting::ScriptFieldValue &value)
    {
        if (assets::IsManagedAssetFieldMetadata(fieldName)) return false;
        auto &scriptEngine = core::Engine::GetInstance().GetScriptEngine();
        if (const auto *scriptClass = scriptEngine.FindClass(m_scriptClass))
        {
            const auto *fieldDefinition = scripting::FindFieldDefinition(*scriptClass, fieldName);
            if (!fieldDefinition || !fieldDefinition->serialized || !scripting::IsFieldValueCompatible(*fieldDefinition, value))
            {
                return false;
            }
        }

        m_fieldValues[fieldName] = value;
        ApplySerializedFields();
        return true;
    }

    std::optional<scripting::ScriptFieldValue> ScriptComponent::GetFieldValue(const std::string &fieldName) const
    {
        if (m_instance && core::Engine::GetInstance().IsRuntimeRunning())
        {
            if (const auto runtimeValue = m_instance->GetFieldValue(fieldName))
            {
                return runtimeValue;
            }
        }

        const auto iterator = m_fieldValues.find(fieldName);
        if (iterator == m_fieldValues.end())
        {
            return std::nullopt;
        }

        return iterator->second;
    }

    std::unordered_map<std::string, scripting::ScriptFieldValue> ScriptComponent::GetFieldValuesSnapshot() const
    {
        auto fieldValues = m_fieldValues;
        if (m_instance && core::Engine::GetInstance().IsRuntimeRunning())
        {
            for (auto &[fieldName, fieldValue] : m_instance->GetFieldValuesSnapshot())
            {
                fieldValues.insert_or_assign(std::move(fieldName), std::move(fieldValue));
            }
        }

        return fieldValues;
    }

    std::vector<scripting::ScriptFieldDefinition> ScriptComponent::GetSerializedFields() const
    {
        return core::Engine::GetInstance().GetScriptEngine().GetSerializedFields(m_scriptClass);
    }

    bool ScriptComponent::RestoreFieldValuesToDefaults()
    {
        bool changed = false;
        for (const auto &field : GetSerializedFields())
        {
            // A null reflected default means the script did not provide a value
            // that can replace the component's current reference/value.
            if (field.defaultValueIsNull ||
                std::holds_alternative<std::monostate>(field.defaultValue) ||
                !scripting::IsFieldValueCompatible(field, field.defaultValue))
            {
                continue;
            }

            const auto currentValue = GetFieldValue(field.name);
            if (!currentValue || *currentValue != field.defaultValue)
            {
                m_fieldValues.insert_or_assign(field.name, field.defaultValue);
                changed = true;
            }
        }

        if (changed)
        {
            ApplySerializedFields();
        }
        return changed;
    }

    void ScriptComponent::RemapEntityReferences(const std::unordered_map<uint32_t, uint32_t> &entityIdRemap)
    {
        bool changed = false;
        for (auto &[fieldName, fieldValue] : m_fieldValues)
        {
            (void)fieldName;
            auto *entityId = std::get_if<uint32_t>(&fieldValue);
            if (!entityId)
            {
                continue;
            }

            const auto remappedEntity = entityIdRemap.find(*entityId);
            if (remappedEntity == entityIdRemap.end())
            {
                continue;
            }

            *entityId = remappedEntity->second;
            changed = true;
        }

        if (changed)
        {
            ApplySerializedFields();
        }
    }

    void ScriptComponent::Reload()
    {
        Stop();
    }

    void ScriptComponent::EnsureInstance()
    {
        const auto &config = core::Engine::GetInstance().GetConfig();
        if (config.headless && std::find(config.headlessScriptClasses.begin(),
            config.headlessScriptClasses.end(), m_scriptClass) == config.headlessScriptClasses.end())
            return;

        if (m_instance || m_scriptClass.empty())
        {
            return;
        }

        auto &scripts = core::Engine::GetInstance().GetScriptEngine();
        if (const auto *definition = scripts.FindClass(m_scriptClass); definition && definition->IsEditorCommand()) return;
        m_instance = scripts.CreateInstance(m_scriptClass);
        if (!m_instance)
        {
            return;
        }

        m_instance->SetOwner(GetOwner());
        ApplySerializedFields();
    }

    void ScriptComponent::ApplySerializedFields()
    {
        if (!m_instance)
        {
            return;
        }

        auto &scriptEngine = core::Engine::GetInstance().GetScriptEngine();
        if (const auto *scriptClass = scriptEngine.FindClass(m_scriptClass))
        {
            scripting::MigrateRenamedFields(scriptClass->fields, m_fieldValues);
            for (const auto &field : scriptClass->fields)
            {
                if (!field.serialized)
                {
                    continue;
                }

                if (const auto value = m_fieldValues.find(field.name); value != m_fieldValues.end())
                {
                    value->second = scripting::NormalizeFieldValue(field, std::move(value->second));
                }
                else
                {
                    m_fieldValues[field.name] = scripting::ResolveFieldDefault(field);
                }
            }
        }

        m_instance->ApplyFieldValues(m_fieldValues);
    }
}
