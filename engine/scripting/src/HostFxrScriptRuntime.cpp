#include "PlutoGE/scripting/HostFxrScriptRuntime.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <filesystem>
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
        using get_marshaled_string_fn = const char *(__cdecl *)();
        using free_marshaled_string_fn = void(__cdecl *)(const char *);
        using create_script_instance_fn = int64_t(__cdecl *)(const char *, uint32_t);
        using destroy_script_instance_fn = void(__cdecl *)(int64_t);
        using invoke_on_create_fn = int(__cdecl *)(int64_t);
        using invoke_on_update_fn = int(__cdecl *)(int64_t, float);
        using apply_field_data_fn = int(__cdecl *)(int64_t, const char *);
        using set_entity_id_fn = int(__cdecl *)(int64_t, uint32_t);
        using register_entity_transform_api_fn = int(__cdecl *)(void *, void *);

        struct NativeVector3
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        using get_entity_rotation_fn = NativeVector3(__cdecl *)(uint32_t);
        using set_entity_rotation_fn = void(__cdecl *)(uint32_t, NativeVector3);
        constexpr std::wstring_view kScriptBridgeType = L"PlutoGE.ScriptCore.Native.ScriptBridge, PlutoGE.ScriptCore";
        constexpr std::wstring_view kScriptCoreAssembly = L"PlutoGE.ScriptCore.dll";
        constexpr std::wstring_view kScriptCoreRuntimeConfig = L"PlutoGE.ScriptCore.runtimeconfig.json";

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
            candidates.push_back(assemblyPath.parent_path());

            if (const auto envOverride = GetEnvironmentVariableText(L"PLUTOGE_SCRIPTCORE_DIR"))
            {
                candidates.emplace_back(*envOverride);
            }

            auto current = std::filesystem::current_path();
            candidates.push_back(current);

            while (!current.empty())
            {
                candidates.push_back(current / "engine" / "scripting" / "managed" / "PlutoGE.ScriptCore" / "bin" / "Debug" / "net8.0");
                candidates.push_back(current / "engine" / "scripting" / "managed" / "PlutoGE.ScriptCore" / "bin" / "Release" / "net8.0");

                if (current == current.root_path())
                {
                    break;
                }

                current = current.parent_path();
            }

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
            if (fieldTypeValue < static_cast<int>(ScriptFieldType::None) || fieldTypeValue > static_cast<int>(ScriptFieldType::EntityId))
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

        std::string SerializeFieldData(const std::unordered_map<std::string, ScriptFieldValue> &fieldValues)
        {
            std::ostringstream stream;
            for (const auto &[fieldName, fieldValue] : fieldValues)
            {
                const auto fieldType = GetFieldTypeForValue(fieldValue);
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
                        if (tokens[0] == "CLASS" && tokens.size() >= 4)
                        {
                            if (currentClass)
                            {
                                classes.push_back(*currentClass);
                            }

                            currentClass = ScriptClassDefinition{};
                            currentClass->assemblyName = tokens[1];
                            currentClass->namespaceName = tokens[2];
                            currentClass->className = tokens[3];
                        }
                        else if (tokens[0] == "FIELD" && tokens.size() >= 5 && currentClass)
                        {
                            ScriptFieldDefinition fieldDefinition;
                            fieldDefinition.name = tokens[1];
                            fieldDefinition.type = ParseFieldType(tokens[2]);
                            fieldDefinition.serialized = tokens[3] == "1";
                            fieldDefinition.defaultValue = ParseFieldValue(fieldDefinition.type, tokens[4]);
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

        NativeVector3 GetEntityRotation(uint32_t entityId)
        {
            const auto *scene = core::Engine::GetInstance().GetScene();
            if (!scene)
            {
                return {};
            }

            auto *entity = scene->FindEntityByID(entityId);
            if (!entity)
            {
                return {};
            }

            const auto rotation = entity->GetRotation();
            return NativeVector3{rotation.x, rotation.y, rotation.z};
        }

        void SetEntityRotation(uint32_t entityId, NativeVector3 rotation)
        {
            auto *scene = core::Engine::GetInstance().GetScene();
            if (!scene)
            {
                return;
            }

            auto *entity = scene->FindEntityByID(entityId);
            if (!entity)
            {
                return;
            }

            entity->SetRotation(glm::vec3(rotation.x, rotation.y, rotation.z));
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
        get_marshaled_string_fn getScriptMetadata = nullptr;
        free_marshaled_string_fn freeMarshaledString = nullptr;
        get_marshaled_string_fn getLastError = nullptr;
        create_script_instance_fn createScriptInstance = nullptr;
        destroy_script_instance_fn destroyScriptInstance = nullptr;
        invoke_on_create_fn invokeOnCreate = nullptr;
        invoke_on_update_fn invokeOnUpdate = nullptr;
        apply_field_data_fn applyFieldData = nullptr;
        set_entity_id_fn setEntityId = nullptr;
        register_entity_transform_api_fn registerEntityTransformApi = nullptr;
        std::filesystem::path bridgeAssemblyPath;
        std::filesystem::path runtimeConfigPath;
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

        template <typename DelegateType>
        bool LoadManagedExport(HostFxrScriptRuntime::Impl &impl, const wchar_t *methodName, DelegateType &delegate)
        {
            void *functionPointer = nullptr;
            const int result = impl.loadAssemblyAndGetFunctionPointer(
                impl.bridgeAssemblyPath.c_str(),
                kScriptBridgeType.data(),
                methodName,
                nullptr,
                nullptr,
                &functionPointer);

            if (result != 0 || !functionPointer)
            {
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
            if (impl.loadAssemblyAndGetFunctionPointer && impl.bridgeAssemblyPath == bridgeAssemblyPath && impl.runtimeConfigPath == runtimeConfigPath)
            {
                return true;
            }

            impl.bridgeAssemblyPath = bridgeAssemblyPath;
            impl.runtimeConfigPath = runtimeConfigPath;

            hostfxr_handle hostContext = nullptr;
            const int initializeResult = impl.initializeForRuntimeConfig(runtimeConfigPath.c_str(), nullptr, &hostContext);
            if (initializeResult != 0 || !hostContext)
            {
                impl.lastError = "hostfxr failed to initialize the managed runtime";
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

            return LoadManagedExport(impl, L"LoadScriptAssembly", impl.loadScriptAssembly) &&
                   LoadManagedExport(impl, L"GetScriptMetadata", impl.getScriptMetadata) &&
                   LoadManagedExport(impl, L"FreeNativeString", impl.freeMarshaledString) &&
                   LoadManagedExport(impl, L"GetLastError", impl.getLastError) &&
                   LoadManagedExport(impl, L"CreateScriptInstance", impl.createScriptInstance) &&
                   LoadManagedExport(impl, L"DestroyScriptInstance", impl.destroyScriptInstance) &&
                   LoadManagedExport(impl, L"InvokeOnCreate", impl.invokeOnCreate) &&
                   LoadManagedExport(impl, L"InvokeOnUpdate", impl.invokeOnUpdate) &&
                   LoadManagedExport(impl, L"ApplyFieldData", impl.applyFieldData) &&
                   LoadManagedExport(impl, L"SetEntityId", impl.setEntityId) &&
                   LoadManagedExport(impl, L"RegisterEntityTransformApi", impl.registerEntityTransformApi);
        }

        class ManagedScriptInstance final : public ScriptInstance
        {
        public:
            ManagedScriptInstance(std::shared_ptr<HostFxrScriptRuntime::Impl> impl, int64_t instanceHandle)
                : m_impl(std::move(impl)), m_instanceHandle(instanceHandle)
            {
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

            void ApplyFieldValues(const std::unordered_map<std::string, ScriptFieldValue> &fieldValues) override
            {
                ScriptInstance::ApplyFieldValues(fieldValues);

                if (!m_impl || !m_impl->applyFieldData)
                {
                    return;
                }

                const std::string wireData = SerializeFieldData(fieldValues);
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
        };
#endif
    }

    HostFxrScriptRuntime::HostFxrScriptRuntime()
        : m_impl(std::make_shared<Impl>())
    {
    }

    HostFxrScriptRuntime::~HostFxrScriptRuntime() = default;

    bool HostFxrScriptRuntime::LoadAssembly(const std::filesystem::path &assemblyPath)
    {
#ifdef _WIN32
        m_impl->scriptClasses.clear();
        m_impl->loaded = false;
        m_impl->lastError.clear();

        if (!EnsureManagedBridgeLoaded(*m_impl, assemblyPath))
        {
            return false;
        }

        if (!m_impl->registerEntityTransformApi ||
            m_impl->registerEntityTransformApi(
                reinterpret_cast<void *>(static_cast<get_entity_rotation_fn>(&GetEntityRotation)),
                reinterpret_cast<void *>(static_cast<set_entity_rotation_fn>(&SetEntityRotation))) == 0)
        {
            m_impl->lastError = TakeManagedString(*m_impl, m_impl->getLastError);
            return false;
        }

        const std::string assemblyPathUtf8 = WideToUtf8(std::filesystem::absolute(assemblyPath).wstring());
        if (!m_impl->loadScriptAssembly || m_impl->loadScriptAssembly(assemblyPathUtf8.c_str()) == 0)
        {
            m_impl->lastError = TakeManagedString(*m_impl, m_impl->getLastError);
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

        return std::make_unique<ManagedScriptInstance>(m_impl, instanceHandle);
#else
        (void)scriptClass;
        return nullptr;
#endif
    }
}