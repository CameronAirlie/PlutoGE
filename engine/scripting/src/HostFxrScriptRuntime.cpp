#include "PlutoGE/scripting/HostFxrScriptRuntime.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/platform/InputState.h"
#include "PlutoGE/scripting/ScriptLogging.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/ParticleSystemComponent.h"
#include "PlutoGE/scene/components/RigidbodyComponent.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/scene/components/UIComponent.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace PlutoGE::scripting
{
    namespace
    {
#ifdef _WIN32
        using char_t = wchar_t;
        using hostfxr_handle = void *;

        struct hostfxr_initialize_parameters
        {
            size_t size;
            const char_t *host_path;
            const char_t *dotnet_root;
        };

        enum hostfxr_delegate_type
        {
            hdt_com_activation = 0,
            hdt_load_in_memory_assembly = 1,
            hdt_winrt_activation = 2,
            hdt_com_register = 3,
            hdt_com_unregister = 4,
            hdt_load_assembly_and_get_function_pointer = 5,
        };

        using hostfxr_initialize_for_runtime_config_fn = int(__cdecl *)(const char_t *, const hostfxr_initialize_parameters *, hostfxr_handle *);
        using hostfxr_get_runtime_delegate_fn = int(__cdecl *)(hostfxr_handle, hostfxr_delegate_type, void **);
        using hostfxr_close_fn = int(__cdecl *)(hostfxr_handle);
        using load_assembly_and_get_function_pointer_fn = int(__cdecl *)(const char_t *, const char_t *, const char_t *, const char_t *, void *, void **);

        using load_script_assembly_fn = int(__cdecl *)(const char *);
        using unload_script_assembly_fn = int(__cdecl *)();
        using get_marshaled_string_fn = const char *(__cdecl *)();
        using get_field_data_fn = const char *(__cdecl *)(int64_t);
        using free_marshaled_string_fn = void(__cdecl *)(const char *);
        using create_script_instance_fn = int64_t(__cdecl *)(const char *, uint32_t);
        using destroy_script_instance_fn = void(__cdecl *)(int64_t);
        using invoke_on_create_fn = int(__cdecl *)(int64_t);
        using invoke_on_update_fn = int(__cdecl *)(int64_t, float);
        using invoke_on_collision_fn = int(__cdecl *)(int64_t, uint32_t);
        using apply_field_data_fn = int(__cdecl *)(int64_t, const char *);
        using set_entity_id_fn = int(__cdecl *)(int64_t, uint32_t);
        using register_game_object_api_fn = int(__cdecl *)(void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *);
        using register_prefab_api_fn = int(__cdecl *)(void *);
        using register_scriptable_object_api_fn = int(__cdecl *)(void *);
        using register_component_api_fn = int(__cdecl *)(void *, void *, void *);
        using register_camera_component_api_fn = int(__cdecl *)(void *, void *, void *, void *);
        using register_light_component_api_fn = int(__cdecl *)(void *, void *, void *, void *);
        using register_mesh_component_api_fn = int(__cdecl *)(void *, void *, void *, void *, void *, void *);
        using register_animation_component_api_fn = int(__cdecl *)(void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *);
        using register_rigidbody_component_api_fn = int(__cdecl *)(void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *);
        using register_collider_component_api_fn = int(__cdecl *)(void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *);
        using register_particle_system_component_api_fn = int(__cdecl *)(void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *);
        using register_runtime_ui_api_fn = int(__cdecl *)(void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *);
        using register_input_api_fn = int(__cdecl *)(void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *);
        using register_physics_api_fn = int(__cdecl *)(void *, void *, void *);
        using register_debug_api_fn = int(__cdecl *)(void *);

        struct NativeVector3
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        struct NativeRaycastHit
        {
            uint32_t entityId = 0;
            NativeVector3 point{};
            NativeVector3 normal{};
            float distance = 0.0f;
        };

        using get_entity_vector3_fn = NativeVector3(__cdecl *)(uint32_t);
        using set_entity_vector3_fn = void(__cdecl *)(uint32_t, NativeVector3);
        using get_entity_active_fn = int(__cdecl *)(uint32_t);
        using set_entity_active_fn = void(__cdecl *)(uint32_t, int32_t);
        using get_entity_tag_count_fn = int(__cdecl *)(uint32_t);
        using get_entity_tag_fn = const char *(__cdecl *)(uint32_t, int32_t);
        using destroy_entity_fn = int(__cdecl *)(uint32_t);
        using get_entity_name_fn = const char *(__cdecl *)(uint32_t);
        using find_entity_by_name_fn = uint32_t(__cdecl *)(const char *);
        using get_entity_count_by_tag_fn = int32_t(__cdecl *)(const char *);
        using get_entity_by_tag_fn = uint32_t(__cdecl *)(const char *, int32_t);
        using instantiate_prefab_fn = uint32_t(__cdecl *)(const char *);
        using load_scriptable_object_asset_fn = const char *(__cdecl *)(const char *);
        using has_entity_component_fn = int(__cdecl *)(uint32_t, int32_t);
        using get_component_enabled_fn = int(__cdecl *)(uint32_t, int32_t);
        using set_component_enabled_fn = void(__cdecl *)(uint32_t, int32_t, int32_t);
        using get_camera_main_fn = int(__cdecl *)(uint32_t);
        using set_camera_main_fn = void(__cdecl *)(uint32_t, int32_t);
        using get_camera_fov_fn = float(__cdecl *)(uint32_t);
        using set_camera_fov_fn = void(__cdecl *)(uint32_t, float);
        using get_light_intensity_fn = float(__cdecl *)(uint32_t);
        using set_light_intensity_fn = void(__cdecl *)(uint32_t, float);
        using get_light_color_fn = NativeVector3(__cdecl *)(uint32_t);
        using set_light_color_fn = void(__cdecl *)(uint32_t, NativeVector3);
        using get_mesh_static_fn = int(__cdecl *)(uint32_t);
        using set_mesh_static_fn = void(__cdecl *)(uint32_t, int32_t);
        using get_mesh_color_fn = NativeVector3(__cdecl *)(uint32_t);
        using set_mesh_color_fn = void(__cdecl *)(uint32_t, NativeVector3);
        using get_component_float_fn = float(__cdecl *)(uint32_t);
        using set_component_float_fn = void(__cdecl *)(uint32_t, float);
        using get_component_bool_fn = int(__cdecl *)(uint32_t);
        using set_component_bool_fn = void(__cdecl *)(uint32_t, int32_t);
        using get_component_int_fn = int(__cdecl *)(uint32_t);
        using set_component_int_fn = void(__cdecl *)(uint32_t, int32_t);
        using get_component_string_by_index_fn = const char *(__cdecl *)(uint32_t, int32_t);
        using get_component_string_fn = const char *(__cdecl *)(uint32_t);
        using get_component_float_by_index_fn = float(__cdecl *)(uint32_t, int32_t);
        using component_action_fn = void(__cdecl *)(uint32_t);
        using get_component_vector3_fn = NativeVector3(__cdecl *)(uint32_t);
        using set_component_vector3_fn = void(__cdecl *)(uint32_t, NativeVector3);
        using set_component_string_fn = void(__cdecl *)(uint32_t, const char *);
        using particle_emit_at_fn = void(__cdecl *)(uint32_t, NativeVector3, int32_t);
        using set_animation_bool_parameter_fn = void(__cdecl *)(uint32_t, const char *, int32_t);
        using set_animation_float_parameter_fn = void(__cdecl *)(uint32_t, const char *, float);
        using set_animation_int_parameter_fn = void(__cdecl *)(uint32_t, const char *, int32_t);
        using get_input_key_fn = int(__cdecl *)(int32_t);
        using get_input_mouse_button_fn = int(__cdecl *)(int32_t);
        using get_input_mouse_vector2_fn = NativeVector3(__cdecl *)();
        using get_input_quit_requested_fn = int(__cdecl *)();
        using get_input_cursor_locked_fn = int(__cdecl *)();
        using set_input_cursor_locked_fn = void(__cdecl *)(int32_t);
        using physics_raycast_fn = int(__cdecl *)(NativeVector3, NativeVector3, float, uint32_t, NativeRaycastHit *);
        using physics_raycast_tagged_fn = int(__cdecl *)(NativeVector3, NativeVector3, float, uint32_t, const char *, NativeRaycastHit *);
        using physics_move_kinematic_fn = NativeVector3(__cdecl *)(uint32_t, NativeVector3, float);
        using script_log_fn = void(__cdecl *)(int32_t, const char *);
        constexpr std::wstring_view kScriptBridgeType = L"PlutoGE.ScriptCore.Native.ScriptBridge, PlutoGE.ScriptCore";
        constexpr std::wstring_view kScriptCoreAssembly = L"PlutoGE.ScriptCore.dll";
        constexpr std::wstring_view kScriptCoreRuntimeConfig = L"PlutoGE.ScriptCore.runtimeconfig.json";

        const wchar_t *GetUnmanagedCallersOnlyMethodMarker()
        {
            return reinterpret_cast<const wchar_t *>(static_cast<std::intptr_t>(-1));
        }

        enum class ManagedComponentKind : int32_t
        {
            Mesh = 0,
            Camera = 1,
            Light = 2,
            Script = 3,
            Rigidbody = 4,
            Collider = 5,
            Animation = 6,
            Canvas = 7,
            RectTransform = 8,
            UIImage = 9,
            UIText = 10,
            UIButton = 11,
            ParticleSystem = 12,
        };

        std::wstring Utf8ToWide(std::string_view text)
        {
            if (text.empty())
            {
                return {};
            }

            const int wideSize = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            if (wideSize <= 0)
            {
                return {};
            }

            std::wstring wide(static_cast<size_t>(wideSize), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), wideSize);
            return wide;
        }

        std::string WideToUtf8(std::wstring_view text)
        {
            if (text.empty())
            {
                return {};
            }

            const int utf8Size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            if (utf8Size <= 0)
            {
                return {};
            }

            std::string utf8(static_cast<size_t>(utf8Size), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), utf8Size, nullptr, nullptr);
            return utf8;
        }

        std::optional<std::wstring> GetEnvironmentVariableText(const wchar_t *name)
        {
            const DWORD requiredSize = GetEnvironmentVariableW(name, nullptr, 0);
            if (requiredSize == 0)
            {
                return std::nullopt;
            }

            std::wstring value(requiredSize, L'\0');
            GetEnvironmentVariableW(name, value.data(), requiredSize);
            if (!value.empty() && value.back() == L'\0')
            {
                value.pop_back();
            }

            return value;
        }

        std::optional<std::filesystem::path> FindHostFxrLibrary()
        {
            std::vector<std::filesystem::path> dotnetRoots;
            if (const auto dotnetRoot = GetEnvironmentVariableText(L"DOTNET_ROOT"))
            {
                dotnetRoots.emplace_back(*dotnetRoot);
            }

            if (const auto programFiles = GetEnvironmentVariableText(L"ProgramFiles"))
            {
                dotnetRoots.emplace_back(std::filesystem::path(*programFiles) / "dotnet");
            }

            for (const auto &dotnetRoot : dotnetRoots)
            {
                const auto fxrDirectory = dotnetRoot / "host" / "fxr";
                if (!std::filesystem::exists(fxrDirectory))
                {
                    continue;
                }

                std::vector<std::filesystem::path> versions;
                for (const auto &entry : std::filesystem::directory_iterator(fxrDirectory))
                {
                    if (entry.is_directory())
                    {
                        versions.push_back(entry.path());
                    }
                }

                std::sort(versions.begin(), versions.end());
                for (auto iterator = versions.rbegin(); iterator != versions.rend(); ++iterator)
                {
                    const auto candidate = *iterator / "hostfxr.dll";
                    if (std::filesystem::exists(candidate))
                    {
                        return candidate;
                    }
                }
            }

            return std::nullopt;
        }

        std::vector<std::filesystem::path> BuildScriptCoreCandidates(const std::filesystem::path &assemblyPath)
        {
            std::vector<std::filesystem::path> candidates;

            if (const auto envOverride = GetEnvironmentVariableText(L"PLUTOGE_SCRIPTCORE_DIR"))
            {
                candidates.emplace_back(*envOverride);
            }

            std::array<wchar_t, MAX_PATH> modulePath{};
            const DWORD modulePathLength = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
            auto current = std::filesystem::current_path();
            std::vector<std::filesystem::path> searchRoots;
            if (modulePathLength > 0 && modulePathLength < modulePath.size())
            {
                current = std::filesystem::path(modulePath.data()).parent_path().lexically_normal();
                searchRoots.push_back(current);
            }

            searchRoots.push_back(std::filesystem::current_path());

            for (auto root : searchRoots)
            {
                while (!root.empty())
                {
                    candidates.push_back(root / "engine" / "scripting" / "managed" / "PlutoGE.ScriptCore" / "bin" / "Debug" / "net8.0");
                    candidates.push_back(root / "engine" / "scripting" / "managed" / "PlutoGE.ScriptCore" / "bin" / "Release" / "net8.0");

                    if (root == root.root_path())
                    {
                        break;
                    }

                    root = root.parent_path();
                }
            }

            if (modulePathLength > 0 && modulePathLength < modulePath.size())
            {
                candidates.push_back(std::filesystem::path(modulePath.data()).parent_path().lexically_normal());
            }

            candidates.push_back(std::filesystem::current_path());
            candidates.push_back(assemblyPath.parent_path());

            return candidates;
        }

        std::optional<std::pair<std::filesystem::path, std::filesystem::path>> FindScriptCorePaths(const std::filesystem::path &assemblyPath)
        {
            for (const auto &candidateDirectory : BuildScriptCoreCandidates(assemblyPath))
            {
                const auto assemblyCandidate = candidateDirectory / kScriptCoreAssembly;
                const auto runtimeConfigCandidate = candidateDirectory / kScriptCoreRuntimeConfig;
                if (std::filesystem::exists(assemblyCandidate) && std::filesystem::exists(runtimeConfigCandidate))
                {
                    return std::make_pair(assemblyCandidate, runtimeConfigCandidate);
                }
            }

            return std::nullopt;
        }

        std::vector<std::string> SplitEscaped(std::string_view text, char delimiter)
        {
            std::vector<std::string> parts;
            std::string current;
            bool escaping = false;

            for (const char character : text)
            {
                if (escaping)
                {
                    switch (character)
                    {
                    case 'n':
                        current.push_back('\n');
                        break;
                    case 't':
                        current.push_back('\t');
                        break;
                    case '\\':
                        current.push_back('\\');
                        break;
                    default:
                        current.push_back(character);
                        break;
                    }

                    escaping = false;
                    continue;
                }

                if (character == '\\')
                {
                    escaping = true;
                    continue;
                }

                if (character == delimiter)
                {
                    parts.push_back(current);
                    current.clear();
                    continue;
                }

                current.push_back(character);
            }

            parts.push_back(current);
            return parts;
        }

        std::string EscapeText(std::string_view text)
        {
            std::string escaped;
            escaped.reserve(text.size());

            for (const char character : text)
            {
                switch (character)
                {
                case '\\':
                    escaped += "\\\\";
                    break;
                case '\n':
                    escaped += "\\n";
                    break;
                case '\t':
                    escaped += "\\t";
                    break;
                default:
                    escaped.push_back(character);
                    break;
                }
            }

            return escaped;
        }

        ScriptFieldType ParseFieldType(std::string_view token)
        {
            int fieldTypeValue = 0;
            std::from_chars(token.data(), token.data() + token.size(), fieldTypeValue);
            if (fieldTypeValue < static_cast<int>(ScriptFieldType::None) || fieldTypeValue > static_cast<int>(ScriptFieldType::ScriptableObjectAsset))
            {
                return ScriptFieldType::None;
            }

            return static_cast<ScriptFieldType>(fieldTypeValue);
        }

        float ParseFloat(std::string_view token)
        {
            return std::strtof(std::string(token).c_str(), nullptr);
        }

        double ParseDouble(std::string_view token)
        {
            return std::strtod(std::string(token).c_str(), nullptr);
        }

        ScriptFieldValue ParseFieldValue(ScriptFieldType type, const std::string &token)
        {
            switch (type)
            {
            case ScriptFieldType::Boolean:
                return token == "true";
            case ScriptFieldType::Int32:
            {
                int32_t value = 0;
                std::from_chars(token.data(), token.data() + token.size(), value);
                return value;
            }
            case ScriptFieldType::Float:
                return ParseFloat(token);
            case ScriptFieldType::Double:
                return ParseDouble(token);
            case ScriptFieldType::String:
            case ScriptFieldType::PrefabAsset:
            case ScriptFieldType::ScriptableObjectAsset:
                return token;
            case ScriptFieldType::Vector2:
            {
                const auto parts = SplitEscaped(token, ',');
                if (parts.size() != 2)
                {
                    return glm::vec2{0.0f, 0.0f};
                }

                return glm::vec2{ParseFloat(parts[0]), ParseFloat(parts[1])};
            }
            case ScriptFieldType::Vector3:
            {
                const auto parts = SplitEscaped(token, ',');
                if (parts.size() != 3)
                {
                    return glm::vec3{0.0f, 0.0f, 0.0f};
                }

                return glm::vec3{ParseFloat(parts[0]), ParseFloat(parts[1]), ParseFloat(parts[2])};
            }
            case ScriptFieldType::EntityId:
            case ScriptFieldType::GameObject:
            case ScriptFieldType::MeshComponent:
            case ScriptFieldType::CameraComponent:
            case ScriptFieldType::LightComponent:
            case ScriptFieldType::RigidbodyComponent:
            case ScriptFieldType::ColliderComponent:
            case ScriptFieldType::AnimationComponent:
            case ScriptFieldType::CanvasComponent:
            case ScriptFieldType::RectTransformComponent:
            case ScriptFieldType::UIImageComponent:
            case ScriptFieldType::UITextComponent:
            case ScriptFieldType::UIButtonComponent:
            case ScriptFieldType::ParticleSystemComponent:
            {
                uint32_t value = 0;
                std::from_chars(token.data(), token.data() + token.size(), value);
                return value;
            }
            case ScriptFieldType::None:
            default:
                return std::monostate{};
            }
        }

        ScriptFieldType GetFieldTypeForValue(const ScriptFieldValue &value)
        {
            if (std::holds_alternative<bool>(value))
            {
                return ScriptFieldType::Boolean;
            }

            if (std::holds_alternative<int32_t>(value))
            {
                return ScriptFieldType::Int32;
            }

            if (std::holds_alternative<float>(value))
            {
                return ScriptFieldType::Float;
            }

            if (std::holds_alternative<double>(value))
            {
                return ScriptFieldType::Double;
            }

            if (std::holds_alternative<std::string>(value))
            {
                return ScriptFieldType::String;
            }

            if (std::holds_alternative<glm::vec2>(value))
            {
                return ScriptFieldType::Vector2;
            }

            if (std::holds_alternative<glm::vec3>(value))
            {
                return ScriptFieldType::Vector3;
            }

            if (std::holds_alternative<uint32_t>(value))
            {
                return ScriptFieldType::EntityId;
            }

            return ScriptFieldType::None;
        }

        ScriptFieldType GetSerializedFieldType(std::string_view fieldName,
                                               const std::vector<ScriptFieldDefinition> &fieldDefinitions,
                                               const ScriptFieldValue &fieldValue)
        {
            for (const auto &fieldDefinition : fieldDefinitions)
            {
                if (fieldDefinition.name == fieldName)
                {
                    return fieldDefinition.type;
                }
            }

            return GetFieldTypeForValue(fieldValue);
        }

        std::string SerializeFieldValue(const ScriptFieldValue &value)
        {
            return std::visit(
                [](const auto &typedValue) -> std::string
                {
                    using ValueType = std::decay_t<decltype(typedValue)>;

                    if constexpr (std::is_same_v<ValueType, std::monostate>)
                    {
                        return {};
                    }
                    else if constexpr (std::is_same_v<ValueType, bool>)
                    {
                        return typedValue ? "true" : "false";
                    }
                    else if constexpr (std::is_same_v<ValueType, int32_t> || std::is_same_v<ValueType, uint32_t>)
                    {
                        return std::to_string(typedValue);
                    }
                    else if constexpr (std::is_same_v<ValueType, float> || std::is_same_v<ValueType, double>)
                    {
                        return std::to_string(typedValue);
                    }
                    else if constexpr (std::is_same_v<ValueType, std::string>)
                    {
                        return typedValue;
                    }
                    else if constexpr (std::is_same_v<ValueType, glm::vec2>)
                    {
                        return std::to_string(typedValue.x) + "," + std::to_string(typedValue.y);
                    }
                    else if constexpr (std::is_same_v<ValueType, glm::vec3>)
                    {
                        return std::to_string(typedValue.x) + "," + std::to_string(typedValue.y) + "," + std::to_string(typedValue.z);
                    }
                    else
                    {
                        return {};
                    }
                },
                value);
        }

        std::string SerializeFieldData(const std::unordered_map<std::string, ScriptFieldValue> &fieldValues,
                                       const std::vector<ScriptFieldDefinition> &fieldDefinitions)
        {
            std::ostringstream stream;
            for (const auto &[fieldName, fieldValue] : fieldValues)
            {
                const auto fieldType = GetSerializedFieldType(fieldName, fieldDefinitions, fieldValue);
                stream << "FIELD\t"
                       << EscapeText(fieldName) << '\t'
                       << static_cast<int>(fieldType) << '\t'
                       << EscapeText(SerializeFieldValue(fieldValue)) << '\n';
            }

            return stream.str();
        }

        std::vector<ScriptClassDefinition> ParseMetadata(std::string_view metadata)
        {
            std::vector<ScriptClassDefinition> classes;
            std::optional<ScriptClassDefinition> currentClass;

            size_t lineStart = 0;
            while (lineStart <= metadata.size())
            {
                const size_t lineEnd = metadata.find('\n', lineStart);
                const auto line = metadata.substr(lineStart, lineEnd == std::string_view::npos ? metadata.size() - lineStart : lineEnd - lineStart);

                if (!line.empty())
                {
                    const auto tokens = SplitEscaped(line, '\t');
                    if (!tokens.empty())
                    {
                        if ((tokens[0] == "CLASS" || tokens[0] == "OBJECT") && tokens.size() >= 4)
                        {
                            if (currentClass)
                            {
                                classes.push_back(*currentClass);
                            }

                            currentClass = ScriptClassDefinition{};
                            currentClass->assemblyName = tokens[1];
                            currentClass->namespaceName = tokens[2];
                            currentClass->className = tokens[3];
                            currentClass->kind = tokens[0] == "OBJECT" ? ScriptClassKind::ScriptableObject : ScriptClassKind::Behaviour;
                            if (tokens.size() >= 5)
                            {
                                currentClass->assignableTypeNames = SplitEscaped(tokens[4], ';');
                            }
                        }
                        else if (tokens[0] == "FIELD" && tokens.size() >= 5 && currentClass)
                        {
                            ScriptFieldDefinition fieldDefinition;
                            fieldDefinition.name = tokens[1];
                            fieldDefinition.type = ParseFieldType(tokens[2]);
                            fieldDefinition.serialized = tokens[3] == "1";
                            fieldDefinition.defaultValue = ParseFieldValue(fieldDefinition.type, tokens[4]);
                            if (tokens.size() >= 6)
                            {
                                fieldDefinition.referenceTypeName = tokens[5];
                            }
                            currentClass->fields.push_back(std::move(fieldDefinition));
                        }
                        else if (tokens[0] == "END" && currentClass)
                        {
                            classes.push_back(*currentClass);
                            currentClass.reset();
                        }
                    }
                }

                if (lineEnd == std::string_view::npos)
                {
                    break;
                }

                lineStart = lineEnd + 1;
            }

            if (currentClass)
            {
                classes.push_back(*currentClass);
            }

            return classes;
        }

        std::unordered_map<std::string, ScriptFieldValue> ParseFieldData(std::string_view fieldData)
        {
            std::unordered_map<std::string, ScriptFieldValue> fieldValues;

            std::string_view remaining = fieldData;
            while (!remaining.empty())
            {
                const auto lineEnd = remaining.find('\n');
                const std::string_view line = lineEnd == std::string_view::npos
                                                  ? remaining
                                                  : remaining.substr(0, lineEnd);

                if (!line.empty())
                {
                    const auto tokens = SplitEscaped(line, '\t');
                    if (tokens.size() >= 4 && tokens[0] == "FIELD")
                    {
                        const auto fieldType = ParseFieldType(tokens[2]);
                        fieldValues[tokens[1]] = ParseFieldValue(fieldType, tokens[3]);
                    }
                }

                if (lineEnd == std::string_view::npos)
                {
                    break;
                }

                remaining.remove_prefix(lineEnd + 1);
            }

            return fieldValues;
        }

        scene::Entity *FindEntity(uint32_t entityId)
        {
            auto *scene = core::Engine::GetInstance().GetScene();
            if (!scene)
            {
                return nullptr;
            }

            return scene->FindEntityByID(entityId);
        }

        scene::Component *FindComponent(uint32_t entityId, ManagedComponentKind componentKind)
        {
            auto *entity = FindEntity(entityId);
            if (!entity)
            {
                return nullptr;
            }

            switch (componentKind)
            {
            case ManagedComponentKind::Mesh:
                return entity->GetComponent<scene::MeshComponent>();
            case ManagedComponentKind::Camera:
                return entity->GetComponent<scene::CameraComponent>();
            case ManagedComponentKind::Light:
                return entity->GetComponent<scene::LightComponent>();
            case ManagedComponentKind::Script:
                return entity->GetComponent<scene::ScriptComponent>();
            case ManagedComponentKind::Rigidbody:
                return entity->GetComponent<scene::RigidbodyComponent>();
            case ManagedComponentKind::Collider:
                return entity->GetComponent<scene::ColliderComponent>();
            case ManagedComponentKind::Animation:
                return entity->GetComponent<scene::AnimationComponent>();
            case ManagedComponentKind::Canvas:
                return entity->GetComponent<scene::CanvasComponent>();
            case ManagedComponentKind::RectTransform:
                return entity->GetComponent<scene::RectTransformComponent>();
            case ManagedComponentKind::UIImage:
                return entity->GetComponent<scene::UIImageComponent>();
            case ManagedComponentKind::UIText:
                return entity->GetComponent<scene::UITextComponent>();
            case ManagedComponentKind::UIButton:
                return entity->GetComponent<scene::UIButtonComponent>();
            case ManagedComponentKind::ParticleSystem:
                return entity->GetComponent<scene::ParticleSystemComponent>();
            default:
                return nullptr;
            }
        }

        const platform::InputState &GetInputState()
        {
            return core::Engine::GetInstance().GetWindow().GetInputState();
        }

        bool IsScriptInputEnabled()
        {
            return core::Engine::GetInstance().GetWindow().IsScriptInputEnabled();
        }

        bool IsFiniteVector3(NativeVector3 value)
        {
            return std::isfinite(value.x) &&
                   std::isfinite(value.y) &&
                   std::isfinite(value.z);
        }

        NativeVector3 GetEntityPosition(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            if (!entity)
            {
                return {};
            }

            const auto position = entity->GetPosition();
            return NativeVector3{position.x, position.y, position.z};
        }

        NativeVector3 GetEntityWorldPosition(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            if (!entity)
            {
                return {};
            }

            const auto position = entity->GetWorldPosition();
            return NativeVector3{position.x, position.y, position.z};
        }

        void SetEntityPosition(uint32_t entityId, NativeVector3 position)
        {
            auto *entity = FindEntity(entityId);
            if (!entity || !IsFiniteVector3(position))
            {
                return;
            }

            entity->SetPosition(glm::vec3(position.x, position.y, position.z));
        }

        NativeVector3 GetEntityRotation(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            if (!entity)
            {
                return {};
            }

            const auto rotation = entity->GetRotation();
            return NativeVector3{rotation.x, rotation.y, rotation.z};
        }

        void SetEntityRotation(uint32_t entityId, NativeVector3 rotation)
        {
            auto *entity = FindEntity(entityId);
            if (!entity || !IsFiniteVector3(rotation))
            {
                return;
            }

            entity->SetRotation(glm::vec3(rotation.x, rotation.y, rotation.z));
        }

        NativeVector3 GetEntityScale(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            if (!entity)
            {
                return {};
            }

            const auto scale = entity->GetScale();
            return NativeVector3{scale.x, scale.y, scale.z};
        }

        void SetEntityScale(uint32_t entityId, NativeVector3 scale)
        {
            auto *entity = FindEntity(entityId);
            if (!entity || !IsFiniteVector3(scale))
            {
                return;
            }

            entity->SetScale(glm::vec3(scale.x, scale.y, scale.z));
        }

        NativeVector3 GetEntityForward(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            if (!entity)
            {
                return NativeVector3{0.0f, 0.0f, -1.0f};
            }

            const glm::mat4 worldTransform = entity->GetWorldTransform();
            const glm::vec3 forward = glm::normalize(-glm::vec3(worldTransform[2]));
            return NativeVector3{forward.x, forward.y, forward.z};
        }

        NativeVector3 GetEntityRight(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            if (!entity)
            {
                return NativeVector3{1.0f, 0.0f, 0.0f};
            }

            const glm::mat4 worldTransform = entity->GetWorldTransform();
            const glm::vec3 right = glm::normalize(glm::vec3(worldTransform[0]));
            return NativeVector3{right.x, right.y, right.z};
        }

        int32_t GetEntityActive(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            return entity && entity->IsSelfActive() ? 1 : 0;
        }

        void SetEntityActive(uint32_t entityId, int32_t active)
        {
            auto *entity = FindEntity(entityId);
            if (!entity)
            {
                return;
            }

            entity->SetActive(active != 0);
        }

        int32_t GetEntityTagCount(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            return entity ? static_cast<int32_t>(entity->GetTags().size()) : 0;
        }

        const char *GetEntityTag(uint32_t entityId, int32_t tagIndex)
        {
            thread_local std::string tagStorage;
            tagStorage.clear();

            auto *entity = FindEntity(entityId);
            if (!entity || tagIndex < 0 || tagIndex >= static_cast<int32_t>(entity->GetTags().size()))
            {
                return tagStorage.c_str();
            }

            tagStorage = entity->GetTags()[static_cast<std::size_t>(tagIndex)];
            return tagStorage.c_str();
        }

        int32_t DestroyEntity(uint32_t entityId)
        {
            auto *scene = core::Engine::GetInstance().GetScene();
            return scene && scene->DestroyEntity(entityId) ? 1 : 0;
        }

        const char *GetEntityName(uint32_t entityId)
        {
            thread_local std::string nameStorage;
            auto *entity = FindEntity(entityId);
            nameStorage = entity ? entity->GetName() : std::string{};
            return nameStorage.c_str();
        }

        uint32_t FindEntityByName(const char *name)
        {
            auto *activeScene = core::Engine::GetInstance().GetScene();
            auto *entity = activeScene && name ? activeScene->FindEntityByName(name) : nullptr;
            return entity ? entity->GetID() : 0;
        }

        int32_t GetEntityCountByTag(const char *tag)
        {
            auto *activeScene = core::Engine::GetInstance().GetScene();
            return activeScene && tag ? static_cast<int32_t>(activeScene->FindEntitiesByTag(tag).size()) : 0;
        }

        uint32_t GetEntityByTag(const char *tag, int32_t index)
        {
            auto *activeScene = core::Engine::GetInstance().GetScene();
            if (!activeScene || !tag || index < 0)
            {
                return 0;
            }

            const auto entities = activeScene->FindEntitiesByTag(tag);
            return index < static_cast<int32_t>(entities.size()) && entities[static_cast<std::size_t>(index)]
                       ? entities[static_cast<std::size_t>(index)]->GetID()
                       : 0;
        }

        uint32_t InstantiatePrefab(const char *prefabReference)
        {
            auto *activeScene = core::Engine::GetInstance().GetScene();
            if (!activeScene || !prefabReference || prefabReference[0] == '\0')
            {
                return 0;
            }

            std::string errorMessage;
            auto *instance = scene::Prefab::Instantiate(*activeScene, prefabReference, nullptr, &errorMessage);
            if (!instance)
            {
                if (!errorMessage.empty())
                {
                    DispatchScriptLog(ScriptLogSeverity::Warning,
                                      "Failed to instantiate prefab '" + std::string(prefabReference) + "': " + errorMessage);
                }
                return 0;
            }

            return instance->GetID();
        }

        const char *LoadScriptableObjectAsset(const char *assetReference)
        {
            static thread_local std::string assetData;
            assetData.clear();
            if (!assetReference || *assetReference == '\0')
            {
                return assetData.c_str();
            }

            const auto path = core::Engine::GetInstance().GetAssetManager().ResolveAssetPath(assetReference);
            std::ifstream input(path, std::ios::in | std::ios::binary);
            if (!input.is_open())
            {
                return assetData.c_str();
            }

            std::ostringstream stream;
            stream << input.rdbuf();
            assetData = stream.str();
            return assetData.c_str();
        }

        int32_t HasEntityComponent(uint32_t entityId, int32_t componentKind)
        {
            return FindComponent(entityId, static_cast<ManagedComponentKind>(componentKind)) ? 1 : 0;
        }

        int32_t GetComponentEnabled(uint32_t entityId, int32_t componentKind)
        {
            auto *component = FindComponent(entityId, static_cast<ManagedComponentKind>(componentKind));
            return component && component->IsEnabled() ? 1 : 0;
        }

        void SetComponentEnabled(uint32_t entityId, int32_t componentKind, int32_t enabled)
        {
            auto *component = FindComponent(entityId, static_cast<ManagedComponentKind>(componentKind));
            if (!component)
            {
                return;
            }

            component->SetEnabled(enabled != 0);
        }

        int32_t GetCameraMain(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            auto *cameraComponent = entity ? entity->GetComponent<scene::CameraComponent>() : nullptr;
            return cameraComponent && cameraComponent->IsMainCamera() ? 1 : 0;
        }

        void SetCameraMain(uint32_t entityId, int32_t isMain)
        {
            auto *entity = FindEntity(entityId);
            if (!entity)
            {
                return;
            }

            if (auto *cameraComponent = entity->GetComponent<scene::CameraComponent>())
            {
                cameraComponent->SetMainCamera(isMain != 0);
            }
        }

        float GetCameraFov(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            if (!entity)
            {
                return 0.0f;
            }

            auto *cameraComponent = entity->GetComponent<scene::CameraComponent>();
            return cameraComponent && cameraComponent->GetCamera() ? cameraComponent->GetCamera()->GetFOV() : 0.0f;
        }

        void SetCameraFov(uint32_t entityId, float fov)
        {
            auto *entity = FindEntity(entityId);
            if (!entity)
            {
                return;
            }

            if (auto *cameraComponent = entity->GetComponent<scene::CameraComponent>(); cameraComponent && cameraComponent->GetCamera())
            {
                cameraComponent->GetCamera()->SetFOV(fov);
            }
        }

        float GetLightIntensity(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            if (!entity)
            {
                return 0.0f;
            }

            if (auto *lightComponent = entity->GetComponent<scene::LightComponent>())
            {
                return lightComponent->GetLight().intensity;
            }

            return 0.0f;
        }

        void SetLightIntensity(uint32_t entityId, float intensity)
        {
            auto *entity = FindEntity(entityId);
            if (!entity)
            {
                return;
            }

            if (auto *lightComponent = entity->GetComponent<scene::LightComponent>())
            {
                lightComponent->SetIntensity(intensity);
            }
        }

        NativeVector3 GetLightColor(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            if (!entity)
            {
                return {};
            }

            if (auto *lightComponent = entity->GetComponent<scene::LightComponent>())
            {
                const auto color = lightComponent->GetLight().color;
                return NativeVector3{color.x, color.y, color.z};
            }

            return {};
        }

        void SetLightColor(uint32_t entityId, NativeVector3 color)
        {
            auto *entity = FindEntity(entityId);
            if (!entity)
            {
                return;
            }

            if (auto *lightComponent = entity->GetComponent<scene::LightComponent>())
            {
                lightComponent->SetColor(glm::vec3(color.x, color.y, color.z));
            }
        }

        int32_t GetMeshStatic(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            if (!entity)
            {
                return 0;
            }

            if (auto *meshComponent = entity->GetComponent<scene::MeshComponent>())
            {
                return meshComponent->IsStatic() ? 1 : 0;
            }

            return 0;
        }

        void SetMeshStatic(uint32_t entityId, int32_t isStatic)
        {
            auto *entity = FindEntity(entityId);
            if (!entity)
            {
                return;
            }

            if (auto *meshComponent = entity->GetComponent<scene::MeshComponent>())
            {
                meshComponent->SetStatic(isStatic != 0);
            }
        }

        NativeVector3 GetMeshColor(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            auto *meshComponent = entity ? entity->GetComponent<scene::MeshComponent>() : nullptr;
            auto *material = meshComponent ? meshComponent->GetMaterialForMaterialSlot(0) : nullptr;
            if (!material)
            {
                return NativeVector3{1.0f, 1.0f, 1.0f};
            }

            const auto color = material->GetConfig().color;
            return NativeVector3{color.r, color.g, color.b};
        }

        void SetMeshColor(uint32_t entityId, NativeVector3 color)
        {
            auto *entity = FindEntity(entityId);
            auto *meshComponent = entity ? entity->GetComponent<scene::MeshComponent>() : nullptr;
            if (!meshComponent || !IsFiniteVector3(color))
            {
                return;
            }

            auto *material = meshComponent->GetMaterialForMaterialSlot(0);
            if (!material)
            {
                material = meshComponent->CreateUniqueMaterialForMaterialSlot(0);
            }
            else if (!meshComponent->GetMaterialAssetForMaterialSlot(0).empty())
            {
                material = meshComponent->CreateUniqueMaterialForMaterialSlot(0);
                meshComponent->SetMaterialAssetForMaterialSlot(0, {});
            }

            if (material)
            {
                const auto alpha = material->GetConfig().color.a;
                material->SetColor(glm::vec4(color.x, color.y, color.z, alpha));
            }
        }

        scene::AnimationComponent *FindAnimation(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            return entity ? entity->GetComponent<scene::AnimationComponent>() : nullptr;
        }

        int32_t GetAnimationClipCount(uint32_t entityId)
        {
            auto *component = FindAnimation(entityId);
            return component ? component->GetClipCount() : 0;
        }

        int32_t GetAnimationClipIndex(uint32_t entityId)
        {
            auto *component = FindAnimation(entityId);
            return component ? component->GetCurrentClipIndex() : 0;
        }

        void SetAnimationClipIndex(uint32_t entityId, int32_t clipIndex)
        {
            if (auto *component = FindAnimation(entityId))
            {
                component->SetCurrentClipIndex(clipIndex);
            }
        }

        const char *GetAnimationClipName(uint32_t entityId, int32_t clipIndex)
        {
            thread_local std::string clipNameStorage;
            clipNameStorage.clear();

            auto *component = FindAnimation(entityId);
            if (!component || clipIndex < 0 || clipIndex >= component->GetClipCount())
            {
                return clipNameStorage.c_str();
            }

            clipNameStorage = component->GetClips()[static_cast<size_t>(clipIndex)].name;
            return clipNameStorage.c_str();
        }

        float GetAnimationClipDuration(uint32_t entityId, int32_t clipIndex)
        {
            auto *component = FindAnimation(entityId);
            if (!component || clipIndex < 0 || clipIndex >= component->GetClipCount())
            {
                return 0.0f;
            }

            return component->GetClips()[static_cast<size_t>(clipIndex)].duration;
        }

        int32_t GetAnimationPlaying(uint32_t entityId)
        {
            auto *component = FindAnimation(entityId);
            return component && component->IsPlaying() ? 1 : 0;
        }
        void SetAnimationPlaying(uint32_t entityId, int32_t value)
        {
            if (auto *component = FindAnimation(entityId))
                component->SetPlaying(value != 0);
        }
        int32_t GetAnimationLooping(uint32_t entityId)
        {
            auto *component = FindAnimation(entityId);
            return component && component->IsLooping() ? 1 : 0;
        }
        void SetAnimationLooping(uint32_t entityId, int32_t value)
        {
            if (auto *component = FindAnimation(entityId))
                component->SetLooping(value != 0);
        }
        int32_t GetAnimationAutoplay(uint32_t entityId)
        {
            auto *component = FindAnimation(entityId);
            return component && component->IsAutoplay() ? 1 : 0;
        }
        void SetAnimationAutoplay(uint32_t entityId, int32_t value)
        {
            if (auto *component = FindAnimation(entityId))
                component->SetAutoplay(value != 0);
        }
        float GetAnimationSpeed(uint32_t entityId)
        {
            auto *component = FindAnimation(entityId);
            return component ? component->GetSpeed() : 0.0f;
        }
        void SetAnimationSpeed(uint32_t entityId, float value)
        {
            if (auto *component = FindAnimation(entityId))
                component->SetSpeed(value);
        }
        float GetAnimationTime(uint32_t entityId)
        {
            auto *component = FindAnimation(entityId);
            return component ? component->GetTime() : 0.0f;
        }
        void SetAnimationTime(uint32_t entityId, float value)
        {
            if (auto *component = FindAnimation(entityId))
                component->SetTime(value);
        }
        void AnimationPlay(uint32_t entityId)
        {
            if (auto *component = FindAnimation(entityId))
                component->Play();
        }
        void AnimationPause(uint32_t entityId)
        {
            if (auto *component = FindAnimation(entityId))
                component->Pause();
        }
        void AnimationStop(uint32_t entityId)
        {
            if (auto *component = FindAnimation(entityId))
                component->Stop();
        }
        void SetAnimationBoolParameter(uint32_t entityId, const char *name, int32_t value)
        {
            if (auto *component = FindAnimation(entityId); component && name)
            {
                component->SetBool(name, value != 0);
            }
        }
        void SetAnimationFloatParameter(uint32_t entityId, const char *name, float value)
        {
            if (auto *component = FindAnimation(entityId); component && name)
            {
                component->SetFloat(name, value);
            }
        }
        void SetAnimationIntParameter(uint32_t entityId, const char *name, int32_t value)
        {
            if (auto *component = FindAnimation(entityId); component && name)
            {
                component->SetInt(name, value);
            }
        }
        void SetAnimationTriggerParameter(uint32_t entityId, const char *name)
        {
            if (auto *component = FindAnimation(entityId); component && name)
            {
                component->SetTrigger(name);
            }
        }
        void ResetAnimationTriggerParameter(uint32_t entityId, const char *name)
        {
            if (auto *component = FindAnimation(entityId); component && name)
            {
                component->ResetTrigger(name);
            }
        }
        void AnimationPlayState(uint32_t entityId, const char *name)
        {
            if (auto *component = FindAnimation(entityId); component && name)
            {
                component->PlayState(name);
            }
        }

        scene::RigidbodyComponent *FindRigidbody(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            return entity ? entity->GetComponent<scene::RigidbodyComponent>() : nullptr;
        }

        scene::ColliderComponent *FindCollider(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            return entity ? entity->GetComponent<scene::ColliderComponent>() : nullptr;
        }

        float GetRigidbodyMass(uint32_t entityId)
        {
            auto *component = FindRigidbody(entityId);
            return component ? component->GetMass() : 0.0f;
        }
        void SetRigidbodyMass(uint32_t entityId, float value)
        {
            if (auto *component = FindRigidbody(entityId))
                component->SetMass(value);
        }
        float GetRigidbodyLinearDrag(uint32_t entityId)
        {
            auto *component = FindRigidbody(entityId);
            return component ? component->GetLinearDrag() : 0.0f;
        }
        void SetRigidbodyLinearDrag(uint32_t entityId, float value)
        {
            if (auto *component = FindRigidbody(entityId))
                component->SetLinearDrag(value);
        }
        float GetRigidbodyAngularDrag(uint32_t entityId)
        {
            auto *component = FindRigidbody(entityId);
            return component ? component->GetAngularDrag() : 0.0f;
        }
        void SetRigidbodyAngularDrag(uint32_t entityId, float value)
        {
            if (auto *component = FindRigidbody(entityId))
                component->SetAngularDrag(value);
        }
        float GetRigidbodyFriction(uint32_t entityId)
        {
            auto *component = FindRigidbody(entityId);
            return component ? component->GetFriction() : 0.0f;
        }
        void SetRigidbodyFriction(uint32_t entityId, float value)
        {
            if (auto *component = FindRigidbody(entityId))
                component->SetFriction(value);
        }
        int32_t GetRigidbodyUseGravity(uint32_t entityId)
        {
            auto *component = FindRigidbody(entityId);
            return component && component->UsesGravity() ? 1 : 0;
        }
        void SetRigidbodyUseGravity(uint32_t entityId, int32_t value)
        {
            if (auto *component = FindRigidbody(entityId))
                component->SetUseGravity(value != 0);
        }
        int32_t GetRigidbodyKinematic(uint32_t entityId)
        {
            auto *component = FindRigidbody(entityId);
            return component && component->IsKinematic() ? 1 : 0;
        }
        void SetRigidbodyKinematic(uint32_t entityId, int32_t value)
        {
            if (auto *component = FindRigidbody(entityId))
                component->SetKinematic(value != 0);
        }
        int32_t GetRigidbodyFreezeRotation(uint32_t entityId)
        {
            auto *component = FindRigidbody(entityId);
            return component && component->HasFreezeRotation() ? 1 : 0;
        }
        void SetRigidbodyFreezeRotation(uint32_t entityId, int32_t value)
        {
            if (auto *component = FindRigidbody(entityId))
                component->SetFreezeRotation(value != 0);
        }
        NativeVector3 GetRigidbodyVelocity(uint32_t entityId)
        {
            auto *component = FindRigidbody(entityId);
            const auto value = component ? component->GetVelocity() : glm::vec3{0.0f};
            return NativeVector3{value.x, value.y, value.z};
        }
        void SetRigidbodyVelocity(uint32_t entityId, NativeVector3 value)
        {
            if (auto *component = FindRigidbody(entityId); component && IsFiniteVector3(value))
            {
                component->SetVelocity(glm::vec3(value.x, value.y, value.z));
            }
        }
        NativeVector3 GetRigidbodyAngularVelocity(uint32_t entityId)
        {
            auto *component = FindRigidbody(entityId);
            const auto value = component ? component->GetAngularVelocity() : glm::vec3{0.0f};
            return NativeVector3{value.x, value.y, value.z};
        }
        void SetRigidbodyAngularVelocity(uint32_t entityId, NativeVector3 value)
        {
            if (auto *component = FindRigidbody(entityId); component && IsFiniteVector3(value))
            {
                component->SetAngularVelocity(glm::vec3(value.x, value.y, value.z));
            }
        }

        NativeVector3 GetMeshEmission(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            auto *meshComponent = entity ? entity->GetComponent<scene::MeshComponent>() : nullptr;
            auto *material = meshComponent ? meshComponent->GetMaterialForMaterialSlot(0) : nullptr;
            if (!material)
            {
                return {};
            }

            const auto emission = material->GetConfig().emission;
            return NativeVector3{emission.r, emission.g, emission.b};
        }

        void SetMeshEmission(uint32_t entityId, NativeVector3 emission)
        {
            auto *entity = FindEntity(entityId);
            auto *meshComponent = entity ? entity->GetComponent<scene::MeshComponent>() : nullptr;
            if (!meshComponent || !IsFiniteVector3(emission))
            {
                return;
            }

            auto *material = meshComponent->GetMaterialForMaterialSlot(0);
            if (!material)
            {
                material = meshComponent->CreateUniqueMaterialForMaterialSlot(0);
            }
            else if (!meshComponent->GetMaterialAssetForMaterialSlot(0).empty())
            {
                material = meshComponent->CreateUniqueMaterialForMaterialSlot(0);
                meshComponent->SetMaterialAssetForMaterialSlot(0, {});
            }

            if (material)
            {
                material->SetEmission(glm::vec3(
                    emission.x > 0.0f ? emission.x : 0.0f,
                    emission.y > 0.0f ? emission.y : 0.0f,
                    emission.z > 0.0f ? emission.z : 0.0f));
            }
        }
        void AddRigidbodyForce(uint32_t entityId, NativeVector3 value)
        {
            if (IsFiniteVector3(value))
            {
                if (auto *activeScene = core::Engine::GetInstance().GetScene())
                {
                    activeScene->AddRigidbodyForce(entityId, glm::vec3(value.x, value.y, value.z), false);
                }
            }
        }
        void AddRigidbodyImpulse(uint32_t entityId, NativeVector3 value)
        {
            if (IsFiniteVector3(value))
            {
                if (auto *activeScene = core::Engine::GetInstance().GetScene())
                {
                    activeScene->AddRigidbodyForce(entityId, glm::vec3(value.x, value.y, value.z), true);
                }
            }
        }

        int32_t GetColliderShape(uint32_t entityId)
        {
            auto *component = FindCollider(entityId);
            return component ? static_cast<int32_t>(component->GetShape()) : 0;
        }
        void SetColliderShape(uint32_t entityId, int32_t value)
        {
            if (auto *component = FindCollider(entityId))
            {
                component->SetShape(static_cast<scene::ColliderShape>(std::clamp(value, 0, 2)));
            }
        }
        NativeVector3 GetColliderCenter(uint32_t entityId)
        {
            auto *component = FindCollider(entityId);
            const auto value = component ? component->GetCenter() : glm::vec3{0.0f};
            return NativeVector3{value.x, value.y, value.z};
        }
        void SetColliderCenter(uint32_t entityId, NativeVector3 value)
        {
            if (auto *component = FindCollider(entityId); component && IsFiniteVector3(value))
            {
                component->SetCenter(glm::vec3(value.x, value.y, value.z));
            }
        }
        NativeVector3 GetColliderSize(uint32_t entityId)
        {
            auto *component = FindCollider(entityId);
            const auto value = component ? component->GetSize() : glm::vec3{1.0f};
            return NativeVector3{value.x, value.y, value.z};
        }
        void SetColliderSize(uint32_t entityId, NativeVector3 value)
        {
            if (auto *component = FindCollider(entityId); component && IsFiniteVector3(value))
            {
                component->SetSize(glm::vec3(value.x, value.y, value.z));
            }
        }
        float GetColliderRadius(uint32_t entityId)
        {
            auto *component = FindCollider(entityId);
            return component ? component->GetRadius() : 0.0f;
        }
        void SetColliderRadius(uint32_t entityId, float value)
        {
            if (auto *component = FindCollider(entityId))
                component->SetRadius(value);
        }
        float GetColliderHeight(uint32_t entityId)
        {
            auto *component = FindCollider(entityId);
            return component ? component->GetHeight() : 0.0f;
        }
        void SetColliderHeight(uint32_t entityId, float value)
        {
            if (auto *component = FindCollider(entityId))
                component->SetHeight(value);
        }
        int32_t GetColliderTrigger(uint32_t entityId)
        {
            auto *component = FindCollider(entityId);
            return component && component->IsTrigger() ? 1 : 0;
        }
        void SetColliderTrigger(uint32_t entityId, int32_t value)
        {
            if (auto *component = FindCollider(entityId))
                component->SetTrigger(value != 0);
        }

        scene::ParticleSystemComponent *FindParticleSystem(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            return entity ? entity->GetComponent<scene::ParticleSystemComponent>() : nullptr;
        }

        int32_t GetParticleSystemPlaying(uint32_t entityId)
        {
            auto *component = FindParticleSystem(entityId);
            return component && component->IsPlaying() ? 1 : 0;
        }
        int32_t GetParticleSystemParticleCount(uint32_t entityId)
        {
            auto *component = FindParticleSystem(entityId);
            return component ? component->GetParticleCount() : 0;
        }
        void ParticleSystemPlay(uint32_t entityId)
        {
            if (auto *component = FindParticleSystem(entityId))
                component->Play();
        }
        void ParticleSystemPause(uint32_t entityId)
        {
            if (auto *component = FindParticleSystem(entityId))
                component->Pause();
        }
        void ParticleSystemStop(uint32_t entityId, int32_t clear)
        {
            if (auto *component = FindParticleSystem(entityId))
                component->Stop(clear != 0);
        }
        void ParticleSystemClear(uint32_t entityId)
        {
            if (auto *component = FindParticleSystem(entityId))
                component->Clear();
        }
        void ParticleSystemEmit(uint32_t entityId, int32_t count)
        {
            if (auto *component = FindParticleSystem(entityId))
                component->Emit(count);
        }
        void ParticleSystemEmitAt(uint32_t entityId, NativeVector3 position, int32_t count)
        {
            if (auto *component = FindParticleSystem(entityId); component && IsFiniteVector3(position))
            {
                component->EmitAt(glm::vec3(position.x, position.y, position.z), count);
            }
        }
        const char *GetParticleSystemAssetReference(uint32_t entityId)
        {
            thread_local std::string assetReferenceStorage;
            assetReferenceStorage.clear();
            if (auto *component = FindParticleSystem(entityId))
            {
                assetReferenceStorage = component->GetParticleSystemAssetReference();
            }
            return assetReferenceStorage.c_str();
        }
        void SetParticleSystemAssetReference(uint32_t entityId, const char *assetReference)
        {
            if (auto *component = FindParticleSystem(entityId))
            {
                component->SetParticleSystemAssetReference(assetReference ? assetReference : "");
            }
        }
        int32_t GetParticleSystemLooping(uint32_t entityId)
        {
            auto *component = FindParticleSystem(entityId);
            return component && component->GetLooping() ? 1 : 0;
        }
        void SetParticleSystemLooping(uint32_t entityId, int32_t value)
        {
            if (auto *component = FindParticleSystem(entityId))
                component->SetLooping(value != 0);
        }
        int32_t GetParticleSystemPlayOnAwake(uint32_t entityId)
        {
            auto *component = FindParticleSystem(entityId);
            return component && component->GetPlayOnAwake() ? 1 : 0;
        }
        void SetParticleSystemPlayOnAwake(uint32_t entityId, int32_t value)
        {
            if (auto *component = FindParticleSystem(entityId))
                component->SetPlayOnAwake(value != 0);
        }
        float GetParticleSystemDuration(uint32_t entityId)
        {
            auto *component = FindParticleSystem(entityId);
            return component ? component->GetDuration() : 0.0f;
        }
        void SetParticleSystemDuration(uint32_t entityId, float value)
        {
            if (auto *component = FindParticleSystem(entityId))
                component->SetDuration(value);
        }
        float GetParticleSystemStartLifetime(uint32_t entityId)
        {
            auto *component = FindParticleSystem(entityId);
            return component ? component->GetStartLifetime() : 0.0f;
        }
        void SetParticleSystemStartLifetime(uint32_t entityId, float value)
        {
            if (auto *component = FindParticleSystem(entityId))
                component->SetStartLifetime(value);
        }
        float GetParticleSystemStartSpeed(uint32_t entityId)
        {
            auto *component = FindParticleSystem(entityId);
            return component ? component->GetStartSpeed() : 0.0f;
        }
        void SetParticleSystemStartSpeed(uint32_t entityId, float value)
        {
            if (auto *component = FindParticleSystem(entityId))
                component->SetStartSpeed(value);
        }
        float GetParticleSystemStartSize(uint32_t entityId)
        {
            auto *component = FindParticleSystem(entityId);
            return component ? component->GetStartSize() : 0.0f;
        }
        void SetParticleSystemStartSize(uint32_t entityId, float value)
        {
            if (auto *component = FindParticleSystem(entityId))
                component->SetStartSize(value);
        }
        float GetParticleSystemGravityModifier(uint32_t entityId)
        {
            auto *component = FindParticleSystem(entityId);
            return component ? component->GetGravityModifier() : 0.0f;
        }
        void SetParticleSystemGravityModifier(uint32_t entityId, float value)
        {
            if (auto *component = FindParticleSystem(entityId))
                component->SetGravityModifier(value);
        }
        float GetParticleSystemEmissionRate(uint32_t entityId)
        {
            auto *component = FindParticleSystem(entityId);
            return component ? component->GetEmissionRateOverTime() : 0.0f;
        }
        void SetParticleSystemEmissionRate(uint32_t entityId, float value)
        {
            if (auto *component = FindParticleSystem(entityId))
                component->SetEmissionRateOverTime(value);
        }
        NativeVector3 GetParticleSystemStartColor(uint32_t entityId)
        {
            auto *component = FindParticleSystem(entityId);
            const auto value = component ? component->GetStartColor() : glm::vec4{1.0f};
            return NativeVector3{value.r, value.g, value.b};
        }
        void SetParticleSystemStartColor(uint32_t entityId, NativeVector3 value)
        {
            if (auto *component = FindParticleSystem(entityId); component && IsFiniteVector3(value))
            {
                const float alpha = component->GetStartColor().a;
                component->SetStartColor(glm::vec4(value.x, value.y, value.z, alpha));
            }
        }
        NativeVector3 GetParticleSystemShapeSize(uint32_t entityId)
        {
            auto *component = FindParticleSystem(entityId);
            const auto value = component ? component->GetShapeSize() : glm::vec3{1.0f};
            return NativeVector3{value.x, value.y, value.z};
        }
        void SetParticleSystemShapeSize(uint32_t entityId, NativeVector3 value)
        {
            if (auto *component = FindParticleSystem(entityId); component && IsFiniteVector3(value))
            {
                component->SetShapeSize(glm::vec3(value.x, value.y, value.z));
            }
        }
        int32_t GetParticleSystemSimulationSpace(uint32_t entityId)
        {
            auto *component = FindParticleSystem(entityId);
            return component ? static_cast<int32_t>(component->GetSimulationSpace()) : 0;
        }
        void SetParticleSystemSimulationSpace(uint32_t entityId, int32_t value)
        {
            if (auto *component = FindParticleSystem(entityId))
            {
                component->SetSimulationSpace(value == 1 ? scene::ParticleSimulationSpace::World : scene::ParticleSimulationSpace::Local);
            }
        }
        int32_t GetParticleSystemShape(uint32_t entityId)
        {
            auto *component = FindParticleSystem(entityId);
            return component ? static_cast<int32_t>(component->GetShape()) : 0;
        }
        void SetParticleSystemShape(uint32_t entityId, int32_t value)
        {
            if (auto *component = FindParticleSystem(entityId))
            {
                component->SetShape(static_cast<scene::ParticleShape>(std::clamp(value, 0, 3)));
            }
        }

        scene::CanvasComponent *FindCanvas(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            return entity ? entity->GetComponent<scene::CanvasComponent>() : nullptr;
        }
        scene::RectTransformComponent *FindRectTransform(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            return entity ? entity->GetComponent<scene::RectTransformComponent>() : nullptr;
        }
        scene::UIImageComponent *FindUIImage(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            return entity ? entity->GetComponent<scene::UIImageComponent>() : nullptr;
        }
        scene::UITextComponent *FindUIText(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            return entity ? entity->GetComponent<scene::UITextComponent>() : nullptr;
        }
        scene::UIButtonComponent *FindUIButton(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            return entity ? entity->GetComponent<scene::UIButtonComponent>() : nullptr;
        }

        float GetCanvasScaleFactor(uint32_t entityId)
        {
            auto *component = FindCanvas(entityId);
            return component ? component->GetScaleFactor() : 1.0f;
        }
        void SetCanvasScaleFactor(uint32_t entityId, float value)
        {
            if (auto *component = FindCanvas(entityId))
                component->SetScaleFactor(value);
        }
        int32_t GetCanvasSortingOrder(uint32_t entityId)
        {
            auto *component = FindCanvas(entityId);
            return component ? component->GetSortingOrder() : 0;
        }
        void SetCanvasSortingOrder(uint32_t entityId, int32_t value)
        {
            if (auto *component = FindCanvas(entityId))
                component->SetSortingOrder(value);
        }

        NativeVector3 GetRectAnchoredPosition(uint32_t entityId)
        {
            auto *component = FindRectTransform(entityId);
            const auto value = component ? component->GetAnchoredPosition() : glm::vec2{0.0f};
            return NativeVector3{value.x, value.y, 0.0f};
        }
        void SetRectAnchoredPosition(uint32_t entityId, NativeVector3 value)
        {
            if (auto *component = FindRectTransform(entityId); component && std::isfinite(value.x) && std::isfinite(value.y))
            {
                component->SetAnchoredPosition(glm::vec2(value.x, value.y));
            }
        }
        NativeVector3 GetRectSizeDelta(uint32_t entityId)
        {
            auto *component = FindRectTransform(entityId);
            const auto value = component ? component->GetSizeDelta() : glm::vec2{0.0f};
            return NativeVector3{value.x, value.y, 0.0f};
        }
        void SetRectSizeDelta(uint32_t entityId, NativeVector3 value)
        {
            if (auto *component = FindRectTransform(entityId); component && std::isfinite(value.x) && std::isfinite(value.y))
            {
                component->SetSizeDelta(glm::vec2(value.x, value.y));
            }
        }
        int32_t GetRectAnchorPreset(uint32_t entityId)
        {
            auto *component = FindRectTransform(entityId);
            return component ? static_cast<int32_t>(component->GetAnchorPreset()) : 4;
        }
        void SetRectAnchorPreset(uint32_t entityId, int32_t value)
        {
            if (auto *component = FindRectTransform(entityId))
            {
                component->SetAnchorPreset(static_cast<scene::UIAnchorPreset>(std::clamp(value, 0, 9)));
            }
        }

        NativeVector3 GetUIImageColor(uint32_t entityId)
        {
            auto *component = FindUIImage(entityId);
            const auto value = component ? component->GetColor() : glm::vec4{1.0f};
            return NativeVector3{value.r, value.g, value.b};
        }
        void SetUIImageColor(uint32_t entityId, NativeVector3 value)
        {
            if (auto *component = FindUIImage(entityId); component && IsFiniteVector3(value))
            {
                const float alpha = component->GetColor().a;
                component->SetColor(glm::vec4(value.x, value.y, value.z, alpha));
            }
        }
        float GetUIImageAlpha(uint32_t entityId)
        {
            auto *component = FindUIImage(entityId);
            return component ? component->GetColor().a : 1.0f;
        }
        void SetUIImageAlpha(uint32_t entityId, float value)
        {
            if (auto *component = FindUIImage(entityId); component && std::isfinite(value))
            {
                auto color = component->GetColor();
                color.a = value;
                component->SetColor(color);
            }
        }
        const char *GetUIImageTexture(uint32_t entityId)
        {
            thread_local std::string textureStorage;
            auto *component = FindUIImage(entityId);
            textureStorage = component ? component->GetTexturePath() : std::string{};
            return textureStorage.c_str();
        }
        void SetUIImageTexture(uint32_t entityId, const char *value)
        {
            if (auto *component = FindUIImage(entityId))
            {
                component->SetTexturePath(value ? value : "");
            }
        }
        int32_t GetUIImagePreserveAspect(uint32_t entityId)
        {
            auto *component = FindUIImage(entityId);
            return component && component->GetPreserveAspect() ? 1 : 0;
        }
        void SetUIImagePreserveAspect(uint32_t entityId, int32_t value)
        {
            if (auto *component = FindUIImage(entityId))
            {
                component->SetPreserveAspect(value != 0);
            }
        }
        float GetUIImageFillAmount(uint32_t entityId)
        {
            auto *component = FindUIImage(entityId);
            return component ? component->GetFillAmount() : 1.0f;
        }
        void SetUIImageFillAmount(uint32_t entityId, float value)
        {
            if (auto *component = FindUIImage(entityId); component && std::isfinite(value))
            {
                component->SetFillAmount(value);
            }
        }

        const char *GetUIText(uint32_t entityId)
        {
            thread_local std::string textStorage;
            auto *component = FindUIText(entityId);
            textStorage = component ? component->GetText() : std::string{};
            return textStorage.c_str();
        }
        void SetUIText(uint32_t entityId, const char *text)
        {
            if (auto *component = FindUIText(entityId))
            {
                component->SetText(text ? text : "");
            }
        }
        NativeVector3 GetUITextColor(uint32_t entityId)
        {
            auto *component = FindUIText(entityId);
            const auto value = component ? component->GetColor() : glm::vec4{1.0f};
            return NativeVector3{value.r, value.g, value.b};
        }
        void SetUITextColor(uint32_t entityId, NativeVector3 value)
        {
            if (auto *component = FindUIText(entityId); component && IsFiniteVector3(value))
            {
                const float alpha = component->GetColor().a;
                component->SetColor(glm::vec4(value.x, value.y, value.z, alpha));
            }
        }
        float GetUITextFontSize(uint32_t entityId)
        {
            auto *component = FindUIText(entityId);
            return component ? component->GetFontSize() : 0.0f;
        }
        void SetUITextFontSize(uint32_t entityId, float value)
        {
            if (auto *component = FindUIText(entityId))
                component->SetFontSize(value);
        }

        int32_t GetUIButtonInteractable(uint32_t entityId)
        {
            auto *component = FindUIButton(entityId);
            return component && component->IsInteractable() ? 1 : 0;
        }
        void SetUIButtonInteractable(uint32_t entityId, int32_t value)
        {
            if (auto *component = FindUIButton(entityId))
                component->SetInteractable(value != 0);
        }
        int32_t GetUIButtonHovered(uint32_t entityId)
        {
            auto *component = FindUIButton(entityId);
            return component && component->IsHovered() ? 1 : 0;
        }
        int32_t GetUIButtonPressed(uint32_t entityId)
        {
            auto *component = FindUIButton(entityId);
            return component && component->WasPressed() ? 1 : 0;
        }
        int32_t GetUIButtonReleased(uint32_t entityId)
        {
            auto *component = FindUIButton(entityId);
            return component && component->WasReleased() ? 1 : 0;
        }
        int32_t GetUIButtonClicked(uint32_t entityId)
        {
            auto *component = FindUIButton(entityId);
            return component && component->WasClicked() ? 1 : 0;
        }

        int32_t GetKeyDown(int32_t keyCode)
        {
            if (!IsScriptInputEnabled())
            {
                return 0;
            }

            const auto &inputState = GetInputState();
            return keyCode >= 0 && keyCode < static_cast<int32_t>(inputState.keys.size()) && inputState.keys[static_cast<size_t>(keyCode)] ? 1 : 0;
        }

        int32_t GetKeyPressed(int32_t keyCode)
        {
            if (!IsScriptInputEnabled())
            {
                return 0;
            }

            const auto &inputState = GetInputState();
            return keyCode >= 0 && keyCode < static_cast<int32_t>(inputState.keys.size()) &&
                           inputState.keys[static_cast<size_t>(keyCode)] && !inputState.previousKeys[static_cast<size_t>(keyCode)]
                       ? 1
                       : 0;
        }

        int32_t GetKeyReleased(int32_t keyCode)
        {
            if (!IsScriptInputEnabled())
            {
                return 0;
            }

            const auto &inputState = GetInputState();
            return keyCode >= 0 && keyCode < static_cast<int32_t>(inputState.keys.size()) &&
                           !inputState.keys[static_cast<size_t>(keyCode)] && inputState.previousKeys[static_cast<size_t>(keyCode)]
                       ? 1
                       : 0;
        }

        int32_t GetMouseButtonDown(int32_t button)
        {
            if (!IsScriptInputEnabled())
            {
                return 0;
            }

            const auto &inputState = GetInputState();
            return button >= 0 && button < 8 && inputState.mouseState.buttons[button] ? 1 : 0;
        }

        int32_t GetMouseButtonPressed(int32_t button)
        {
            if (!IsScriptInputEnabled())
            {
                return 0;
            }

            const auto &inputState = GetInputState();
            return button >= 0 && button < 8 && inputState.mouseState.buttons[button] && !inputState.mouseState.previousButtons[button] ? 1 : 0;
        }

        int32_t GetMouseButtonReleased(int32_t button)
        {
            if (!IsScriptInputEnabled())
            {
                return 0;
            }

            const auto &inputState = GetInputState();
            return button >= 0 && button < 8 && !inputState.mouseState.buttons[button] && inputState.mouseState.previousButtons[button] ? 1 : 0;
        }

        NativeVector3 GetMousePosition()
        {
            if (!IsScriptInputEnabled())
            {
                return {};
            }

            const auto &inputState = GetInputState();
            return NativeVector3{static_cast<float>(inputState.mouseState.x), static_cast<float>(inputState.mouseState.y), 0.0f};
        }

        NativeVector3 GetMouseDelta()
        {
            if (!IsScriptInputEnabled())
            {
                return {};
            }

            const auto &inputState = GetInputState();
            return NativeVector3{static_cast<float>(inputState.mouseState.deltaX), static_cast<float>(inputState.mouseState.deltaY), 0.0f};
        }

        NativeVector3 GetMouseScrollDelta()
        {
            if (!IsScriptInputEnabled())
            {
                return {};
            }

            const auto &inputState = GetInputState();
            return NativeVector3{static_cast<float>(inputState.mouseState.scrollDeltaX), static_cast<float>(inputState.mouseState.scrollDeltaY), 0.0f};
        }

        int32_t GetQuitRequested()
        {
            return GetInputState().quitRequested ? 1 : 0;
        }

        int32_t GetCursorLocked()
        {
            const auto &window = core::Engine::GetInstance().GetWindow();
            return IsScriptInputEnabled() && (window.IsCursorLocked() || window.IsCursorLockRequested()) ? 1 : 0;
        }

        void SetCursorLocked(int32_t locked)
        {
            core::Engine::GetInstance().GetWindow().SetCursorLocked(locked != 0);
        }

        int32_t PhysicsRaycast(NativeVector3 origin,
                               NativeVector3 direction,
                               float maxDistance,
                               uint32_t ignoredEntityId,
                               NativeRaycastHit *nativeHit)
        {
            if (!nativeHit ||
                !IsFiniteVector3(origin) ||
                !IsFiniteVector3(direction) ||
                !std::isfinite(maxDistance))
            {
                return 0;
            }

            auto *scene = core::Engine::GetInstance().GetScene();
            if (!scene)
            {
                return 0;
            }

            scene::PhysicsRaycastHit hit;
            if (!scene->Raycast(glm::vec3(origin.x, origin.y, origin.z),
                                glm::vec3(direction.x, direction.y, direction.z),
                                maxDistance,
                                hit,
                                ignoredEntityId))
            {
                return 0;
            }

            nativeHit->entityId = hit.entityId;
            nativeHit->point = NativeVector3{hit.point.x, hit.point.y, hit.point.z};
            nativeHit->normal = NativeVector3{hit.normal.x, hit.normal.y, hit.normal.z};
            nativeHit->distance = hit.distance;
            return 1;
        }

        int32_t PhysicsRaycastTagged(NativeVector3 origin,
                                     NativeVector3 direction,
                                     float maxDistance,
                                     uint32_t ignoredEntityId,
                                     const char *tag,
                                     NativeRaycastHit *nativeHit)
        {
            if (!nativeHit ||
                !IsFiniteVector3(origin) ||
                !IsFiniteVector3(direction) ||
                !std::isfinite(maxDistance))
            {
                return 0;
            }

            auto *scene = core::Engine::GetInstance().GetScene();
            if (!scene)
            {
                return 0;
            }

            scene::PhysicsRaycastHit hit;
            if (!scene->RaycastByTag(glm::vec3(origin.x, origin.y, origin.z),
                                     glm::vec3(direction.x, direction.y, direction.z),
                                     maxDistance,
                                     tag ? std::string(tag) : std::string{},
                                     hit,
                                     ignoredEntityId))
            {
                return 0;
            }

            nativeHit->entityId = hit.entityId;
            nativeHit->point = NativeVector3{hit.point.x, hit.point.y, hit.point.z};
            nativeHit->normal = NativeVector3{hit.normal.x, hit.normal.y, hit.normal.z};
            nativeHit->distance = hit.distance;
            return 1;
        }

        NativeVector3 PhysicsMoveKinematic(uint32_t entityId, NativeVector3 displacement, float skinWidth)
        {
            if (!IsFiniteVector3(displacement) || !std::isfinite(skinWidth))
            {
                return {};
            }

            auto *scene = core::Engine::GetInstance().GetScene();
            auto *entity = FindEntity(entityId);
            if (!scene || !entity)
            {
                return {};
            }

            const auto applied = scene->MoveKinematic(*entity, glm::vec3(displacement.x, displacement.y, displacement.z), skinWidth);
            return NativeVector3{applied.x, applied.y, applied.z};
        }

        void LogScriptMessage(int32_t severity, const char *message)
        {
            if (!message)
            {
                return;
            }

            ScriptLogSeverity logSeverity = ScriptLogSeverity::Info;
            switch (severity)
            {
            case 1:
                logSeverity = ScriptLogSeverity::Warning;
                break;
            case 2:
                logSeverity = ScriptLogSeverity::Error;
                break;
            case 0:
            default:
                logSeverity = ScriptLogSeverity::Info;
                break;
            }

            DispatchScriptLog(logSeverity, message);
        }
#endif
    }

    struct HostFxrScriptRuntime::Impl
    {
#ifdef _WIN32
        HMODULE hostfxrLibrary = nullptr;
        hostfxr_initialize_for_runtime_config_fn initializeForRuntimeConfig = nullptr;
        hostfxr_get_runtime_delegate_fn getRuntimeDelegate = nullptr;
        hostfxr_close_fn closeHostContext = nullptr;
        load_assembly_and_get_function_pointer_fn loadAssemblyAndGetFunctionPointer = nullptr;

        load_script_assembly_fn loadScriptAssembly = nullptr;
        unload_script_assembly_fn unloadScriptAssembly = nullptr;
        get_marshaled_string_fn getScriptMetadata = nullptr;
        get_field_data_fn getFieldData = nullptr;
        free_marshaled_string_fn freeMarshaledString = nullptr;
        get_marshaled_string_fn getLastError = nullptr;
        create_script_instance_fn createScriptInstance = nullptr;
        destroy_script_instance_fn destroyScriptInstance = nullptr;
        invoke_on_create_fn invokeOnCreate = nullptr;
        invoke_on_update_fn invokeOnUpdate = nullptr;
        invoke_on_collision_fn invokeOnCollisionEnter = nullptr;
        invoke_on_collision_fn invokeOnCollisionExit = nullptr;
        apply_field_data_fn applyFieldData = nullptr;
        set_entity_id_fn setEntityId = nullptr;
        register_game_object_api_fn registerGameObjectApi = nullptr;
        register_prefab_api_fn registerPrefabApi = nullptr;
        register_scriptable_object_api_fn registerScriptableObjectApi = nullptr;
        register_component_api_fn registerComponentApi = nullptr;
        register_camera_component_api_fn registerCameraComponentApi = nullptr;
        register_light_component_api_fn registerLightComponentApi = nullptr;
        register_mesh_component_api_fn registerMeshComponentApi = nullptr;
        register_animation_component_api_fn registerAnimationComponentApi = nullptr;
        register_rigidbody_component_api_fn registerRigidbodyComponentApi = nullptr;
        register_collider_component_api_fn registerColliderComponentApi = nullptr;
        register_particle_system_component_api_fn registerParticleSystemComponentApi = nullptr;
        register_runtime_ui_api_fn registerRuntimeUIApi = nullptr;
        register_input_api_fn registerInputApi = nullptr;
        register_physics_api_fn registerPhysicsApi = nullptr;
        register_debug_api_fn registerDebugApi = nullptr;
        std::filesystem::path bridgeSourceAssemblyPath;
        std::filesystem::path bridgeSourceRuntimeConfigPath;
        std::filesystem::path bridgeAssemblyPath;
        std::filesystem::path runtimeConfigPath;
        std::filesystem::path bridgeShadowDirectory;
        std::filesystem::path shadowManagedDirectory;
        std::filesystem::path shadowAssemblyPath;
#endif

        std::filesystem::path loadedAssemblyPath;
        std::vector<ScriptClassDefinition> scriptClasses;
        std::string lastError;
        bool loaded = false;
    };

    namespace
    {
#ifdef _WIN32
        std::string TakeManagedString(HostFxrScriptRuntime::Impl &impl, get_marshaled_string_fn getter)
        {
            if (!getter || !impl.freeMarshaledString)
            {
                return {};
            }

            const char *managedText = getter();
            if (!managedText)
            {
                return {};
            }

            const std::string result(managedText);
            impl.freeMarshaledString(managedText);
            return result;
        }

        void CleanupShadowCopy(HostFxrScriptRuntime::Impl &impl)
        {
            if (impl.shadowManagedDirectory.empty())
            {
                return;
            }

            std::error_code errorCode;
            std::filesystem::remove_all(impl.shadowManagedDirectory, errorCode);
            impl.shadowManagedDirectory.clear();
            impl.shadowAssemblyPath.clear();
        }

        void CleanupBridgeShadowCopy(HostFxrScriptRuntime::Impl &impl)
        {
            if (!impl.bridgeShadowDirectory.empty())
            {
                std::error_code errorCode;
                std::filesystem::remove_all(impl.bridgeShadowDirectory, errorCode);
            }

            impl.bridgeShadowDirectory.clear();
            impl.bridgeAssemblyPath.clear();
            impl.runtimeConfigPath.clear();
        }

        void ResetManagedBridge(HostFxrScriptRuntime::Impl &impl)
        {
            impl.loadAssemblyAndGetFunctionPointer = nullptr;
            impl.loadScriptAssembly = nullptr;
            impl.unloadScriptAssembly = nullptr;
            impl.getScriptMetadata = nullptr;
            impl.getFieldData = nullptr;
            impl.freeMarshaledString = nullptr;
            impl.getLastError = nullptr;
            impl.createScriptInstance = nullptr;
            impl.destroyScriptInstance = nullptr;
            impl.invokeOnCreate = nullptr;
            impl.invokeOnUpdate = nullptr;
            impl.invokeOnCollisionEnter = nullptr;
            impl.invokeOnCollisionExit = nullptr;
            impl.applyFieldData = nullptr;
            impl.setEntityId = nullptr;
            impl.registerGameObjectApi = nullptr;
            impl.registerPrefabApi = nullptr;
            impl.registerScriptableObjectApi = nullptr;
            impl.registerComponentApi = nullptr;
            impl.registerCameraComponentApi = nullptr;
            impl.registerLightComponentApi = nullptr;
            impl.registerMeshComponentApi = nullptr;
            impl.registerAnimationComponentApi = nullptr;
            impl.registerRigidbodyComponentApi = nullptr;
            impl.registerColliderComponentApi = nullptr;
            impl.registerParticleSystemComponentApi = nullptr;
            impl.registerRuntimeUIApi = nullptr;
            impl.registerInputApi = nullptr;
            impl.registerPhysicsApi = nullptr;
            impl.registerDebugApi = nullptr;
            impl.bridgeSourceAssemblyPath.clear();
            impl.bridgeSourceRuntimeConfigPath.clear();
            CleanupBridgeShadowCopy(impl);
        }

        bool PrepareShadowCopy(HostFxrScriptRuntime::Impl &impl,
                               const std::filesystem::path &assemblyPath,
                               std::filesystem::path &shadowAssemblyPath)
        {
            CleanupShadowCopy(impl);

            std::error_code errorCode;
            const auto tempRoot = std::filesystem::temp_directory_path(errorCode);
            if (errorCode)
            {
                impl.lastError = "Failed to locate a temporary directory for managed script shadow copy.";
                return false;
            }

            const auto sourceDirectory = assemblyPath.parent_path();
            const auto uniqueDirectoryName = assemblyPath.stem().string() + "-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
            const auto shadowDirectory = (tempRoot / "PlutoGE" / "ManagedShadow" / uniqueDirectoryName).lexically_normal();

            std::filesystem::create_directories(shadowDirectory, errorCode);
            if (errorCode)
            {
                impl.lastError = "Failed to create managed script shadow directory: " + shadowDirectory.string();
                return false;
            }

            for (std::filesystem::recursive_directory_iterator iterator(sourceDirectory, std::filesystem::directory_options::skip_permission_denied, errorCode), end;
                 iterator != end;
                 iterator.increment(errorCode))
            {
                if (errorCode)
                {
                    impl.lastError = "Failed to enumerate managed script outputs for shadow copy.";
                    CleanupShadowCopy(impl);
                    return false;
                }

                const auto relativePath = std::filesystem::relative(iterator->path(), sourceDirectory, errorCode);
                if (errorCode)
                {
                    impl.lastError = "Failed to resolve managed shadow-copy path.";
                    CleanupShadowCopy(impl);
                    return false;
                }

                const auto destinationPath = (shadowDirectory / relativePath).lexically_normal();
                if (iterator->is_directory())
                {
                    std::filesystem::create_directories(destinationPath, errorCode);
                }
                else if (iterator->is_regular_file())
                {
                    std::filesystem::create_directories(destinationPath.parent_path(), errorCode);
                    if (!errorCode)
                    {
                        std::filesystem::copy_file(iterator->path(), destinationPath, std::filesystem::copy_options::overwrite_existing, errorCode);
                    }
                }

                if (errorCode)
                {
                    impl.lastError = "Failed to copy managed script outputs into shadow directory.";
                    CleanupShadowCopy(impl);
                    return false;
                }
            }

            shadowAssemblyPath = (shadowDirectory / assemblyPath.filename()).lexically_normal();
            if (!std::filesystem::exists(shadowAssemblyPath))
            {
                impl.lastError = "Managed shadow copy is missing the script assembly: " + shadowAssemblyPath.string();
                CleanupShadowCopy(impl);
                return false;
            }

            impl.shadowManagedDirectory = shadowDirectory;
            impl.shadowAssemblyPath = shadowAssemblyPath;
            return true;
        }

        bool PrepareBridgeShadowCopy(HostFxrScriptRuntime::Impl &impl,
                                     const std::filesystem::path &assemblyPath,
                                     const std::filesystem::path &runtimeConfigPath)
        {
            CleanupBridgeShadowCopy(impl);

            std::error_code errorCode;
            const auto tempRoot = std::filesystem::temp_directory_path(errorCode);
            if (errorCode)
            {
                impl.lastError = "Failed to locate a temporary directory for the managed bridge shadow copy.";
                return false;
            }

            const auto sourceDirectory = assemblyPath.parent_path();
            const auto uniqueDirectoryName = assemblyPath.stem().string() + "-bridge-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
            const auto shadowDirectory = (tempRoot / "PlutoGE" / "ManagedShadow" / uniqueDirectoryName).lexically_normal();

            std::filesystem::create_directories(shadowDirectory, errorCode);
            if (errorCode)
            {
                impl.lastError = "Failed to create managed bridge shadow directory: " + shadowDirectory.string();
                return false;
            }

            for (std::filesystem::recursive_directory_iterator iterator(sourceDirectory, std::filesystem::directory_options::skip_permission_denied, errorCode), end;
                 iterator != end;
                 iterator.increment(errorCode))
            {
                if (errorCode)
                {
                    impl.lastError = "Failed to enumerate managed bridge outputs for shadow copy.";
                    CleanupBridgeShadowCopy(impl);
                    return false;
                }

                const auto relativePath = std::filesystem::relative(iterator->path(), sourceDirectory, errorCode);
                if (errorCode)
                {
                    impl.lastError = "Failed to resolve managed bridge shadow-copy path.";
                    CleanupBridgeShadowCopy(impl);
                    return false;
                }

                const auto destinationPath = (shadowDirectory / relativePath).lexically_normal();
                if (iterator->is_directory())
                {
                    std::filesystem::create_directories(destinationPath, errorCode);
                }
                else if (iterator->is_regular_file())
                {
                    std::filesystem::create_directories(destinationPath.parent_path(), errorCode);
                    if (!errorCode)
                    {
                        std::filesystem::copy_file(iterator->path(), destinationPath, std::filesystem::copy_options::overwrite_existing, errorCode);
                    }
                }

                if (errorCode)
                {
                    impl.lastError = "Failed to copy managed bridge outputs into the shadow directory.";
                    CleanupBridgeShadowCopy(impl);
                    return false;
                }
            }

            impl.bridgeShadowDirectory = shadowDirectory;
            impl.bridgeAssemblyPath = (shadowDirectory / assemblyPath.filename()).lexically_normal();
            impl.runtimeConfigPath = (shadowDirectory / runtimeConfigPath.filename()).lexically_normal();
            if (!std::filesystem::exists(impl.bridgeAssemblyPath))
            {
                impl.lastError = "Managed bridge shadow copy is missing the bridge assembly: " + impl.bridgeAssemblyPath.string();
                CleanupBridgeShadowCopy(impl);
                return false;
            }

            if (!std::filesystem::exists(impl.runtimeConfigPath))
            {
                impl.lastError = "Managed bridge shadow copy is missing the runtime config: " + impl.runtimeConfigPath.string();
                CleanupBridgeShadowCopy(impl);
                return false;
            }

            return true;
        }

        template <typename DelegateType>
        bool LoadManagedExport(HostFxrScriptRuntime::Impl &impl, const wchar_t *methodName, DelegateType &delegate)
        {
            void *functionPointer = nullptr;
            const int result = impl.loadAssemblyAndGetFunctionPointer(
                impl.bridgeAssemblyPath.c_str(),
                kScriptBridgeType.data(),
                methodName,
                GetUnmanagedCallersOnlyMethodMarker(),
                nullptr,
                &functionPointer);

            if (result != 0 || !functionPointer)
            {
                impl.lastError = "Failed to load managed bridge export '" + WideToUtf8(methodName) +
                                 "' from " + WideToUtf8(impl.bridgeAssemblyPath.wstring()) +
                                 " (hostfxr result " + std::to_string(result) + ")";
                return false;
            }

            delegate = reinterpret_cast<DelegateType>(functionPointer);
            return true;
        }

        bool EnsureHostFxrLoaded(HostFxrScriptRuntime::Impl &impl)
        {
            if (impl.loadAssemblyAndGetFunctionPointer)
            {
                return true;
            }

            const auto hostFxrPath = FindHostFxrLibrary();
            if (!hostFxrPath)
            {
                impl.lastError = "Failed to locate hostfxr.dll";
                return false;
            }

            impl.hostfxrLibrary = LoadLibraryW(hostFxrPath->c_str());
            if (!impl.hostfxrLibrary)
            {
                impl.lastError = "Failed to load hostfxr.dll";
                return false;
            }

            impl.initializeForRuntimeConfig = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(GetProcAddress(impl.hostfxrLibrary, "hostfxr_initialize_for_runtime_config"));
            impl.getRuntimeDelegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(GetProcAddress(impl.hostfxrLibrary, "hostfxr_get_runtime_delegate"));
            impl.closeHostContext = reinterpret_cast<hostfxr_close_fn>(GetProcAddress(impl.hostfxrLibrary, "hostfxr_close"));

            if (!impl.initializeForRuntimeConfig || !impl.getRuntimeDelegate || !impl.closeHostContext)
            {
                impl.lastError = "hostfxr exports were not available";
                return false;
            }

            return true;
        }

        bool EnsureManagedBridgeLoaded(HostFxrScriptRuntime::Impl &impl, const std::filesystem::path &assemblyPath)
        {
            if (!EnsureHostFxrLoaded(impl))
            {
                return false;
            }

            const auto scriptCorePaths = FindScriptCorePaths(assemblyPath);
            if (!scriptCorePaths)
            {
                impl.lastError = "Failed to locate PlutoGE.ScriptCore.dll and runtimeconfig.json";
                return false;
            }

            const auto &[bridgeAssemblyPath, runtimeConfigPath] = *scriptCorePaths;
            if (impl.loadAssemblyAndGetFunctionPointer &&
                impl.bridgeSourceAssemblyPath == bridgeAssemblyPath &&
                impl.bridgeSourceRuntimeConfigPath == runtimeConfigPath)
            {
                return true;
            }

            if (!PrepareBridgeShadowCopy(impl, bridgeAssemblyPath, runtimeConfigPath))
            {
                return false;
            }

            impl.bridgeSourceAssemblyPath = bridgeAssemblyPath;
            impl.bridgeSourceRuntimeConfigPath = runtimeConfigPath;

            hostfxr_handle hostContext = nullptr;
            const int initializeResult = impl.initializeForRuntimeConfig(impl.runtimeConfigPath.c_str(), nullptr, &hostContext);
            if (initializeResult < 0)
            {
                impl.lastError = "hostfxr failed to initialize the managed runtime (status " + std::to_string(initializeResult) + ")";
                return false;
            }

            if (!hostContext)
            {
                impl.lastError = "hostfxr initialized without a host context (status " + std::to_string(initializeResult) + ")";
                return false;
            }

            void *loadAssemblyDelegate = nullptr;
            const int delegateResult = impl.getRuntimeDelegate(hostContext, hdt_load_assembly_and_get_function_pointer, &loadAssemblyDelegate);
            impl.closeHostContext(hostContext);

            if (delegateResult != 0 || !loadAssemblyDelegate)
            {
                impl.lastError = "hostfxr failed to retrieve load_assembly_and_get_function_pointer";
                return false;
            }

            impl.loadAssemblyAndGetFunctionPointer = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(loadAssemblyDelegate);

            const bool requiredExportsLoaded =
                LoadManagedExport(impl, L"LoadScriptAssembly", impl.loadScriptAssembly) &&
                LoadManagedExport(impl, L"GetScriptMetadata", impl.getScriptMetadata) &&
                LoadManagedExport(impl, L"GetFieldData", impl.getFieldData) &&
                LoadManagedExport(impl, L"FreeNativeString", impl.freeMarshaledString) &&
                LoadManagedExport(impl, L"GetLastError", impl.getLastError) &&
                LoadManagedExport(impl, L"CreateScriptInstance", impl.createScriptInstance) &&
                LoadManagedExport(impl, L"DestroyScriptInstance", impl.destroyScriptInstance) &&
                LoadManagedExport(impl, L"InvokeOnCreate", impl.invokeOnCreate) &&
                LoadManagedExport(impl, L"InvokeOnUpdate", impl.invokeOnUpdate) &&
                LoadManagedExport(impl, L"InvokeOnCollisionEnter", impl.invokeOnCollisionEnter) &&
                LoadManagedExport(impl, L"InvokeOnCollisionExit", impl.invokeOnCollisionExit) &&
                LoadManagedExport(impl, L"ApplyFieldData", impl.applyFieldData) &&
                LoadManagedExport(impl, L"SetEntityId", impl.setEntityId) &&
                LoadManagedExport(impl, L"RegisterGameObjectApi", impl.registerGameObjectApi) &&
                LoadManagedExport(impl, L"RegisterPrefabApi", impl.registerPrefabApi) &&
                LoadManagedExport(impl, L"RegisterScriptableObjectApi", impl.registerScriptableObjectApi) &&
                LoadManagedExport(impl, L"RegisterComponentApi", impl.registerComponentApi) &&
                LoadManagedExport(impl, L"RegisterCameraComponentApi", impl.registerCameraComponentApi) &&
                LoadManagedExport(impl, L"RegisterLightComponentApi", impl.registerLightComponentApi) &&
                LoadManagedExport(impl, L"RegisterMeshComponentApi", impl.registerMeshComponentApi) &&
                LoadManagedExport(impl, L"RegisterAnimationComponentApi", impl.registerAnimationComponentApi) &&
                LoadManagedExport(impl, L"RegisterRigidbodyComponentApi", impl.registerRigidbodyComponentApi) &&
                LoadManagedExport(impl, L"RegisterColliderComponentApi", impl.registerColliderComponentApi) &&
                LoadManagedExport(impl, L"RegisterParticleSystemComponentApi", impl.registerParticleSystemComponentApi) &&
                LoadManagedExport(impl, L"RegisterRuntimeUIApi", impl.registerRuntimeUIApi) &&
                LoadManagedExport(impl, L"RegisterInputApi", impl.registerInputApi) &&
                LoadManagedExport(impl, L"RegisterPhysicsApi", impl.registerPhysicsApi) &&
                LoadManagedExport(impl, L"RegisterDebugApi", impl.registerDebugApi);

            if (!requiredExportsLoaded)
            {
                const std::string exportError = impl.lastError;
                ResetManagedBridge(impl);
                impl.lastError = exportError;
                return false;
            }

            if (!LoadManagedExport(impl, L"UnloadScriptAssembly", impl.unloadScriptAssembly))
            {
                impl.unloadScriptAssembly = nullptr;
                impl.lastError.clear();
            }

            return true;
        }

        class ManagedScriptInstance final : public ScriptInstance
        {
        public:
            ManagedScriptInstance(std::shared_ptr<HostFxrScriptRuntime::Impl> impl,
                                  int64_t instanceHandle,
                                  ScriptClassDefinition scriptClass)
                : m_impl(std::move(impl)), m_instanceHandle(instanceHandle), m_scriptClass(std::move(scriptClass))
            {
            }

            [[nodiscard]] std::optional<ScriptFieldValue> GetFieldValue(std::string_view fieldName) const override
            {
                if (!m_impl || !m_impl->getFieldData || !m_impl->freeMarshaledString)
                {
                    return ScriptInstance::GetFieldValue(fieldName);
                }

                const char *managedText = m_impl->getFieldData(m_instanceHandle);
                if (!managedText)
                {
                    return ScriptInstance::GetFieldValue(fieldName);
                }

                const std::string wireData(managedText);
                m_impl->freeMarshaledString(managedText);

                const auto fieldValues = ParseFieldData(wireData);
                const auto iterator = fieldValues.find(std::string(fieldName));
                if (iterator == fieldValues.end())
                {
                    return ScriptInstance::GetFieldValue(fieldName);
                }

                return iterator->second;
            }

            [[nodiscard]] std::unordered_map<std::string, ScriptFieldValue> GetFieldValuesSnapshot() const override
            {
                if (!m_impl || !m_impl->getFieldData || !m_impl->freeMarshaledString)
                {
                    return ScriptInstance::GetFieldValuesSnapshot();
                }

                const char *managedText = m_impl->getFieldData(m_instanceHandle);
                if (!managedText)
                {
                    return ScriptInstance::GetFieldValuesSnapshot();
                }

                const std::string wireData(managedText);
                m_impl->freeMarshaledString(managedText);
                return ParseFieldData(wireData);
            }

            ~ManagedScriptInstance() override
            {
                if (m_impl && m_instanceHandle != 0 && m_impl->destroyScriptInstance)
                {
                    m_impl->destroyScriptInstance(m_instanceHandle);
                }
            }

            void OnCreate() override
            {
                if (m_impl && m_impl->invokeOnCreate)
                {
                    m_impl->invokeOnCreate(m_instanceHandle);
                }
            }

            void OnUpdate(float deltaTime) override
            {
                if (m_impl && m_impl->invokeOnUpdate)
                {
                    m_impl->invokeOnUpdate(m_instanceHandle, deltaTime);
                }
            }

            void OnCollisionEnter(uint32_t otherEntityId) override
            {
                if (m_impl && m_impl->invokeOnCollisionEnter)
                {
                    m_impl->invokeOnCollisionEnter(m_instanceHandle, otherEntityId);
                }
            }

            void OnCollisionExit(uint32_t otherEntityId) override
            {
                if (m_impl && m_impl->invokeOnCollisionExit)
                {
                    m_impl->invokeOnCollisionExit(m_instanceHandle, otherEntityId);
                }
            }

            void ApplyFieldValues(const std::unordered_map<std::string, ScriptFieldValue> &fieldValues) override
            {
                ScriptInstance::ApplyFieldValues(fieldValues);

                if (!m_impl || !m_impl->applyFieldData)
                {
                    return;
                }

                const std::string wireData = SerializeFieldData(fieldValues, m_scriptClass.fields);
                m_impl->applyFieldData(m_instanceHandle, wireData.c_str());
            }

        protected:
            void OnOwnerAssigned(scene::Entity *owner) override
            {
                if (!m_impl || !m_impl->setEntityId || !owner)
                {
                    return;
                }

                m_impl->setEntityId(m_instanceHandle, owner->GetID());
            }

        private:
            std::shared_ptr<HostFxrScriptRuntime::Impl> m_impl;
            int64_t m_instanceHandle = 0;
            ScriptClassDefinition m_scriptClass;
        };
#endif
    }

    HostFxrScriptRuntime::HostFxrScriptRuntime()
        : m_impl(std::make_shared<Impl>())
    {
    }

    HostFxrScriptRuntime::~HostFxrScriptRuntime()
    {
#ifdef _WIN32
        if (m_impl && m_impl->unloadScriptAssembly && m_impl->loaded)
        {
            m_impl->unloadScriptAssembly();
        }

        if (m_impl)
        {
            CleanupShadowCopy(*m_impl);
            CleanupBridgeShadowCopy(*m_impl);
        }
#endif
    }

    bool HostFxrScriptRuntime::LoadAssembly(const std::filesystem::path &assemblyPath)
    {
#ifdef _WIN32
        m_impl->scriptClasses.clear();
        m_impl->loaded = false;
        m_impl->lastError.clear();

        auto setManagedBridgeFailure = [&](std::string_view stepName)
        {
            m_impl->lastError = TakeManagedString(*m_impl, m_impl->getLastError);
            if (!m_impl->lastError.empty())
            {
                return;
            }

            m_impl->lastError = "Managed bridge call failed at step '" + std::string(stepName) + "'.";
        };

        if (!EnsureManagedBridgeLoaded(*m_impl, assemblyPath))
        {
            return false;
        }

        std::filesystem::path shadowAssemblyPath;
        if (!PrepareShadowCopy(*m_impl, std::filesystem::absolute(assemblyPath), shadowAssemblyPath))
        {
            return false;
        }

        if (!m_impl->registerGameObjectApi ||
            m_impl->registerGameObjectApi(
                reinterpret_cast<void *>(static_cast<get_entity_vector3_fn>(&GetEntityPosition)),
                reinterpret_cast<void *>(static_cast<get_entity_vector3_fn>(&GetEntityWorldPosition)),
                reinterpret_cast<void *>(static_cast<set_entity_vector3_fn>(&SetEntityPosition)),
                reinterpret_cast<void *>(static_cast<get_entity_vector3_fn>(&GetEntityRotation)),
                reinterpret_cast<void *>(static_cast<set_entity_vector3_fn>(&SetEntityRotation)),
                reinterpret_cast<void *>(static_cast<get_entity_vector3_fn>(&GetEntityScale)),
                reinterpret_cast<void *>(static_cast<set_entity_vector3_fn>(&SetEntityScale)),
                reinterpret_cast<void *>(static_cast<get_entity_vector3_fn>(&GetEntityForward)),
                reinterpret_cast<void *>(static_cast<get_entity_vector3_fn>(&GetEntityRight)),
                reinterpret_cast<void *>(static_cast<get_entity_active_fn>(&GetEntityActive)),
                reinterpret_cast<void *>(static_cast<set_entity_active_fn>(&SetEntityActive)),
                reinterpret_cast<void *>(static_cast<get_entity_tag_count_fn>(&GetEntityTagCount)),
                reinterpret_cast<void *>(static_cast<get_entity_tag_fn>(&GetEntityTag)),
                reinterpret_cast<void *>(static_cast<destroy_entity_fn>(&DestroyEntity)),
                reinterpret_cast<void *>(static_cast<get_entity_name_fn>(&GetEntityName)),
                reinterpret_cast<void *>(static_cast<find_entity_by_name_fn>(&FindEntityByName)),
                reinterpret_cast<void *>(static_cast<get_entity_count_by_tag_fn>(&GetEntityCountByTag)),
                reinterpret_cast<void *>(static_cast<get_entity_by_tag_fn>(&GetEntityByTag))) == 0)
        {
            setManagedBridgeFailure("RegisterGameObjectApi");
            return false;
        }

        if (!m_impl->registerPrefabApi ||
            m_impl->registerPrefabApi(
                reinterpret_cast<void *>(static_cast<instantiate_prefab_fn>(&InstantiatePrefab))) == 0)
        {
            setManagedBridgeFailure("RegisterPrefabApi");
            return false;
        }

        if (!m_impl->registerScriptableObjectApi ||
            m_impl->registerScriptableObjectApi(
                reinterpret_cast<void *>(static_cast<load_scriptable_object_asset_fn>(&LoadScriptableObjectAsset))) == 0)
        {
            setManagedBridgeFailure("RegisterScriptableObjectApi");
            return false;
        }

        if (!m_impl->registerComponentApi ||
            m_impl->registerComponentApi(
                reinterpret_cast<void *>(static_cast<has_entity_component_fn>(&HasEntityComponent)),
                reinterpret_cast<void *>(static_cast<get_component_enabled_fn>(&GetComponentEnabled)),
                reinterpret_cast<void *>(static_cast<set_component_enabled_fn>(&SetComponentEnabled))) == 0)
        {
            setManagedBridgeFailure("RegisterComponentApi");
            return false;
        }

        if (!m_impl->registerCameraComponentApi ||
            m_impl->registerCameraComponentApi(
                reinterpret_cast<void *>(static_cast<get_camera_main_fn>(&GetCameraMain)),
                reinterpret_cast<void *>(static_cast<set_camera_main_fn>(&SetCameraMain)),
                reinterpret_cast<void *>(static_cast<get_camera_fov_fn>(&GetCameraFov)),
                reinterpret_cast<void *>(static_cast<set_camera_fov_fn>(&SetCameraFov))) == 0)
        {
            setManagedBridgeFailure("RegisterCameraComponentApi");
            return false;
        }

        if (!m_impl->registerLightComponentApi ||
            m_impl->registerLightComponentApi(
                reinterpret_cast<void *>(static_cast<get_light_intensity_fn>(&GetLightIntensity)),
                reinterpret_cast<void *>(static_cast<set_light_intensity_fn>(&SetLightIntensity)),
                reinterpret_cast<void *>(static_cast<get_light_color_fn>(&GetLightColor)),
                reinterpret_cast<void *>(static_cast<set_light_color_fn>(&SetLightColor))) == 0)
        {
            setManagedBridgeFailure("RegisterLightComponentApi");
            return false;
        }

        if (!m_impl->registerMeshComponentApi ||
            m_impl->registerMeshComponentApi(
                reinterpret_cast<void *>(static_cast<get_mesh_static_fn>(&GetMeshStatic)),
                reinterpret_cast<void *>(static_cast<set_mesh_static_fn>(&SetMeshStatic)),
                reinterpret_cast<void *>(static_cast<get_mesh_color_fn>(&GetMeshColor)),
                reinterpret_cast<void *>(static_cast<set_mesh_color_fn>(&SetMeshColor)),
                reinterpret_cast<void *>(static_cast<get_mesh_color_fn>(&GetMeshEmission)),
                reinterpret_cast<void *>(static_cast<set_mesh_color_fn>(&SetMeshEmission))) == 0)
        {
            setManagedBridgeFailure("RegisterMeshComponentApi");
            return false;
        }

        if (!m_impl->registerAnimationComponentApi ||
            m_impl->registerAnimationComponentApi(
                reinterpret_cast<void *>(static_cast<get_component_int_fn>(&GetAnimationClipCount)),
                reinterpret_cast<void *>(static_cast<get_component_int_fn>(&GetAnimationClipIndex)),
                reinterpret_cast<void *>(static_cast<set_component_int_fn>(&SetAnimationClipIndex)),
                reinterpret_cast<void *>(static_cast<get_component_string_by_index_fn>(&GetAnimationClipName)),
                reinterpret_cast<void *>(static_cast<get_component_float_by_index_fn>(&GetAnimationClipDuration)),
                reinterpret_cast<void *>(static_cast<get_component_bool_fn>(&GetAnimationPlaying)),
                reinterpret_cast<void *>(static_cast<set_component_bool_fn>(&SetAnimationPlaying)),
                reinterpret_cast<void *>(static_cast<get_component_bool_fn>(&GetAnimationLooping)),
                reinterpret_cast<void *>(static_cast<set_component_bool_fn>(&SetAnimationLooping)),
                reinterpret_cast<void *>(static_cast<get_component_bool_fn>(&GetAnimationAutoplay)),
                reinterpret_cast<void *>(static_cast<set_component_bool_fn>(&SetAnimationAutoplay)),
                reinterpret_cast<void *>(static_cast<get_component_float_fn>(&GetAnimationSpeed)),
                reinterpret_cast<void *>(static_cast<set_component_float_fn>(&SetAnimationSpeed)),
                reinterpret_cast<void *>(static_cast<get_component_float_fn>(&GetAnimationTime)),
                reinterpret_cast<void *>(static_cast<set_component_float_fn>(&SetAnimationTime)),
                reinterpret_cast<void *>(static_cast<component_action_fn>(&AnimationPlay)),
                reinterpret_cast<void *>(static_cast<component_action_fn>(&AnimationPause)),
                reinterpret_cast<void *>(static_cast<component_action_fn>(&AnimationStop)),
                reinterpret_cast<void *>(static_cast<set_animation_bool_parameter_fn>(&SetAnimationBoolParameter)),
                reinterpret_cast<void *>(static_cast<set_animation_float_parameter_fn>(&SetAnimationFloatParameter)),
                reinterpret_cast<void *>(static_cast<set_animation_int_parameter_fn>(&SetAnimationIntParameter)),
                reinterpret_cast<void *>(static_cast<set_component_string_fn>(&SetAnimationTriggerParameter)),
                reinterpret_cast<void *>(static_cast<set_component_string_fn>(&ResetAnimationTriggerParameter)),
                reinterpret_cast<void *>(static_cast<set_component_string_fn>(&AnimationPlayState))) == 0)
        {
            setManagedBridgeFailure("RegisterAnimationComponentApi");
            return false;
        }

        if (!m_impl->registerRigidbodyComponentApi ||
            m_impl->registerRigidbodyComponentApi(
                reinterpret_cast<void *>(static_cast<get_component_float_fn>(&GetRigidbodyMass)),
                reinterpret_cast<void *>(static_cast<set_component_float_fn>(&SetRigidbodyMass)),
                reinterpret_cast<void *>(static_cast<get_component_float_fn>(&GetRigidbodyLinearDrag)),
                reinterpret_cast<void *>(static_cast<set_component_float_fn>(&SetRigidbodyLinearDrag)),
                reinterpret_cast<void *>(static_cast<get_component_float_fn>(&GetRigidbodyAngularDrag)),
                reinterpret_cast<void *>(static_cast<set_component_float_fn>(&SetRigidbodyAngularDrag)),
                reinterpret_cast<void *>(static_cast<get_component_float_fn>(&GetRigidbodyFriction)),
                reinterpret_cast<void *>(static_cast<set_component_float_fn>(&SetRigidbodyFriction)),
                reinterpret_cast<void *>(static_cast<get_component_bool_fn>(&GetRigidbodyUseGravity)),
                reinterpret_cast<void *>(static_cast<set_component_bool_fn>(&SetRigidbodyUseGravity)),
                reinterpret_cast<void *>(static_cast<get_component_bool_fn>(&GetRigidbodyKinematic)),
                reinterpret_cast<void *>(static_cast<set_component_bool_fn>(&SetRigidbodyKinematic)),
                reinterpret_cast<void *>(static_cast<get_component_bool_fn>(&GetRigidbodyFreezeRotation)),
                reinterpret_cast<void *>(static_cast<set_component_bool_fn>(&SetRigidbodyFreezeRotation)),
                reinterpret_cast<void *>(static_cast<get_component_vector3_fn>(&GetRigidbodyVelocity)),
                reinterpret_cast<void *>(static_cast<set_component_vector3_fn>(&SetRigidbodyVelocity)),
                reinterpret_cast<void *>(static_cast<get_component_vector3_fn>(&GetRigidbodyAngularVelocity)),
                reinterpret_cast<void *>(static_cast<set_component_vector3_fn>(&SetRigidbodyAngularVelocity)),
                reinterpret_cast<void *>(static_cast<set_component_vector3_fn>(&AddRigidbodyForce)),
                reinterpret_cast<void *>(static_cast<set_component_vector3_fn>(&AddRigidbodyImpulse))) == 0)
        {
            setManagedBridgeFailure("RegisterRigidbodyComponentApi");
            return false;
        }

        if (!m_impl->registerColliderComponentApi ||
            m_impl->registerColliderComponentApi(
                reinterpret_cast<void *>(static_cast<get_component_int_fn>(&GetColliderShape)),
                reinterpret_cast<void *>(static_cast<set_component_int_fn>(&SetColliderShape)),
                reinterpret_cast<void *>(static_cast<get_component_vector3_fn>(&GetColliderCenter)),
                reinterpret_cast<void *>(static_cast<set_component_vector3_fn>(&SetColliderCenter)),
                reinterpret_cast<void *>(static_cast<get_component_vector3_fn>(&GetColliderSize)),
                reinterpret_cast<void *>(static_cast<set_component_vector3_fn>(&SetColliderSize)),
                reinterpret_cast<void *>(static_cast<get_component_float_fn>(&GetColliderRadius)),
                reinterpret_cast<void *>(static_cast<set_component_float_fn>(&SetColliderRadius)),
                reinterpret_cast<void *>(static_cast<get_component_float_fn>(&GetColliderHeight)),
                reinterpret_cast<void *>(static_cast<set_component_float_fn>(&SetColliderHeight)),
                reinterpret_cast<void *>(static_cast<get_component_bool_fn>(&GetColliderTrigger)),
                reinterpret_cast<void *>(static_cast<set_component_bool_fn>(&SetColliderTrigger))) == 0)
        {
            setManagedBridgeFailure("RegisterColliderComponentApi");
            return false;
        }

        if (!m_impl->registerParticleSystemComponentApi ||
            m_impl->registerParticleSystemComponentApi(
                reinterpret_cast<void *>(static_cast<get_component_bool_fn>(&GetParticleSystemPlaying)),
                reinterpret_cast<void *>(static_cast<get_component_int_fn>(&GetParticleSystemParticleCount)),
                reinterpret_cast<void *>(static_cast<component_action_fn>(&ParticleSystemPlay)),
                reinterpret_cast<void *>(static_cast<component_action_fn>(&ParticleSystemPause)),
                reinterpret_cast<void *>(static_cast<set_component_bool_fn>(&ParticleSystemStop)),
                reinterpret_cast<void *>(static_cast<component_action_fn>(&ParticleSystemClear)),
                reinterpret_cast<void *>(static_cast<set_component_int_fn>(&ParticleSystemEmit)),
                reinterpret_cast<void *>(static_cast<particle_emit_at_fn>(&ParticleSystemEmitAt)),
                reinterpret_cast<void *>(static_cast<get_component_string_fn>(&GetParticleSystemAssetReference)),
                reinterpret_cast<void *>(static_cast<set_component_string_fn>(&SetParticleSystemAssetReference)),
                reinterpret_cast<void *>(static_cast<get_component_bool_fn>(&GetParticleSystemLooping)),
                reinterpret_cast<void *>(static_cast<set_component_bool_fn>(&SetParticleSystemLooping)),
                reinterpret_cast<void *>(static_cast<get_component_bool_fn>(&GetParticleSystemPlayOnAwake)),
                reinterpret_cast<void *>(static_cast<set_component_bool_fn>(&SetParticleSystemPlayOnAwake)),
                reinterpret_cast<void *>(static_cast<get_component_float_fn>(&GetParticleSystemDuration)),
                reinterpret_cast<void *>(static_cast<set_component_float_fn>(&SetParticleSystemDuration)),
                reinterpret_cast<void *>(static_cast<get_component_float_fn>(&GetParticleSystemStartLifetime)),
                reinterpret_cast<void *>(static_cast<set_component_float_fn>(&SetParticleSystemStartLifetime)),
                reinterpret_cast<void *>(static_cast<get_component_float_fn>(&GetParticleSystemStartSpeed)),
                reinterpret_cast<void *>(static_cast<set_component_float_fn>(&SetParticleSystemStartSpeed)),
                reinterpret_cast<void *>(static_cast<get_component_float_fn>(&GetParticleSystemStartSize)),
                reinterpret_cast<void *>(static_cast<set_component_float_fn>(&SetParticleSystemStartSize)),
                reinterpret_cast<void *>(static_cast<get_component_float_fn>(&GetParticleSystemGravityModifier)),
                reinterpret_cast<void *>(static_cast<set_component_float_fn>(&SetParticleSystemGravityModifier)),
                reinterpret_cast<void *>(static_cast<get_component_float_fn>(&GetParticleSystemEmissionRate)),
                reinterpret_cast<void *>(static_cast<set_component_float_fn>(&SetParticleSystemEmissionRate)),
                reinterpret_cast<void *>(static_cast<get_component_vector3_fn>(&GetParticleSystemStartColor)),
                reinterpret_cast<void *>(static_cast<set_component_vector3_fn>(&SetParticleSystemStartColor)),
                reinterpret_cast<void *>(static_cast<get_component_vector3_fn>(&GetParticleSystemShapeSize)),
                reinterpret_cast<void *>(static_cast<set_component_vector3_fn>(&SetParticleSystemShapeSize)),
                reinterpret_cast<void *>(static_cast<get_component_int_fn>(&GetParticleSystemSimulationSpace)),
                reinterpret_cast<void *>(static_cast<set_component_int_fn>(&SetParticleSystemSimulationSpace)),
                reinterpret_cast<void *>(static_cast<get_component_int_fn>(&GetParticleSystemShape)),
                reinterpret_cast<void *>(static_cast<set_component_int_fn>(&SetParticleSystemShape))) == 0)
        {
            setManagedBridgeFailure("RegisterParticleSystemComponentApi");
            return false;
        }

        if (!m_impl->registerRuntimeUIApi ||
            m_impl->registerRuntimeUIApi(
                reinterpret_cast<void *>(static_cast<get_component_float_fn>(&GetCanvasScaleFactor)),
                reinterpret_cast<void *>(static_cast<set_component_float_fn>(&SetCanvasScaleFactor)),
                reinterpret_cast<void *>(static_cast<get_component_int_fn>(&GetCanvasSortingOrder)),
                reinterpret_cast<void *>(static_cast<set_component_int_fn>(&SetCanvasSortingOrder)),
                reinterpret_cast<void *>(static_cast<get_component_vector3_fn>(&GetRectAnchoredPosition)),
                reinterpret_cast<void *>(static_cast<set_component_vector3_fn>(&SetRectAnchoredPosition)),
                reinterpret_cast<void *>(static_cast<get_component_vector3_fn>(&GetRectSizeDelta)),
                reinterpret_cast<void *>(static_cast<set_component_vector3_fn>(&SetRectSizeDelta)),
                reinterpret_cast<void *>(static_cast<get_component_int_fn>(&GetRectAnchorPreset)),
                reinterpret_cast<void *>(static_cast<set_component_int_fn>(&SetRectAnchorPreset)),
                reinterpret_cast<void *>(static_cast<get_component_vector3_fn>(&GetUIImageColor)),
                reinterpret_cast<void *>(static_cast<set_component_vector3_fn>(&SetUIImageColor)),
                reinterpret_cast<void *>(static_cast<get_component_float_fn>(&GetUIImageAlpha)),
                reinterpret_cast<void *>(static_cast<set_component_float_fn>(&SetUIImageAlpha)),
                reinterpret_cast<void *>(static_cast<get_component_string_fn>(&GetUIImageTexture)),
                reinterpret_cast<void *>(static_cast<set_component_string_fn>(&SetUIImageTexture)),
                reinterpret_cast<void *>(static_cast<get_component_bool_fn>(&GetUIImagePreserveAspect)),
                reinterpret_cast<void *>(static_cast<set_component_bool_fn>(&SetUIImagePreserveAspect)),
                reinterpret_cast<void *>(static_cast<get_component_float_fn>(&GetUIImageFillAmount)),
                reinterpret_cast<void *>(static_cast<set_component_float_fn>(&SetUIImageFillAmount)),
                reinterpret_cast<void *>(static_cast<get_component_string_fn>(&GetUIText)),
                reinterpret_cast<void *>(static_cast<set_component_string_fn>(&SetUIText)),
                reinterpret_cast<void *>(static_cast<get_component_vector3_fn>(&GetUITextColor)),
                reinterpret_cast<void *>(static_cast<set_component_vector3_fn>(&SetUITextColor)),
                reinterpret_cast<void *>(static_cast<get_component_float_fn>(&GetUITextFontSize)),
                reinterpret_cast<void *>(static_cast<set_component_float_fn>(&SetUITextFontSize)),
                reinterpret_cast<void *>(static_cast<get_component_bool_fn>(&GetUIButtonInteractable)),
                reinterpret_cast<void *>(static_cast<set_component_bool_fn>(&SetUIButtonInteractable)),
                reinterpret_cast<void *>(static_cast<get_component_bool_fn>(&GetUIButtonHovered)),
                reinterpret_cast<void *>(static_cast<get_component_bool_fn>(&GetUIButtonPressed)),
                reinterpret_cast<void *>(static_cast<get_component_bool_fn>(&GetUIButtonReleased)),
                reinterpret_cast<void *>(static_cast<get_component_bool_fn>(&GetUIButtonClicked))) == 0)
        {
            setManagedBridgeFailure("RegisterRuntimeUIApi");
            return false;
        }

        if (!m_impl->registerInputApi ||
            m_impl->registerInputApi(
                reinterpret_cast<void *>(static_cast<get_input_key_fn>(&GetKeyDown)),
                reinterpret_cast<void *>(static_cast<get_input_key_fn>(&GetKeyPressed)),
                reinterpret_cast<void *>(static_cast<get_input_key_fn>(&GetKeyReleased)),
                reinterpret_cast<void *>(static_cast<get_input_mouse_button_fn>(&GetMouseButtonDown)),
                reinterpret_cast<void *>(static_cast<get_input_mouse_button_fn>(&GetMouseButtonPressed)),
                reinterpret_cast<void *>(static_cast<get_input_mouse_button_fn>(&GetMouseButtonReleased)),
                reinterpret_cast<void *>(static_cast<get_input_mouse_vector2_fn>(&GetMousePosition)),
                reinterpret_cast<void *>(static_cast<get_input_mouse_vector2_fn>(&GetMouseDelta)),
                reinterpret_cast<void *>(static_cast<get_input_mouse_vector2_fn>(&GetMouseScrollDelta)),
                reinterpret_cast<void *>(static_cast<get_input_quit_requested_fn>(&GetQuitRequested)),
                reinterpret_cast<void *>(static_cast<get_input_cursor_locked_fn>(&GetCursorLocked)),
                reinterpret_cast<void *>(static_cast<set_input_cursor_locked_fn>(&SetCursorLocked))) == 0)
        {
            setManagedBridgeFailure("RegisterInputApi");
            return false;
        }

        if (!m_impl->registerPhysicsApi ||
            m_impl->registerPhysicsApi(
                reinterpret_cast<void *>(static_cast<physics_raycast_fn>(&PhysicsRaycast)),
                reinterpret_cast<void *>(static_cast<physics_raycast_tagged_fn>(&PhysicsRaycastTagged)),
                reinterpret_cast<void *>(static_cast<physics_move_kinematic_fn>(&PhysicsMoveKinematic))) == 0)
        {
            setManagedBridgeFailure("RegisterPhysicsApi");
            return false;
        }

        if (!m_impl->registerDebugApi ||
            m_impl->registerDebugApi(
                reinterpret_cast<void *>(static_cast<script_log_fn>(&LogScriptMessage))) == 0)
        {
            setManagedBridgeFailure("RegisterDebugApi");
            return false;
        }

        const std::string assemblyPathUtf8 = WideToUtf8(shadowAssemblyPath.wstring());
        if (!m_impl->loadScriptAssembly || m_impl->loadScriptAssembly(assemblyPathUtf8.c_str()) == 0)
        {
            setManagedBridgeFailure("LoadScriptAssembly");
            return false;
        }

        m_impl->loadedAssemblyPath = assemblyPath;
        m_impl->scriptClasses = ParseMetadata(TakeManagedString(*m_impl, m_impl->getScriptMetadata));
        m_impl->loaded = true;
        return true;
#else
        (void)assemblyPath;
        return false;
#endif
    }

    bool HostFxrScriptRuntime::IsLoaded() const
    {
        return m_impl->loaded;
    }

    std::vector<ScriptClassDefinition> HostFxrScriptRuntime::GetScriptClasses() const
    {
        return m_impl->scriptClasses;
    }

    std::string HostFxrScriptRuntime::GetLastError() const
    {
        return m_impl->lastError;
    }

    std::unique_ptr<ScriptInstance> HostFxrScriptRuntime::CreateInstance(const ScriptClassDefinition &scriptClass) const
    {
#ifdef _WIN32
        if (!m_impl->loaded || !m_impl->createScriptInstance)
        {
            return nullptr;
        }

        const int64_t instanceHandle = m_impl->createScriptInstance(scriptClass.GetFullName().c_str(), 0);
        if (instanceHandle == 0)
        {
            return nullptr;
        }

        return std::make_unique<ManagedScriptInstance>(m_impl, instanceHandle, scriptClass);
#else
        (void)scriptClass;
        return nullptr;
#endif
    }
}
