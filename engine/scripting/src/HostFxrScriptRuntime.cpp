#include "PlutoGE/scripting/HostFxrScriptRuntime.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/platform/InputState.h"
#include "PlutoGE/scripting/ScriptLogging.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/RmlUiRuntime.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/NavigationSystem.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/NavigationMeshComponent.h"
#include "PlutoGE/scene/components/ParticleSystemComponent.h"
#include "PlutoGE/scene/components/RigidbodyComponent.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/scene/components/SoundEmitterComponent.h"
#include "PlutoGE/scene/components/UIComponent.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
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

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

#ifdef _WIN32
#include <Windows.h>
#define PLUTO_HOST_CALL __cdecl
#else
#include <dlfcn.h>
#include <limits.h>
#include <unistd.h>
#define PLUTO_HOST_CALL
#endif

namespace PlutoGE::scripting
{
    namespace
    {
#if defined(_WIN32) || defined(__linux__)
#ifdef _WIN32
        using char_t = wchar_t;
#define HOST_TEXT(value) L##value
#else
        using char_t = char;
#define HOST_TEXT(value) value
#endif
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

        using hostfxr_initialize_for_runtime_config_fn = int(PLUTO_HOST_CALL *)(const char_t *, const hostfxr_initialize_parameters *, hostfxr_handle *);
        using hostfxr_get_runtime_delegate_fn = int(PLUTO_HOST_CALL *)(hostfxr_handle, hostfxr_delegate_type, void **);
        using hostfxr_close_fn = int(PLUTO_HOST_CALL *)(hostfxr_handle);
        using load_assembly_and_get_function_pointer_fn = int(PLUTO_HOST_CALL *)(const char_t *, const char_t *, const char_t *, const char_t *, void *, void **);

        using load_script_assembly_fn = int(PLUTO_HOST_CALL *)(const char *, const char *);
        using unload_script_assembly_fn = int(PLUTO_HOST_CALL *)();
        using get_marshaled_string_fn = const char *(PLUTO_HOST_CALL *)();
        using get_field_data_fn = const char *(PLUTO_HOST_CALL *)(int64_t);
        using free_marshaled_string_fn = void(PLUTO_HOST_CALL *)(const char *);
        using create_script_instance_fn = int64_t(PLUTO_HOST_CALL *)(const char *, uint32_t);
        using destroy_script_instance_fn = void(PLUTO_HOST_CALL *)(int64_t);
        using invoke_on_create_fn = int(PLUTO_HOST_CALL *)(int64_t);
        using invoke_on_update_fn = int(PLUTO_HOST_CALL *)(int64_t, float);
        using invoke_on_late_update_fn = int(PLUTO_HOST_CALL *)(int64_t, float);
        using invoke_on_destroy_fn = int(PLUTO_HOST_CALL *)(int64_t);
        using invoke_on_collision_fn = int(PLUTO_HOST_CALL *)(int64_t, uint32_t);
        using invoke_on_animation_event_fn = int(PLUTO_HOST_CALL *)(int64_t, const char *, const char *, float, int32_t);
        using apply_field_data_fn = int(PLUTO_HOST_CALL *)(int64_t, const char *);
        using set_entity_id_fn = int(PLUTO_HOST_CALL *)(int64_t, uint32_t);
        using register_game_object_api_fn = int(PLUTO_HOST_CALL *)(void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *);
        using register_prefab_api_fn = int(PLUTO_HOST_CALL *)(void *);
        using register_scene_api_fn = int(PLUTO_HOST_CALL *)(void *, void *, void *);
        using register_scriptable_object_api_fn = int(PLUTO_HOST_CALL *)(void *);
        using register_component_api_fn = int(PLUTO_HOST_CALL *)(void *, void *, void *);
        using register_camera_component_api_fn = int(PLUTO_HOST_CALL *)(void *, void *, void *, void *);
        using register_light_component_api_fn = int(PLUTO_HOST_CALL *)(void *, void *, void *, void *);
        using register_mesh_component_api_fn = int(PLUTO_HOST_CALL *)(void *, void *, void *, void *, void *, void *);
        using register_animation_component_api_fn = int(PLUTO_HOST_CALL *)(void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *);
        using register_rigidbody_component_api_fn = int(PLUTO_HOST_CALL *)(void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *);
        using register_collider_component_api_fn = int(PLUTO_HOST_CALL *)(void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *);
        using register_particle_system_component_api_fn = int(PLUTO_HOST_CALL *)(void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *);
        using register_sound_emitter_component_api_fn = int(PLUTO_HOST_CALL *)(void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *);
        using register_runtime_ui_api_fn = int(PLUTO_HOST_CALL *)(void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *);
        using register_advanced_ui_api_fn = int(PLUTO_HOST_CALL *)(void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *);
        using register_rml_ui_api_fn = int(PLUTO_HOST_CALL *)(void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *);
        using register_input_api_fn = int(PLUTO_HOST_CALL *)(void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *, void *);
        using register_physics_api_fn = int(PLUTO_HOST_CALL *)(void *, void *, void *, void *);
        using register_navigation_api_fn = int(PLUTO_HOST_CALL *)(void *, void *);
        using register_debug_api_fn = int(PLUTO_HOST_CALL *)(void *);

        struct HostFxrLocation
        {
            std::filesystem::path libraryPath;
            std::filesystem::path dotnetRoot;
        };

        struct NativeVector3
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        struct NativeQuaternion
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float w = 1.0f;
        };

        struct NativeRaycastHit
        {
            uint32_t entityId = 0;
            NativeVector3 point{};
            NativeVector3 normal{};
            float distance = 0.0f;
        };

        using get_entity_vector3_fn = NativeVector3(PLUTO_HOST_CALL *)(uint32_t);
        using set_entity_vector3_fn = void(PLUTO_HOST_CALL *)(uint32_t, NativeVector3);
        using get_entity_quaternion_fn = NativeQuaternion(PLUTO_HOST_CALL *)(uint32_t);
        using set_entity_quaternion_fn = void(PLUTO_HOST_CALL *)(uint32_t, NativeQuaternion);
        using get_entity_active_fn = int(PLUTO_HOST_CALL *)(uint32_t);
        using set_entity_active_fn = void(PLUTO_HOST_CALL *)(uint32_t, int32_t);
        using get_entity_tag_count_fn = int(PLUTO_HOST_CALL *)(uint32_t);
        using get_entity_tag_fn = const char *(PLUTO_HOST_CALL *)(uint32_t, int32_t);
        using destroy_entity_fn = int(PLUTO_HOST_CALL *)(uint32_t);
        using get_entity_name_fn = const char *(PLUTO_HOST_CALL *)(uint32_t);
        using find_entity_by_name_fn = uint32_t(PLUTO_HOST_CALL *)(const char *);
        using get_entity_count_by_tag_fn = int32_t(PLUTO_HOST_CALL *)(const char *);
        using get_entity_by_tag_fn = uint32_t(PLUTO_HOST_CALL *)(const char *, int32_t);
        using instantiate_prefab_fn = uint32_t(PLUTO_HOST_CALL *)(const char *);
        using load_scene_fn = int(PLUTO_HOST_CALL *)(const char *);
        using quit_application_fn = void(PLUTO_HOST_CALL *)();
        using load_scriptable_object_asset_fn = const char *(PLUTO_HOST_CALL *)(const char *);
        using has_entity_component_fn = int(PLUTO_HOST_CALL *)(uint32_t, int32_t);
        using get_component_enabled_fn = int(PLUTO_HOST_CALL *)(uint32_t, int32_t);
        using set_component_enabled_fn = void(PLUTO_HOST_CALL *)(uint32_t, int32_t, int32_t);
        using get_camera_main_fn = int(PLUTO_HOST_CALL *)(uint32_t);
        using set_camera_main_fn = void(PLUTO_HOST_CALL *)(uint32_t, int32_t);
        using get_camera_fov_fn = float(PLUTO_HOST_CALL *)(uint32_t);
        using set_camera_fov_fn = void(PLUTO_HOST_CALL *)(uint32_t, float);
        using get_light_intensity_fn = float(PLUTO_HOST_CALL *)(uint32_t);
        using set_light_intensity_fn = void(PLUTO_HOST_CALL *)(uint32_t, float);
        using get_light_color_fn = NativeVector3(PLUTO_HOST_CALL *)(uint32_t);
        using set_light_color_fn = void(PLUTO_HOST_CALL *)(uint32_t, NativeVector3);
        using get_mesh_static_fn = int(PLUTO_HOST_CALL *)(uint32_t);
        using set_mesh_static_fn = void(PLUTO_HOST_CALL *)(uint32_t, int32_t);
        using get_mesh_color_fn = NativeVector3(PLUTO_HOST_CALL *)(uint32_t);
        using set_mesh_color_fn = void(PLUTO_HOST_CALL *)(uint32_t, NativeVector3);
        using get_component_float_fn = float(PLUTO_HOST_CALL *)(uint32_t);
        using set_component_float_fn = void(PLUTO_HOST_CALL *)(uint32_t, float);
        using get_component_bool_fn = int(PLUTO_HOST_CALL *)(uint32_t);
        using set_component_bool_fn = void(PLUTO_HOST_CALL *)(uint32_t, int32_t);
        using get_component_int_fn = int(PLUTO_HOST_CALL *)(uint32_t);
        using set_component_int_fn = void(PLUTO_HOST_CALL *)(uint32_t, int32_t);
        using get_component_string_by_index_fn = const char *(PLUTO_HOST_CALL *)(uint32_t, int32_t);
        using get_component_string_fn = const char *(PLUTO_HOST_CALL *)(uint32_t);
        using get_component_float_by_index_fn = float(PLUTO_HOST_CALL *)(uint32_t, int32_t);
        using component_action_fn = void(PLUTO_HOST_CALL *)(uint32_t);
        using component_action_with_two_floats_fn = void(PLUTO_HOST_CALL *)(uint32_t, float, float);
        using get_component_vector3_fn = NativeVector3(PLUTO_HOST_CALL *)(uint32_t);
        using set_component_vector3_fn = void(PLUTO_HOST_CALL *)(uint32_t, NativeVector3);
        using rigidbody_force_at_position_fn = void(PLUTO_HOST_CALL *)(uint32_t, NativeVector3, NativeVector3);
        using set_component_string_fn = void(PLUTO_HOST_CALL *)(uint32_t, const char *);
        using particle_emit_at_fn = void(PLUTO_HOST_CALL *)(uint32_t, NativeVector3, int32_t);
        using set_animation_bool_parameter_fn = void(PLUTO_HOST_CALL *)(uint32_t, const char *, int32_t);
        using set_animation_float_parameter_fn = void(PLUTO_HOST_CALL *)(uint32_t, const char *, float);
        using set_animation_int_parameter_fn = void(PLUTO_HOST_CALL *)(uint32_t, const char *, int32_t);
        using get_input_key_fn = int(PLUTO_HOST_CALL *)(int32_t);
        using get_input_mouse_button_fn = int(PLUTO_HOST_CALL *)(int32_t);
        using get_input_mouse_vector2_fn = NativeVector3(PLUTO_HOST_CALL *)();
        using get_input_quit_requested_fn = int(PLUTO_HOST_CALL *)();
        using get_input_cursor_locked_fn = int(PLUTO_HOST_CALL *)();
        using set_input_cursor_locked_fn = void(PLUTO_HOST_CALL *)(int32_t);
        using physics_raycast_fn = int(PLUTO_HOST_CALL *)(NativeVector3, NativeVector3, float, uint32_t, NativeRaycastHit *);
        using physics_raycast_tagged_fn = int(PLUTO_HOST_CALL *)(NativeVector3, NativeVector3, float, uint32_t, const char *, NativeRaycastHit *);
        using physics_move_kinematic_fn = NativeVector3(PLUTO_HOST_CALL *)(uint32_t, NativeVector3, float);
        using spawn_decal_fn = uint32_t(PLUTO_HOST_CALL *)(NativeVector3, NativeVector3, const char *, NativeVector3, float, float);
        using navigation_project_point_fn = int(PLUTO_HOST_CALL *)(uint32_t, NativeVector3, float, float, NativeVector3 *);
        using navigation_find_path_fn = int32_t(PLUTO_HOST_CALL *)(uint32_t, NativeVector3, NativeVector3, float, float, NativeVector3 *, int32_t, int32_t *);
        using script_log_fn = void(PLUTO_HOST_CALL *)(int32_t, const char *);
        constexpr std::basic_string_view<char_t> kScriptBridgeType = HOST_TEXT("PlutoGE.ScriptCore.Native.ScriptBridge, PlutoGE.ScriptCore");
        constexpr std::string_view kScriptCoreAssembly = "PlutoGE.ScriptCore.dll";
        constexpr std::string_view kScriptCoreRuntimeConfig = "PlutoGE.ScriptCore.runtimeconfig.json";

        const char_t *GetUnmanagedCallersOnlyMethodMarker()
        {
            return reinterpret_cast<const char_t *>(static_cast<std::intptr_t>(-1));
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
            SoundEmitter = 13,
            RmlWidget = 14,
        };

        std::basic_string<char_t> Utf8ToHostString(std::string_view text)
        {
#ifdef _WIN32
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
#else
            return std::string(text);
#endif
        }

        std::string HostStringToUtf8(std::basic_string_view<char_t> text)
        {
#ifdef _WIN32
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
#else
            return std::string(text);
#endif
        }

        std::optional<std::basic_string<char_t>> GetEnvironmentVariableText(const char_t *name)
        {
#ifdef _WIN32
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
#else
            const char *value = std::getenv(name);
            if (!value || *value == '\0')
            {
                return std::nullopt;
            }
            return std::string(value);
#endif
        }

        std::filesystem::path GetExecutableDirectory()
        {
#ifdef _WIN32
            std::array<wchar_t, MAX_PATH> modulePath{};
            const DWORD modulePathLength = GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
            if (modulePathLength == 0 || modulePathLength >= modulePath.size())
            {
                return {};
            }

            return std::filesystem::path(modulePath.data()).parent_path().lexically_normal();
#else
            std::array<char, PATH_MAX> modulePath{};
            const ssize_t modulePathLength = readlink("/proc/self/exe", modulePath.data(), modulePath.size() - 1);
            if (modulePathLength <= 0 || static_cast<size_t>(modulePathLength) >= modulePath.size())
            {
                return {};
            }
            modulePath[static_cast<size_t>(modulePathLength)] = '\0';
            return std::filesystem::path(modulePath.data()).parent_path().lexically_normal();
#endif
        }

        uint64_t GetProcessIdValue()
        {
#ifdef _WIN32
            return static_cast<uint64_t>(GetCurrentProcessId());
#else
            return static_cast<uint64_t>(getpid());
#endif
        }

        uint64_t GetMonotonicTimestamp()
        {
#ifdef _WIN32
            return static_cast<uint64_t>(GetTickCount64());
#else
            return static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
        }

        std::optional<std::filesystem::path> FindHostFxrInDotnetRoot(const std::filesystem::path &dotnetRoot)
        {
            const auto fxrDirectory = dotnetRoot / "host" / "fxr";
            if (!std::filesystem::exists(fxrDirectory))
            {
                return std::nullopt;
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
#ifdef _WIN32
                const auto candidate = *iterator / "hostfxr.dll";
#else
                const auto candidate = *iterator / "libhostfxr.so";
#endif
                if (std::filesystem::exists(candidate))
                {
                    return candidate;
                }
            }

            return std::nullopt;
        }

        std::optional<HostFxrLocation> FindHostFxrLibrary()
        {
            std::vector<std::filesystem::path> dotnetRoots;
            if (const auto executableDirectory = GetExecutableDirectory(); !executableDirectory.empty())
            {
                dotnetRoots.emplace_back(executableDirectory / "DotnetRuntime");
            }

            if (const auto dotnetRoot = GetEnvironmentVariableText(HOST_TEXT("DOTNET_ROOT")))
            {
                dotnetRoots.emplace_back(*dotnetRoot);
            }

#ifdef _WIN32
            if (const auto programFiles = GetEnvironmentVariableText(HOST_TEXT("ProgramFiles")))
            {
                dotnetRoots.emplace_back(std::filesystem::path(*programFiles) / "dotnet");
            }
#else
            dotnetRoots.emplace_back("/usr/share/dotnet");
            dotnetRoots.emplace_back("/usr/lib/dotnet");
#endif

            for (const auto &dotnetRoot : dotnetRoots)
            {
                if (const auto hostFxrPath = FindHostFxrInDotnetRoot(dotnetRoot))
                {
                    return HostFxrLocation{*hostFxrPath, dotnetRoot};
                }
            }

            return std::nullopt;
        }

        std::vector<std::filesystem::path> BuildScriptCoreCandidates(const std::filesystem::path &assemblyPath)
        {
            std::vector<std::filesystem::path> candidates;

            if (const auto envOverride = GetEnvironmentVariableText(HOST_TEXT("PLUTOGE_SCRIPTCORE_DIR")))
            {
                candidates.emplace_back(*envOverride);
            }

            auto current = std::filesystem::current_path();
            std::vector<std::filesystem::path> searchRoots;
            const auto executableDirectory = GetExecutableDirectory();
            if (!executableDirectory.empty())
            {
                current = executableDirectory;
                searchRoots.push_back(current);
            }

            searchRoots.push_back(std::filesystem::current_path());

            for (auto root : searchRoots)
            {
                while (!root.empty())
                {
                    candidates.push_back(root / "engine" / "scripting" / "managed" / "PlutoGE.ScriptCore" / "bin" / "Release" / "net8.0");
                    candidates.push_back(root / "engine" / "scripting" / "managed" / "PlutoGE.ScriptCore" / "bin" / "Debug" / "net8.0");

                    if (root == root.root_path())
                    {
                        break;
                    }

                    root = root.parent_path();
                }
            }

            if (!executableDirectory.empty())
            {
                candidates.push_back(executableDirectory);
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
            if (fieldTypeValue < static_cast<int>(ScriptFieldType::None) || fieldTypeValue > static_cast<int>(ScriptFieldType::MaterialAsset))
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
            case ScriptFieldType::MaterialAsset:
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
            case ScriptFieldType::SoundEmitterComponent:
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
            case ManagedComponentKind::SoundEmitter:
                return entity->GetComponent<scene::SoundEmitterComponent>();
            case ManagedComponentKind::RmlWidget:
                return entity->GetComponent<scene::RmlWidgetComponent>();
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

        void SetEntityWorldPosition(uint32_t entityId, NativeVector3 position)
        {
            auto *entity = FindEntity(entityId);
            if (!entity || !IsFiniteVector3(position))
            {
                return;
            }

            entity->SetWorldPosition(glm::vec3(position.x, position.y, position.z));
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

        NativeVector3 GetEntityWorldRotation(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            if (!entity)
            {
                return {};
            }

            const auto rotation = entity->GetWorldRotation();
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

        void SetEntityWorldRotation(uint32_t entityId, NativeVector3 rotation)
        {
            auto *entity = FindEntity(entityId);
            if (!entity || !IsFiniteVector3(rotation))
            {
                return;
            }

            entity->SetWorldRotation(glm::vec3(rotation.x, rotation.y, rotation.z));
        }

        NativeQuaternion GetEntityRotationQuaternion(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            if (!entity)
                return {};

            const glm::vec3 radians = glm::radians(entity->GetRotation());
            const glm::quat rotation = glm::normalize(glm::quat_cast(
                glm::eulerAngleXYZ(radians.x, radians.y, radians.z)));
            return {rotation.x, rotation.y, rotation.z, rotation.w};
        }

        void SetEntityRotationQuaternion(uint32_t entityId, NativeQuaternion value)
        {
            auto *entity = FindEntity(entityId);
            const glm::quat input(value.w, value.x, value.y, value.z);
            const float lengthSquared = glm::dot(input, input);
            if (!entity || !std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12f)
                return;

            const glm::mat4 matrix = glm::mat4_cast(glm::normalize(input));
            float x = 0.0f, y = 0.0f, z = 0.0f;
            glm::extractEulerAngleXYZ(matrix, x, y, z);
            entity->SetRotation(glm::degrees(glm::vec3(x, y, z)));
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

        int LoadScene(const char *sceneAssetReference)
        {
            if (!sceneAssetReference || sceneAssetReference[0] == '\0')
            {
                return 0;
            }
            return core::Engine::GetInstance().RequestSceneLoad(sceneAssetReference) ? 1 : 0;
        }

        const char *GetActiveScenePath()
        {
            const auto *activeScene = core::Engine::GetInstance().GetScene();
            return activeScene ? activeScene->GetFilePath().c_str() : "";
        }

        void QuitApplication()
        {
            core::Engine::GetInstance().RequestApplicationQuit();
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

        void AddRigidbodyForceAtPosition(uint32_t entityId, NativeVector3 value, NativeVector3 worldPosition)
        {
            if (IsFiniteVector3(value) && IsFiniteVector3(worldPosition))
            {
                if (auto *activeScene = core::Engine::GetInstance().GetScene())
                {
                    activeScene->AddRigidbodyForce(
                        entityId,
                        glm::vec3(value.x, value.y, value.z),
                        false,
                        glm::vec3(worldPosition.x, worldPosition.y, worldPosition.z));
                }
            }
        }

        void AddRigidbodyImpulseAtPosition(uint32_t entityId, NativeVector3 value, NativeVector3 worldPosition)
        {
            if (IsFiniteVector3(value) && IsFiniteVector3(worldPosition))
            {
                if (auto *activeScene = core::Engine::GetInstance().GetScene())
                {
                    activeScene->AddRigidbodyForce(
                        entityId,
                        glm::vec3(value.x, value.y, value.z),
                        true,
                        glm::vec3(worldPosition.x, worldPosition.y, worldPosition.z));
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
        int32_t GetColliderBlocksAudio(uint32_t entityId)
        {
            auto *component = FindCollider(entityId);
            return component && component->BlocksAudio() ? 1 : 0;
        }
        void SetColliderBlocksAudio(uint32_t entityId, int32_t value)
        {
            if (auto *component = FindCollider(entityId))
                component->SetBlocksAudio(value != 0);
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

        scene::SoundEmitterComponent *FindSoundEmitter(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            return entity ? entity->GetComponent<scene::SoundEmitterComponent>() : nullptr;
        }

        int32_t GetSoundEmitterPlaying(uint32_t entityId)
        {
            auto *component = FindSoundEmitter(entityId);
            return component && component->IsPlaying() ? 1 : 0;
        }
        void SoundEmitterPlay(uint32_t entityId)
        {
            if (auto *component = FindSoundEmitter(entityId))
                component->Play();
        }
        void SoundEmitterPlayOneShot(uint32_t entityId)
        {
            if (auto *component = FindSoundEmitter(entityId))
                component->PlayOneShot();
        }
        void SoundEmitterPlayOneShotScaled(uint32_t entityId, float volumeScale, float pitchScale)
        {
            if (auto *component = FindSoundEmitter(entityId))
                component->PlayOneShot(volumeScale, pitchScale);
        }
        void SoundEmitterPause(uint32_t entityId)
        {
            if (auto *component = FindSoundEmitter(entityId))
                component->Pause();
        }
        void SoundEmitterStop(uint32_t entityId)
        {
            if (auto *component = FindSoundEmitter(entityId))
                component->Stop();
        }
        const char *GetSoundEmitterClipReference(uint32_t entityId)
        {
            thread_local std::string assetReferenceStorage;
            assetReferenceStorage.clear();
            if (auto *component = FindSoundEmitter(entityId))
            {
                assetReferenceStorage = component->GetClipReference();
            }
            return assetReferenceStorage.c_str();
        }
        void SetSoundEmitterClipReference(uint32_t entityId, const char *assetReference)
        {
            if (auto *component = FindSoundEmitter(entityId))
            {
                component->SetClipReference(assetReference ? assetReference : "");
            }
        }
        int32_t GetSoundEmitterLooping(uint32_t entityId)
        {
            auto *component = FindSoundEmitter(entityId);
            return component && component->GetLooping() ? 1 : 0;
        }
        void SetSoundEmitterLooping(uint32_t entityId, int32_t value)
        {
            if (auto *component = FindSoundEmitter(entityId))
                component->SetLooping(value != 0);
        }
        int32_t GetSoundEmitterSpatialized(uint32_t entityId)
        {
            auto *component = FindSoundEmitter(entityId);
            return component && component->IsSpatialized() ? 1 : 0;
        }
        void SetSoundEmitterSpatialized(uint32_t entityId, int32_t value)
        {
            if (auto *component = FindSoundEmitter(entityId))
                component->SetSpatialized(value != 0);
        }
        int32_t GetSoundEmitterPlayOnAwake(uint32_t entityId)
        {
            auto *component = FindSoundEmitter(entityId);
            return component && component->GetPlayOnAwake() ? 1 : 0;
        }
        void SetSoundEmitterPlayOnAwake(uint32_t entityId, int32_t value)
        {
            if (auto *component = FindSoundEmitter(entityId))
                component->SetPlayOnAwake(value != 0);
        }
        float GetSoundEmitterVolume(uint32_t entityId)
        {
            auto *component = FindSoundEmitter(entityId);
            return component ? component->GetVolume() : 0.0f;
        }
        void SetSoundEmitterVolume(uint32_t entityId, float value)
        {
            if (auto *component = FindSoundEmitter(entityId))
                component->SetVolume(value);
        }
        float GetSoundEmitterPitch(uint32_t entityId)
        {
            auto *component = FindSoundEmitter(entityId);
            return component ? component->GetPitch() : 1.0f;
        }
        void SetSoundEmitterPitch(uint32_t entityId, float value)
        {
            if (auto *component = FindSoundEmitter(entityId))
                component->SetPitch(value);
        }
        float GetSoundEmitterMinDistance(uint32_t entityId)
        {
            auto *component = FindSoundEmitter(entityId);
            return component ? component->GetMinDistance() : 1.0f;
        }
        void SetSoundEmitterMinDistance(uint32_t entityId, float value)
        {
            if (auto *component = FindSoundEmitter(entityId))
                component->SetMinDistance(value);
        }
        float GetSoundEmitterMaxDistance(uint32_t entityId)
        {
            auto *component = FindSoundEmitter(entityId);
            return component ? component->GetMaxDistance() : 30.0f;
        }
        void SetSoundEmitterMaxDistance(uint32_t entityId, float value)
        {
            if (auto *component = FindSoundEmitter(entityId))
                component->SetMaxDistance(value);
        }
        float GetSoundEmitterRolloff(uint32_t entityId)
        {
            auto *component = FindSoundEmitter(entityId);
            return component ? component->GetRolloff() : 1.0f;
        }
        void SetSoundEmitterRolloff(uint32_t entityId, float value)
        {
            if (auto *component = FindSoundEmitter(entityId))
                component->SetRolloff(value);
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
            if (!IsScriptInputEnabled() ||
                (keyCode != static_cast<int32_t>(platform::KeyCode::Escape) &&
                 render::RmlUiRuntime::Get().IsKeyboardInputCaptured()))
            {
                return 0;
            }

            const auto &inputState = GetInputState();
            return keyCode >= 0 && keyCode < static_cast<int32_t>(inputState.keys.size()) && inputState.keys[static_cast<size_t>(keyCode)] ? 1 : 0;
        }

        int32_t GetKeyPressed(int32_t keyCode)
        {
            if (!IsScriptInputEnabled() ||
                (keyCode != static_cast<int32_t>(platform::KeyCode::Escape) &&
                 render::RmlUiRuntime::Get().IsKeyboardInputCaptured()))
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
            if (!IsScriptInputEnabled() ||
                (keyCode != static_cast<int32_t>(platform::KeyCode::Escape) &&
                 render::RmlUiRuntime::Get().IsKeyboardInputCaptured()))
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
            if (!IsScriptInputEnabled() || render::RmlUiRuntime::Get().IsPointerInputCaptured())
            {
                return 0;
            }

            const auto &inputState = GetInputState();
            return button >= 0 && button < 8 && inputState.mouseState.buttons[button] ? 1 : 0;
        }

        int32_t GetMouseButtonPressed(int32_t button)
        {
            if (!IsScriptInputEnabled() || render::RmlUiRuntime::Get().IsPointerInputCaptured())
            {
                return 0;
            }

            const auto &inputState = GetInputState();
            return button >= 0 && button < 8 && inputState.mouseState.buttons[button] && !inputState.mouseState.previousButtons[button] ? 1 : 0;
        }

        int32_t GetMouseButtonReleased(int32_t button)
        {
            if (!IsScriptInputEnabled() || render::RmlUiRuntime::Get().IsPointerInputCaptured())
            {
                return 0;
            }

            const auto &inputState = GetInputState();
            return button >= 0 && button < 8 && !inputState.mouseState.buttons[button] && inputState.mouseState.previousButtons[button] ? 1 : 0;
        }

        NativeVector3 GetMousePosition()
        {
            if (!IsScriptInputEnabled() || render::RmlUiRuntime::Get().IsPointerInputCaptured())
            {
                return {};
            }

            const auto &inputState = GetInputState();
            return NativeVector3{static_cast<float>(inputState.mouseState.x), static_cast<float>(inputState.mouseState.y), 0.0f};
        }

        NativeVector3 GetMouseDelta()
        {
            if (!IsScriptInputEnabled() || render::RmlUiRuntime::Get().IsPointerInputCaptured())
            {
                return {};
            }

            const auto &inputState = GetInputState();
            return NativeVector3{static_cast<float>(inputState.mouseState.deltaX), static_cast<float>(inputState.mouseState.deltaY), 0.0f};
        }

        NativeVector3 GetMouseScrollDelta()
        {
            if (!IsScriptInputEnabled() || render::RmlUiRuntime::Get().IsPointerInputCaptured())
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

        scene::NavigationSystem *GetNavigationSystem(uint32_t entityId)
        {
            auto *activeScene = core::Engine::GetInstance().GetScene();
            auto *entity = activeScene ? activeScene->FindEntityByID(entityId) : nullptr;
            auto *mesh = entity ? entity->GetComponent<scene::NavigationMeshComponent>() : nullptr;
            if (!mesh)
                return nullptr;
            if (!mesh->GetNavigation().IsBaked() && mesh->ShouldHaveBake())
                mesh->Bake();
            return mesh->GetNavigation().IsBaked() ? &mesh->GetNavigation() : nullptr;
        }

        int32_t NavigationProjectPoint(uint32_t entityId, NativeVector3 point, float agentRadius,
                                       float agentHeight, NativeVector3 *projected)
        {
            auto *navigation = GetNavigationSystem(entityId);
            if (!navigation || !projected)
                return 0;
            glm::vec3 result{};
            if (!navigation->ProjectPoint({point.x, point.y, point.z}, result, agentRadius, agentHeight))
                return 0;
            *projected = {result.x, result.y, result.z};
            return 1;
        }

        int32_t NavigationFindPath(uint32_t entityId, NativeVector3 start, NativeVector3 end,
                                   float agentRadius, float agentHeight, NativeVector3 *points,
                                   int32_t capacity, int32_t *complete)
        {
            auto *navigation = GetNavigationSystem(entityId);
            if (!navigation)
                return 0;
            const auto path = navigation->FindPath({start.x, start.y, start.z}, {end.x, end.y, end.z},
                                                   agentRadius, agentHeight);
            if (complete)
                *complete = path.complete ? 1 : 0;
            const auto count = static_cast<int32_t>(path.points.size());
            if (points && capacity > 0)
            {
                const auto copyCount = (std::min)(count, capacity);
                for (int32_t index = 0; index < copyCount; ++index)
                    points[index] = {path.points[index].x, path.points[index].y, path.points[index].z};
            }
            return count;
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

        int32_t GetCanvasScaleMode(uint32_t entityId)
        {
            auto *component = FindCanvas(entityId);
            return component ? static_cast<int32_t>(component->GetScaleMode()) : 0;
        }
        void SetCanvasScaleMode(uint32_t entityId, int32_t value)
        {
            if (auto *component = FindCanvas(entityId))
                component->SetScaleMode(static_cast<scene::CanvasScaleMode>(std::clamp(value, 0, 2)));
        }
        NativeVector3 GetCanvasReferenceResolution(uint32_t entityId)
        {
            auto *component = FindCanvas(entityId);
            const glm::vec2 value = component ? component->GetReferenceResolution() : glm::vec2(1920.0f, 1080.0f);
            return {value.x, value.y, 0.0f};
        }
        void SetCanvasReferenceResolution(uint32_t entityId, NativeVector3 value)
        {
            if (auto *component = FindCanvas(entityId); component && std::isfinite(value.x) && std::isfinite(value.y))
                component->SetReferenceResolution({value.x, value.y});
        }
        float GetRectRotation(uint32_t entityId)
        {
            auto *component = FindRectTransform(entityId);
            return component ? component->GetRotation() : 0.0f;
        }
        void SetRectRotation(uint32_t entityId, float value)
        {
            if (auto *component = FindRectTransform(entityId); component && std::isfinite(value))
                component->SetRotation(value);
        }
        float GetRectOpacity(uint32_t entityId)
        {
            auto *component = FindRectTransform(entityId);
            return component ? component->GetOpacity() : 1.0f;
        }
        void SetRectOpacity(uint32_t entityId, float value)
        {
            if (auto *component = FindRectTransform(entityId); component && std::isfinite(value))
                component->SetOpacity(value);
        }
        NativeVector3 GetRectLocalScale(uint32_t entityId)
        {
            auto *component = FindRectTransform(entityId);
            const glm::vec2 value = component ? component->GetLocalScale() : glm::vec2(1.0f);
            return {value.x, value.y, 0.0f};
        }
        void SetRectLocalScale(uint32_t entityId, NativeVector3 value)
        {
            if (auto *component = FindRectTransform(entityId); component && std::isfinite(value.x) && std::isfinite(value.y))
                component->SetLocalScale({value.x, value.y});
        }
        int32_t GetRectLayoutMode(uint32_t entityId)
        {
            auto *component = FindRectTransform(entityId);
            return component ? static_cast<int32_t>(component->GetLayoutMode()) : 0;
        }
        void SetRectLayoutMode(uint32_t entityId, int32_t value)
        {
            if (auto *component = FindRectTransform(entityId))
                component->SetLayoutMode(static_cast<scene::UILayoutMode>(std::clamp(value, 0, 3)));
        }
        int32_t GetUIImageType(uint32_t entityId)
        {
            auto *component = FindUIImage(entityId);
            return component ? static_cast<int32_t>(component->GetImageType()) : 0;
        }
        void SetUIImageType(uint32_t entityId, int32_t value)
        {
            if (auto *component = FindUIImage(entityId))
                component->SetImageType(static_cast<scene::UIImageType>(std::clamp(value, 0, 8)));
        }
        float GetUIImageThickness(uint32_t entityId)
        {
            auto *component = FindUIImage(entityId);
            return component ? component->GetThickness() : 2.0f;
        }
        void SetUIImageThickness(uint32_t entityId, float value)
        {
            if (auto *component = FindUIImage(entityId); component && std::isfinite(value))
                component->SetThickness(value);
        }
        int32_t GetUITextAlignment(uint32_t entityId)
        {
            auto *component = FindUIText(entityId);
            return component ? static_cast<int32_t>(component->GetAlignment()) : 4;
        }
        void SetUITextAlignment(uint32_t entityId, int32_t value)
        {
            if (auto *component = FindUIText(entityId))
                component->SetAlignment(static_cast<scene::UITextAlignment>(std::clamp(value, 0, 8)));
        }
        uint64_t GetUIUpdateSequence()
        {
            auto *scene = core::Engine::GetInstance().GetScene();
            return scene ? scene->GetUpdateSequence() : 0;
        }

        int32_t RmlShowDocument(const char *document, int32_t visible)
        {
            return document && render::RmlUiRuntime::Get().ShowDocument(document, visible != 0);
        }
        const char *GetRmlWidgetSource(uint32_t entityId)
        {
            thread_local std::string value;
            auto *entity = FindEntity(entityId);
            auto *widget = entity ? entity->GetComponent<scene::RmlWidgetComponent>() : nullptr;
            value = widget ? widget->GetSource() : std::string{};
            return value.c_str();
        }
        void SetRmlWidgetSource(uint32_t entityId, const char *source)
        {
            if (auto *entity = FindEntity(entityId))
                if (auto *widget = entity->GetComponent<scene::RmlWidgetComponent>())
                    widget->SetSource(source ? source : "");
        }
        int32_t GetRmlWidgetVisible(uint32_t entityId)
        {
            auto *entity = FindEntity(entityId);
            auto *widget = entity ? entity->GetComponent<scene::RmlWidgetComponent>() : nullptr;
            return widget && widget->IsVisible();
        }
        void SetRmlWidgetVisible(uint32_t entityId, int32_t visible)
        {
            if (auto *entity = FindEntity(entityId))
                if (auto *widget = entity->GetComponent<scene::RmlWidgetComponent>())
                    widget->SetVisible(visible != 0);
        }
        int32_t RmlReloadDocument(const char *document)
        {
            return document && render::RmlUiRuntime::Get().ReloadDocument(document);
        }
        int32_t RmlSetText(const char *document, const char *id, const char *value)
        {
            return document && id && value && render::RmlUiRuntime::Get().SetElementText(document, id, value);
        }
        const char *RmlGetText(const char *document, const char *id)
        {
            thread_local std::string value;
            value = document && id ? render::RmlUiRuntime::Get().GetElementText(document, id) : std::string{};
            return value.c_str();
        }
        int32_t RmlSetAttribute(const char *document, const char *id, const char *name, const char *value)
        {
            return document && id && name && value &&
                   render::RmlUiRuntime::Get().SetElementAttribute(document, id, name, value);
        }
        const char *RmlGetAttribute(const char *document, const char *id, const char *name)
        {
            thread_local std::string value;
            value = document && id && name
                        ? render::RmlUiRuntime::Get().GetElementAttribute(document, id, name)
                        : std::string{};
            return value.c_str();
        }
        int32_t RmlSetClass(const char *document, const char *id, const char *name, int32_t enabled)
        {
            return document && id && name &&
                   render::RmlUiRuntime::Get().SetElementClass(document, id, name, enabled != 0);
        }
        int32_t RmlSetStyle(const char *document, const char *id, const char *name, const char *value)
        {
            return document && id && name && value &&
                   render::RmlUiRuntime::Get().SetElementStyle(document, id, name, value);
        }
        int32_t RmlSubscribeEvent(const char *document, const char *id, const char *event)
        {
            return document && id && event && render::RmlUiRuntime::Get().SubscribeEvent(document, id, event);
        }
        int32_t RmlConsumeEvent(const char *document, const char *id, const char *event)
        {
            return document && id && event && render::RmlUiRuntime::Get().ConsumeEvent(document, id, event);
        }
        float GetSceneTimeScale()
        {
            auto *scene = core::Engine::GetInstance().GetScene();
            return scene ? scene->GetTimeScale() : 1.0f;
        }
        void SetSceneTimeScale(float value)
        {
            if (auto *scene = core::Engine::GetInstance().GetScene(); scene && std::isfinite(value))
                scene->SetTimeScale(value < 0.0f ? 0.0f : value);
        }

        uint32_t SpawnDecal(NativeVector3 point,
                            NativeVector3 normal,
                            const char *materialAssetReference,
                            NativeVector3 sizeAndDepth,
                            float lifetime,
                            float fadeDuration)
        {
            if (!IsFiniteVector3(point) || !IsFiniteVector3(normal) || !IsFiniteVector3(sizeAndDepth) ||
                !std::isfinite(lifetime) || !std::isfinite(fadeDuration) ||
                !materialAssetReference || materialAssetReference[0] == '\0')
            {
                return 0;
            }

            auto *activeScene = core::Engine::GetInstance().GetScene();
            if (!activeScene)
            {
                return 0;
            }

            scene::PhysicsRaycastHit hit{
                .point = glm::vec3(point.x, point.y, point.z),
                .normal = glm::vec3(normal.x, normal.y, normal.z),
            };
            auto *decal = activeScene->SpawnDecal(
                hit,
                materialAssetReference,
                glm::vec2(sizeAndDepth.x, sizeAndDepth.y),
                sizeAndDepth.z,
                lifetime,
                fadeDuration);
            return decal ? decal->GetID() : 0;
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
#if defined(_WIN32) || defined(__linux__)
#ifdef _WIN32
        HMODULE hostfxrLibrary = nullptr;
#else
        void *hostfxrLibrary = nullptr;
#endif
        std::filesystem::path dotnetRoot;
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
        invoke_on_late_update_fn invokeOnLateUpdate = nullptr;
        invoke_on_destroy_fn invokeOnDestroy = nullptr;
        invoke_on_collision_fn invokeOnCollisionEnter = nullptr;
        invoke_on_collision_fn invokeOnCollisionExit = nullptr;
        invoke_on_animation_event_fn invokeOnAnimationEvent = nullptr;
        apply_field_data_fn applyFieldData = nullptr;
        set_entity_id_fn setEntityId = nullptr;
        register_game_object_api_fn registerGameObjectApi = nullptr;
        register_prefab_api_fn registerPrefabApi = nullptr;
        register_scene_api_fn registerSceneApi = nullptr;
        register_scriptable_object_api_fn registerScriptableObjectApi = nullptr;
        register_component_api_fn registerComponentApi = nullptr;
        register_camera_component_api_fn registerCameraComponentApi = nullptr;
        register_light_component_api_fn registerLightComponentApi = nullptr;
        register_mesh_component_api_fn registerMeshComponentApi = nullptr;
        register_animation_component_api_fn registerAnimationComponentApi = nullptr;
        register_rigidbody_component_api_fn registerRigidbodyComponentApi = nullptr;
        register_collider_component_api_fn registerColliderComponentApi = nullptr;
        register_particle_system_component_api_fn registerParticleSystemComponentApi = nullptr;
        register_sound_emitter_component_api_fn registerSoundEmitterComponentApi = nullptr;
        register_runtime_ui_api_fn registerRuntimeUIApi = nullptr;
        register_advanced_ui_api_fn registerAdvancedUIApi = nullptr;
        register_rml_ui_api_fn registerRmlUiApi = nullptr;
        register_input_api_fn registerInputApi = nullptr;
        register_physics_api_fn registerPhysicsApi = nullptr;
        register_navigation_api_fn registerNavigationApi = nullptr;
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
#if defined(_WIN32) || defined(__linux__)
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
            impl.invokeOnLateUpdate = nullptr;
            impl.invokeOnDestroy = nullptr;
            impl.invokeOnCollisionEnter = nullptr;
            impl.invokeOnCollisionExit = nullptr;
            impl.applyFieldData = nullptr;
            impl.setEntityId = nullptr;
            impl.registerGameObjectApi = nullptr;
            impl.registerPrefabApi = nullptr;
            impl.registerSceneApi = nullptr;
            impl.registerScriptableObjectApi = nullptr;
            impl.registerComponentApi = nullptr;
            impl.registerCameraComponentApi = nullptr;
            impl.registerLightComponentApi = nullptr;
            impl.registerMeshComponentApi = nullptr;
            impl.registerAnimationComponentApi = nullptr;
            impl.registerRigidbodyComponentApi = nullptr;
            impl.registerColliderComponentApi = nullptr;
            impl.registerParticleSystemComponentApi = nullptr;
            impl.registerSoundEmitterComponentApi = nullptr;
            impl.registerRuntimeUIApi = nullptr;
            impl.registerAdvancedUIApi = nullptr;
            impl.registerRmlUiApi = nullptr;
            impl.registerInputApi = nullptr;
            impl.registerPhysicsApi = nullptr;
            impl.registerNavigationApi = nullptr;
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
            const auto uniqueDirectoryName = assemblyPath.stem().string() + "-" + std::to_string(GetProcessIdValue()) + "-" + std::to_string(GetMonotonicTimestamp());
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
            const auto uniqueDirectoryName = assemblyPath.stem().string() + "-bridge-" + std::to_string(GetProcessIdValue()) + "-" + std::to_string(GetMonotonicTimestamp());
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
        bool LoadManagedExport(HostFxrScriptRuntime::Impl &impl, const char_t *methodName, DelegateType &delegate)
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
                impl.lastError = "Failed to load managed bridge export '" + HostStringToUtf8(methodName) +
                                 "' from " + impl.bridgeAssemblyPath.string() +
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

            const auto hostFxrLocation = FindHostFxrLibrary();
            if (!hostFxrLocation)
            {
                impl.lastError = "Failed to locate the .NET hostfxr library";
                return false;
            }

            impl.dotnetRoot = hostFxrLocation->dotnetRoot;
#ifdef _WIN32
            impl.hostfxrLibrary = LoadLibraryW(hostFxrLocation->libraryPath.c_str());
#else
            impl.hostfxrLibrary = dlopen(hostFxrLocation->libraryPath.c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif
            if (!impl.hostfxrLibrary)
            {
                impl.lastError = "Failed to load hostfxr library";
#ifndef _WIN32
                if (const char *loaderError = dlerror())
                {
                    impl.lastError += ": " + std::string(loaderError);
                }
#endif
                return false;
            }

#ifdef _WIN32
            impl.initializeForRuntimeConfig = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(GetProcAddress(impl.hostfxrLibrary, "hostfxr_initialize_for_runtime_config"));
            impl.getRuntimeDelegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(GetProcAddress(impl.hostfxrLibrary, "hostfxr_get_runtime_delegate"));
            impl.closeHostContext = reinterpret_cast<hostfxr_close_fn>(GetProcAddress(impl.hostfxrLibrary, "hostfxr_close"));
#else
            impl.initializeForRuntimeConfig = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(dlsym(impl.hostfxrLibrary, "hostfxr_initialize_for_runtime_config"));
            impl.getRuntimeDelegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(dlsym(impl.hostfxrLibrary, "hostfxr_get_runtime_delegate"));
            impl.closeHostContext = reinterpret_cast<hostfxr_close_fn>(dlsym(impl.hostfxrLibrary, "hostfxr_close"));
#endif

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
            hostfxr_initialize_parameters initializeParameters{};
            initializeParameters.size = sizeof(initializeParameters);
            initializeParameters.host_path = nullptr;
            initializeParameters.dotnet_root = impl.dotnetRoot.empty() ? nullptr : impl.dotnetRoot.c_str();
            const int initializeResult = impl.initializeForRuntimeConfig(impl.runtimeConfigPath.c_str(), &initializeParameters, &hostContext);
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
                LoadManagedExport(impl, HOST_TEXT("LoadScriptAssembly"), impl.loadScriptAssembly) &&
                LoadManagedExport(impl, HOST_TEXT("GetScriptMetadata"), impl.getScriptMetadata) &&
                LoadManagedExport(impl, HOST_TEXT("GetFieldData"), impl.getFieldData) &&
                LoadManagedExport(impl, HOST_TEXT("FreeNativeString"), impl.freeMarshaledString) &&
                LoadManagedExport(impl, HOST_TEXT("GetLastError"), impl.getLastError) &&
                LoadManagedExport(impl, HOST_TEXT("CreateScriptInstance"), impl.createScriptInstance) &&
                LoadManagedExport(impl, HOST_TEXT("DestroyScriptInstance"), impl.destroyScriptInstance) &&
                LoadManagedExport(impl, HOST_TEXT("InvokeOnCreate"), impl.invokeOnCreate) &&
                LoadManagedExport(impl, HOST_TEXT("InvokeOnUpdate"), impl.invokeOnUpdate) &&
                LoadManagedExport(impl, HOST_TEXT("InvokeOnLateUpdate"), impl.invokeOnLateUpdate) &&
                LoadManagedExport(impl, HOST_TEXT("InvokeOnDestroy"), impl.invokeOnDestroy) &&
                LoadManagedExport(impl, HOST_TEXT("InvokeOnCollisionEnter"), impl.invokeOnCollisionEnter) &&
                LoadManagedExport(impl, HOST_TEXT("InvokeOnCollisionExit"), impl.invokeOnCollisionExit) &&
                LoadManagedExport(impl, HOST_TEXT("InvokeOnAnimationEvent"), impl.invokeOnAnimationEvent) &&
                LoadManagedExport(impl, HOST_TEXT("ApplyFieldData"), impl.applyFieldData) &&
                LoadManagedExport(impl, HOST_TEXT("SetEntityId"), impl.setEntityId) &&
                LoadManagedExport(impl, HOST_TEXT("RegisterGameObjectApi"), impl.registerGameObjectApi) &&
                LoadManagedExport(impl, HOST_TEXT("RegisterPrefabApi"), impl.registerPrefabApi) &&
                LoadManagedExport(impl, HOST_TEXT("RegisterSceneApi"), impl.registerSceneApi) &&
                LoadManagedExport(impl, HOST_TEXT("RegisterScriptableObjectApi"), impl.registerScriptableObjectApi) &&
                LoadManagedExport(impl, HOST_TEXT("RegisterComponentApi"), impl.registerComponentApi) &&
                LoadManagedExport(impl, HOST_TEXT("RegisterCameraComponentApi"), impl.registerCameraComponentApi) &&
                LoadManagedExport(impl, HOST_TEXT("RegisterLightComponentApi"), impl.registerLightComponentApi) &&
                LoadManagedExport(impl, HOST_TEXT("RegisterMeshComponentApi"), impl.registerMeshComponentApi) &&
                LoadManagedExport(impl, HOST_TEXT("RegisterAnimationComponentApi"), impl.registerAnimationComponentApi) &&
                LoadManagedExport(impl, HOST_TEXT("RegisterRigidbodyComponentApi"), impl.registerRigidbodyComponentApi) &&
                LoadManagedExport(impl, HOST_TEXT("RegisterColliderComponentApi"), impl.registerColliderComponentApi) &&
                LoadManagedExport(impl, HOST_TEXT("RegisterParticleSystemComponentApi"), impl.registerParticleSystemComponentApi) &&
                LoadManagedExport(impl, HOST_TEXT("RegisterSoundEmitterComponentApi"), impl.registerSoundEmitterComponentApi) &&
                LoadManagedExport(impl, HOST_TEXT("RegisterRuntimeUIApi"), impl.registerRuntimeUIApi) &&
                LoadManagedExport(impl, HOST_TEXT("RegisterAdvancedUIApi"), impl.registerAdvancedUIApi) &&
                LoadManagedExport(impl, HOST_TEXT("RegisterRmlUiApi"), impl.registerRmlUiApi) &&
                LoadManagedExport(impl, HOST_TEXT("RegisterInputApi"), impl.registerInputApi) &&
                LoadManagedExport(impl, HOST_TEXT("RegisterPhysicsApi"), impl.registerPhysicsApi) &&
                LoadManagedExport(impl, HOST_TEXT("RegisterNavigationApi"), impl.registerNavigationApi) &&
                LoadManagedExport(impl, HOST_TEXT("RegisterDebugApi"), impl.registerDebugApi);

            if (!requiredExportsLoaded)
            {
                const std::string exportError = impl.lastError;
                ResetManagedBridge(impl);
                impl.lastError = exportError;
                return false;
            }

            if (!LoadManagedExport(impl, HOST_TEXT("UnloadScriptAssembly"), impl.unloadScriptAssembly))
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

            void OnLateUpdate(float deltaTime) override
            {
                if (m_impl && m_impl->invokeOnLateUpdate)
                {
                    m_impl->invokeOnLateUpdate(m_instanceHandle, deltaTime);
                }
            }

            void OnDestroy() override
            {
                if (m_impl && m_impl->invokeOnDestroy)
                {
                    m_impl->invokeOnDestroy(m_instanceHandle);
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

            void OnAnimationEvent(std::string_view name, std::string_view stringParameter,
                                  float floatParameter, int intParameter) override
            {
                if (m_impl && m_impl->invokeOnAnimationEvent)
                {
                    const std::string eventName(name);
                    const std::string eventString(stringParameter);
                    m_impl->invokeOnAnimationEvent(m_instanceHandle, eventName.c_str(), eventString.c_str(),
                                                   floatParameter, intParameter);
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
#if defined(_WIN32) || defined(__linux__)
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
#if defined(_WIN32) || defined(__linux__)
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
                reinterpret_cast<void *>(static_cast<get_entity_by_tag_fn>(&GetEntityByTag)),
                reinterpret_cast<void *>(static_cast<set_entity_vector3_fn>(&SetEntityWorldPosition)),
                reinterpret_cast<void *>(static_cast<get_entity_vector3_fn>(&GetEntityWorldRotation)),
                reinterpret_cast<void *>(static_cast<set_entity_vector3_fn>(&SetEntityWorldRotation)),
                reinterpret_cast<void *>(static_cast<get_entity_quaternion_fn>(&GetEntityRotationQuaternion)),
                reinterpret_cast<void *>(static_cast<set_entity_quaternion_fn>(&SetEntityRotationQuaternion))) == 0)
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

        if (!m_impl->registerSceneApi ||
            m_impl->registerSceneApi(
                reinterpret_cast<void *>(static_cast<load_scene_fn>(&LoadScene)),
                reinterpret_cast<void *>(static_cast<get_marshaled_string_fn>(&GetActiveScenePath)),
                reinterpret_cast<void *>(static_cast<quit_application_fn>(&QuitApplication))) == 0)
        {
            setManagedBridgeFailure("RegisterSceneApi");
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
                reinterpret_cast<void *>(static_cast<set_component_vector3_fn>(&AddRigidbodyImpulse)),
                reinterpret_cast<void *>(static_cast<rigidbody_force_at_position_fn>(&AddRigidbodyForceAtPosition)),
                reinterpret_cast<void *>(static_cast<rigidbody_force_at_position_fn>(&AddRigidbodyImpulseAtPosition))) == 0)
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
                reinterpret_cast<void *>(static_cast<set_component_bool_fn>(&SetColliderTrigger)),
                reinterpret_cast<void *>(static_cast<get_component_bool_fn>(&GetColliderBlocksAudio)),
                reinterpret_cast<void *>(static_cast<set_component_bool_fn>(&SetColliderBlocksAudio))) == 0)
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

        if (!m_impl->registerSoundEmitterComponentApi ||
            m_impl->registerSoundEmitterComponentApi(
                reinterpret_cast<void *>(static_cast<get_component_bool_fn>(&GetSoundEmitterPlaying)),
                reinterpret_cast<void *>(static_cast<component_action_fn>(&SoundEmitterPlay)),
                reinterpret_cast<void *>(static_cast<component_action_fn>(&SoundEmitterPlayOneShot)),
                reinterpret_cast<void *>(static_cast<component_action_with_two_floats_fn>(&SoundEmitterPlayOneShotScaled)),
                reinterpret_cast<void *>(static_cast<component_action_fn>(&SoundEmitterPause)),
                reinterpret_cast<void *>(static_cast<component_action_fn>(&SoundEmitterStop)),
                reinterpret_cast<void *>(static_cast<get_component_string_fn>(&GetSoundEmitterClipReference)),
                reinterpret_cast<void *>(static_cast<set_component_string_fn>(&SetSoundEmitterClipReference)),
                reinterpret_cast<void *>(static_cast<get_component_bool_fn>(&GetSoundEmitterLooping)),
                reinterpret_cast<void *>(static_cast<set_component_bool_fn>(&SetSoundEmitterLooping)),
                reinterpret_cast<void *>(static_cast<get_component_bool_fn>(&GetSoundEmitterSpatialized)),
                reinterpret_cast<void *>(static_cast<set_component_bool_fn>(&SetSoundEmitterSpatialized)),
                reinterpret_cast<void *>(static_cast<get_component_bool_fn>(&GetSoundEmitterPlayOnAwake)),
                reinterpret_cast<void *>(static_cast<set_component_bool_fn>(&SetSoundEmitterPlayOnAwake)),
                reinterpret_cast<void *>(static_cast<get_component_float_fn>(&GetSoundEmitterVolume)),
                reinterpret_cast<void *>(static_cast<set_component_float_fn>(&SetSoundEmitterVolume)),
                reinterpret_cast<void *>(static_cast<get_component_float_fn>(&GetSoundEmitterPitch)),
                reinterpret_cast<void *>(static_cast<set_component_float_fn>(&SetSoundEmitterPitch))) == 0)
        {
            setManagedBridgeFailure("RegisterSoundEmitterComponentApi");
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

        if (!m_impl->registerAdvancedUIApi ||
            m_impl->registerAdvancedUIApi(
                reinterpret_cast<void *>(static_cast<get_component_int_fn>(&GetCanvasScaleMode)),
                reinterpret_cast<void *>(static_cast<set_component_int_fn>(&SetCanvasScaleMode)),
                reinterpret_cast<void *>(static_cast<get_component_vector3_fn>(&GetCanvasReferenceResolution)),
                reinterpret_cast<void *>(static_cast<set_component_vector3_fn>(&SetCanvasReferenceResolution)),
                reinterpret_cast<void *>(static_cast<get_component_float_fn>(&GetRectRotation)),
                reinterpret_cast<void *>(static_cast<set_component_float_fn>(&SetRectRotation)),
                reinterpret_cast<void *>(static_cast<get_component_float_fn>(&GetRectOpacity)),
                reinterpret_cast<void *>(static_cast<set_component_float_fn>(&SetRectOpacity)),
                reinterpret_cast<void *>(static_cast<get_component_vector3_fn>(&GetRectLocalScale)),
                reinterpret_cast<void *>(static_cast<set_component_vector3_fn>(&SetRectLocalScale)),
                reinterpret_cast<void *>(static_cast<get_component_int_fn>(&GetRectLayoutMode)),
                reinterpret_cast<void *>(static_cast<set_component_int_fn>(&SetRectLayoutMode)),
                reinterpret_cast<void *>(static_cast<get_component_int_fn>(&GetUIImageType)),
                reinterpret_cast<void *>(static_cast<set_component_int_fn>(&SetUIImageType)),
                reinterpret_cast<void *>(static_cast<get_component_float_fn>(&GetUIImageThickness)),
                reinterpret_cast<void *>(static_cast<set_component_float_fn>(&SetUIImageThickness)),
                reinterpret_cast<void *>(static_cast<get_component_int_fn>(&GetUITextAlignment)),
                reinterpret_cast<void *>(static_cast<set_component_int_fn>(&SetUITextAlignment)),
                reinterpret_cast<void *>(&GetUIUpdateSequence)) == 0)
        {
            setManagedBridgeFailure("RegisterAdvancedUIApi");
            return false;
        }

        if (!m_impl->registerRmlUiApi ||
            m_impl->registerRmlUiApi(
                reinterpret_cast<void *>(&RmlShowDocument),
                reinterpret_cast<void *>(&RmlReloadDocument),
                reinterpret_cast<void *>(&RmlSetText),
                reinterpret_cast<void *>(&RmlGetText),
                reinterpret_cast<void *>(&RmlSetAttribute),
                reinterpret_cast<void *>(&RmlGetAttribute),
                reinterpret_cast<void *>(&RmlSetClass),
                reinterpret_cast<void *>(&RmlSetStyle),
                reinterpret_cast<void *>(&RmlSubscribeEvent),
                reinterpret_cast<void *>(&RmlConsumeEvent),
                reinterpret_cast<void *>(&GetSceneTimeScale),
                reinterpret_cast<void *>(&SetSceneTimeScale),
                reinterpret_cast<void *>(&GetRmlWidgetSource),
                reinterpret_cast<void *>(&SetRmlWidgetSource),
                reinterpret_cast<void *>(&GetRmlWidgetVisible),
                reinterpret_cast<void *>(&SetRmlWidgetVisible)) == 0)
        {
            setManagedBridgeFailure("RegisterRmlUiApi");
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
                reinterpret_cast<void *>(static_cast<physics_move_kinematic_fn>(&PhysicsMoveKinematic)),
                reinterpret_cast<void *>(static_cast<spawn_decal_fn>(&SpawnDecal))) == 0)
        {
            setManagedBridgeFailure("RegisterPhysicsApi");
            return false;
        }

        if (!m_impl->registerNavigationApi ||
            m_impl->registerNavigationApi(
                reinterpret_cast<void *>(static_cast<navigation_project_point_fn>(&NavigationProjectPoint)),
                reinterpret_cast<void *>(static_cast<navigation_find_path_fn>(&NavigationFindPath))) == 0)
        {
            setManagedBridgeFailure("RegisterNavigationApi");
            return false;
        }

        if (!m_impl->registerDebugApi ||
            m_impl->registerDebugApi(
                reinterpret_cast<void *>(static_cast<script_log_fn>(&LogScriptMessage))) == 0)
        {
            setManagedBridgeFailure("RegisterDebugApi");
            return false;
        }

        const std::string assemblyPathUtf8 = shadowAssemblyPath.string();
        const std::string sourceAssemblyPathUtf8 = std::filesystem::absolute(assemblyPath).string();
        if (!m_impl->loadScriptAssembly ||
            m_impl->loadScriptAssembly(
                assemblyPathUtf8.c_str(),
                sourceAssemblyPathUtf8.c_str()) == 0)
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
#if defined(_WIN32) || defined(__linux__)
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
