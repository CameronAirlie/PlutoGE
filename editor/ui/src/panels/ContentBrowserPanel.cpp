#include "PlutoGE/ui/panels/ContentBrowserPanel.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/assets/AssetDatabase.h"
#include "PlutoGE/assets/ModelAsset.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/import/MeshImporter.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/ShaderGraph.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scripting/ScriptEngine.h"
#include "PlutoGE/ui/EditorShell.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <sstream>

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace PlutoGE::ui
{
    class AssetThumbnailCache
    {
    public:
        ~AssetThumbnailCache()
        {
            for (const auto &[reference, entry] : m_entries)
            {
                (void)reference;
                if (entry.texture != 0) glDeleteTextures(1, &entry.texture);
            }
            if (m_depthBuffer != 0) glDeleteRenderbuffers(1, &m_depthBuffer);
            if (m_framebuffer != 0) glDeleteFramebuffers(1, &m_framebuffer);
            if (m_program != 0) glDeleteProgram(m_program);
        }

        void BeginFrame()
        {
            m_generationBudget = 2;
        }

        GLuint Get(const assets::Project &project, core::Engine &engine, const assets::ProjectAssetEntry &asset)
        {
            if (asset.type == assets::ProjectAssetType::Texture)
            {
                const auto path = project.ResolveAssetReference(asset.reference).string();
                auto *texture = engine.GetAssetManager().LoadTexture(path.c_str());
                return texture ? texture->GetTextureID() : 0;
            }

            std::string renderReference = asset.reference;
            if (asset.type == assets::ProjectAssetType::Model)
            {
                const auto sourcePath = project.ResolveAssetReference(asset.reference);
                const auto manifestPath = project.GetAssetDirectoryPath() / "Imported" / sourcePath.stem() /
                                          (sourcePath.stem().string() + ".plutomodel");
                assets::ModelAsset model;
                if (!assets::LoadModelAsset(manifestPath.string(), model)) return 0;
                const auto mesh = std::find_if(model.objects.begin(), model.objects.end(), [](const auto &object)
                                               { return object.type == assets::ProjectAssetType::Mesh; });
                if (mesh == model.objects.end()) return 0;
                renderReference = mesh->reference;
            }

            if (asset.type != assets::ProjectAssetType::Model &&
                asset.type != assets::ProjectAssetType::Mesh &&
                asset.type != assets::ProjectAssetType::Material)
            {
                return 0;
            }

            const auto stamp = GetStamp(project, asset.reference);
            const auto found = m_entries.find(asset.reference);
            if (found != m_entries.end() && found->second.stamp == stamp)
            {
                return found->second.texture;
            }
            if (m_generationBudget <= 0) return 0;
            --m_generationBudget;

            render::Mesh *mesh = nullptr;
            render::Material *material = nullptr;
            if (asset.type == assets::ProjectAssetType::Material)
            {
                material = engine.GetAssetManager().LoadMaterialAsset(asset.reference);
                mesh = engine.GetAssetManager().LoadMeshAsset(std::string(assets::Project::kBuiltinSphereMeshReference));
            }
            else
            {
                mesh = engine.GetAssetManager().LoadMeshAsset(renderReference);
                const auto &materials = engine.GetAssetManager().GetMeshAssetMaterialReferences(renderReference);
                if (!materials.empty()) material = engine.GetAssetManager().LoadMaterialAsset(materials.front());
            }
            if (!mesh) return 0;

            auto &entry = m_entries[asset.reference];
            if (entry.texture == 0) entry.texture = CreateTexture();
            if (!Render(entry.texture, *mesh, material)) return 0;
            entry.stamp = stamp;
            return entry.texture;
        }

        void Clear()
        {
            for (const auto &[reference, entry] : m_entries)
            {
                (void)reference;
                if (entry.texture != 0) glDeleteTextures(1, &entry.texture);
            }
            m_entries.clear();
        }

    private:
        struct Entry
        {
            GLuint texture = 0;
            std::uint64_t stamp = 0;
        };

        static constexpr int kSize = 128;
        std::unordered_map<std::string, Entry> m_entries;
        GLuint m_framebuffer = 0;
        GLuint m_depthBuffer = 0;
        GLuint m_program = 0;
        int m_generationBudget = 0;

        static std::uint64_t GetStamp(const assets::Project &project, const std::string &reference)
        {
            std::error_code error;
            const auto time = std::filesystem::last_write_time(project.ResolveAssetReference(reference), error);
            return error ? 0 : static_cast<std::uint64_t>(time.time_since_epoch().count());
        }

        static GLuint Compile(GLenum type, const char *source)
        {
            const GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);
            GLint compiled = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
            if (compiled == GL_FALSE)
            {
                glDeleteShader(shader);
                return 0;
            }
            return shader;
        }

        bool Initialize()
        {
            if (m_program != 0) return true;
            constexpr const char *vertexSource = R"GLSL(#version 450 core
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
uniform mat4 mvp;
uniform mat4 model;
out vec3 worldNormal;
out vec2 texCoord;
void main() {
    gl_Position = mvp * vec4(position, 1.0);
    worldNormal = mat3(transpose(inverse(model))) * normal;
    texCoord = uv;
})GLSL";
            constexpr const char *fragmentSource = R"GLSL(#version 450 core
in vec3 worldNormal;
in vec2 texCoord;
layout(location=0) out vec4 colorOut;
uniform vec4 baseColor;
uniform sampler2D albedo;
uniform bool useAlbedo;
void main() {
    vec3 n = normalize(worldNormal);
    vec3 lightDirection = normalize(vec3(0.5, 0.8, 0.7));
    float diffuse = max(dot(n, lightDirection), 0.0);
    float rim = pow(1.0 - max(n.z, 0.0), 3.0) * 0.18;
    vec4 surface = baseColor * (useAlbedo ? texture(albedo, texCoord) : vec4(1.0));
    colorOut = vec4(surface.rgb * (0.28 + diffuse * 0.72) + rim, surface.a);
})GLSL";
            const GLuint vertex = Compile(GL_VERTEX_SHADER, vertexSource);
            const GLuint fragment = Compile(GL_FRAGMENT_SHADER, fragmentSource);
            if (vertex == 0 || fragment == 0) return false;
            m_program = glCreateProgram();
            glAttachShader(m_program, vertex);
            glAttachShader(m_program, fragment);
            glLinkProgram(m_program);
            glDeleteShader(vertex);
            glDeleteShader(fragment);
            GLint linked = GL_FALSE;
            glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
            if (linked == GL_FALSE) return false;

            glGenFramebuffers(1, &m_framebuffer);
            glGenRenderbuffers(1, &m_depthBuffer);
            glBindRenderbuffer(GL_RENDERBUFFER, m_depthBuffer);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, kSize, kSize);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
            return true;
        }

        static GLuint CreateTexture()
        {
            GLuint texture = 0;
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            return texture;
        }

        bool Render(GLuint target, render::Mesh &mesh, render::Material *material)
        {
            if (!Initialize()) return false;
            GLint previousFramebuffer = 0, previousProgram = 0, previousVao = 0;
            GLint previousActiveTexture = 0, previousTexture = 0, previousCullMode = 0;
            GLint previousViewport[4]{};
            GLfloat previousClearColor[4]{};
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousFramebuffer);
            glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
            glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
            glActiveTexture(GL_TEXTURE0);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
            glGetIntegerv(GL_CULL_FACE_MODE, &previousCullMode);
            glGetIntegerv(GL_VIEWPORT, previousViewport);
            glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);
            const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
            const GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);

            glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_depthBuffer);
            glViewport(0, 0, kSize, kSize);
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            glClearColor(0.105f, 0.115f, 0.13f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            const auto bounds = mesh.GetBounds();
            const float radius = std::max(bounds.radius, 0.001f);
            glm::mat4 model(1.0f);
            model = glm::rotate(model, glm::radians(-18.0f), glm::vec3(1, 0, 0));
            model = glm::rotate(model, glm::radians(32.0f), glm::vec3(0, 1, 0));
            model = glm::scale(model, glm::vec3(0.78f / radius));
            model = glm::translate(model, -bounds.center);
            const glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 2.4f), glm::vec3(0), glm::vec3(0, 1, 0));
            const glm::mat4 projection = glm::perspective(glm::radians(32.0f), 1.0f, 0.01f, 10.0f);
            const glm::mat4 mvp = projection * view * model;

            glUseProgram(m_program);
            glUniformMatrix4fv(glGetUniformLocation(m_program, "mvp"), 1, GL_FALSE, glm::value_ptr(mvp));
            glUniformMatrix4fv(glGetUniformLocation(m_program, "model"), 1, GL_FALSE, glm::value_ptr(model));
            const auto config = material ? material->GetConfig() : render::MaterialConfig{};
            glUniform4fv(glGetUniformLocation(m_program, "baseColor"), 1, glm::value_ptr(config.color));
            const bool useAlbedo = config.albedoTexture && config.albedoTexture->GetTextureID() != 0;
            glUniform1i(glGetUniformLocation(m_program, "useAlbedo"), useAlbedo ? 1 : 0);
            glUniform1i(glGetUniformLocation(m_program, "albedo"), 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, useAlbedo ? config.albedoTexture->GetTextureID() : 0);
            mesh.Draw();

            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
            glUseProgram(static_cast<GLuint>(previousProgram));
            glBindVertexArray(static_cast<GLuint>(previousVao));
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
            glActiveTexture(static_cast<GLenum>(previousActiveTexture));
            glCullFace(static_cast<GLenum>(previousCullMode));
            glClearColor(previousClearColor[0], previousClearColor[1], previousClearColor[2], previousClearColor[3]);
            glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
            if (!depthEnabled) glDisable(GL_DEPTH_TEST);
            if (!cullEnabled) glDisable(GL_CULL_FACE);
            return true;
        }
    };

    ContentBrowserPanel::ContentBrowserPanel(const PanelConfig &config) : Panel(config) {}
    ContentBrowserPanel::~ContentBrowserPanel() = default;

    namespace
    {
        bool ContainsInsensitive(std::string_view text, std::string_view filter)
        {
            if (filter.empty())
            {
                return true;
            }

            auto lower = [](unsigned char value)
            {
                return static_cast<char>(std::tolower(value));
            };

            std::string haystack(text);
            std::string needle(filter);
            std::transform(haystack.begin(), haystack.end(), haystack.begin(), lower);
            std::transform(needle.begin(), needle.end(), needle.begin(), lower);
            return haystack.find(needle) != std::string::npos;
        }

        std::string DisplayAssetReference(std::string reference)
        {
            if (reference.rfind(assets::Project::kProjectAssetScheme, 0) == 0)
            {
                reference.erase(0, assets::Project::kProjectAssetScheme.size());
            }
            return reference;
        }

        namespace
        {
            std::string PathToGenericUtf8String(const std::filesystem::path &path)
            {
#if defined(__cpp_char8_t)
                // C++20: generic_u8string() returns std::u8string
                std::u8string u8 = path.generic_u8string();
                return std::string(reinterpret_cast<const char *>(u8.data()), u8.size());
#else
                // C++17: generic_u8string() returns std::string
                return path.generic_u8string();
#endif
            }
        }

        std::string NormalizeAssetRelativePath(const std::filesystem::path &path)
        {
            std::filesystem::path normalizedPath;

            try
            {
                normalizedPath = path.lexically_normal();
            }
            catch (const std::exception &)
            {
                normalizedPath = path;
            }

            std::string normalized = PathToGenericUtf8String(normalizedPath);

            // generic_u8string already uses '/', but this is harmless.
            std::replace(normalized.begin(), normalized.end(), '\\', '/');

            if (normalized == ".")
            {
                return {};
            }

            while (!normalized.empty() && normalized.front() == '/')
            {
                normalized.erase(normalized.begin());
            }

            while (!normalized.empty() && normalized.back() == '/')
            {
                normalized.pop_back();
            }

            return normalized;
        }

        std::string GetAssetFolderParent(std::string_view folder)
        {
            std::string normalized(folder);
            std::replace(normalized.begin(), normalized.end(), '\\', '/');
            while (!normalized.empty() && normalized.front() == '/')
            {
                normalized.erase(normalized.begin());
            }
            while (!normalized.empty() && normalized.back() == '/')
            {
                normalized.pop_back();
            }

            const auto separator = normalized.find_last_of('/');
            if (separator == std::string::npos)
            {
                return {};
            }
            return normalized.substr(0, separator);
        }

        std::string GetReferenceRelativePath(const assets::Project &project, const assets::ProjectAssetEntry &asset)
        {
            if (assets::Project::IsProjectAssetReference(asset.reference))
            {
                return NormalizeAssetRelativePath(DisplayAssetReference(asset.reference));
            }

            const auto resolvedPath = project.ResolveAssetReference(asset.reference);
            if (!resolvedPath.empty() && project.IsInAssetDirectory(resolvedPath))
            {
                std::error_code errorCode;
                return NormalizeAssetRelativePath(std::filesystem::relative(resolvedPath, project.GetAssetDirectoryPath(), errorCode));
            }

            return NormalizeAssetRelativePath(DisplayAssetReference(asset.reference));
        }

        std::string GetReferenceFolder(const assets::Project &project, const assets::ProjectAssetEntry &asset)
        {
            return GetAssetFolderParent(GetReferenceRelativePath(project, asset));
        }

        std::string GetReferenceFileName(const assets::Project &project, const assets::ProjectAssetEntry &asset)
        {
            const std::string relativePath = GetReferenceRelativePath(project, asset);
            const auto separator = relativePath.find_last_of('/');
            const auto fileName = separator == std::string::npos ? relativePath : relativePath.substr(separator + 1);
            return fileName.empty() ? DisplayAssetReference(asset.reference) : fileName;
        }

        std::filesystem::path GetImportedModelManifestPath(const assets::Project &project, std::string_view sourceReference)
        {
            const auto sourcePath = project.ResolveAssetReference(sourceReference);
            return project.GetAssetDirectoryPath() / "Imported" / sourcePath.stem() /
                   (sourcePath.stem().string() + ".plutomodel");
        }

        std::string GetAssetFolderName(std::string_view folder)
        {
            const auto separator = folder.find_last_of('/');
            if (separator == std::string::npos)
            {
                return std::string(folder);
            }
            return std::string(folder.substr(separator + 1));
        }

        bool IsFolderAncestorOrSelf(std::string_view ancestor, std::string_view folder)
        {
            if (ancestor.empty())
            {
                return true;
            }
            if (folder == ancestor)
            {
                return true;
            }
            return folder.size() > ancestor.size() &&
                   folder.compare(0, ancestor.size(), ancestor) == 0 &&
                   folder[ancestor.size()] == '/';
        }

        std::filesystem::path GetCreateDirectory(const assets::Project &project, std::string_view selectedFolder, std::string_view fallbackFolder)
        {
            if (!selectedFolder.empty())
            {
                return project.GetAssetDirectoryPath() / std::filesystem::path(std::string(selectedFolder));
            }
            return project.GetAssetDirectoryPath() / std::filesystem::path(std::string(fallbackFolder));
        }

        std::string DisplayCreatePath(const assets::Project &project, const std::filesystem::path &path, std::string_view fileName)
        {
            std::error_code errorCode;
            auto relativePath = std::filesystem::relative(path / std::filesystem::path(std::string(fileName)), project.GetAssetDirectoryPath(), errorCode);
            return NormalizeAssetRelativePath(errorCode ? path.filename() / std::filesystem::path(std::string(fileName)) : relativePath);
        }

        void OpenAsset(EditorShell &editorShell, const assets::Project &project, const assets::ProjectAssetEntry &asset)
        {
            const auto resolvedPath = project.ResolveAssetReference(asset.reference);
            if (resolvedPath.extension() == ".plutoscene")
            {
                editorShell.OpenSceneFromPath(resolvedPath);
            }
            else if (resolvedPath.extension() == ".plutoprefab")
            {
                editorShell.OpenSceneFromPath(resolvedPath);
            }
            else if (asset.type == assets::ProjectAssetType::Material)
            {
                editorShell.OpenMaterialAsset(asset.reference);
            }
            else if (asset.type == assets::ProjectAssetType::Mesh)
            {
                editorShell.OpenMeshAsset(asset.reference);
            }
            else if (asset.type == assets::ProjectAssetType::ShaderGraph)
            {
                editorShell.OpenShaderGraphAsset(asset.reference);
            }
            else if (asset.type == assets::ProjectAssetType::AnimationGraph)
            {
                editorShell.OpenAnimationGraphAsset(asset.reference);
            }
            else if (asset.type == assets::ProjectAssetType::ParticleSystem)
            {
                editorShell.OpenParticleSystemAsset(asset.reference);
            }
        }

        std::string SanitizeAssetFileName(std::string_view text)
        {
            std::string name;
            name.reserve(text.size());
            for (const char rawCharacter : text)
            {
                const unsigned char character = static_cast<unsigned char>(rawCharacter);
                if (std::isalnum(character) != 0 || rawCharacter == '_' || rawCharacter == '-')
                {
                    name.push_back(rawCharacter);
                }
                else if (!name.empty() && name.back() != '_')
                {
                    name.push_back('_');
                }
            }

            while (!name.empty() && name.back() == '_')
            {
                name.pop_back();
            }
            return name;
        }

        std::string EscapeScriptableText(std::string_view text)
        {
            std::string result;
            for (const char character : text)
            {
                switch (character)
                {
                case '\\': result += "\\\\"; break;
                case '\t': result += "\\t"; break;
                case '\n': result += "\\n"; break;
                default: result += character; break;
                }
            }
            return result;
        }

        std::vector<std::string> SplitScriptableLine(std::string_view line)
        {
            std::vector<std::string> tokens(1);
            bool escaping = false;
            for (const char character : line)
            {
                if (escaping)
                {
                    tokens.back() += character == 't' ? '\t' : character == 'n' ? '\n' : character;
                    escaping = false;
                }
                else if (character == '\\')
                {
                    escaping = true;
                }
                else if (character == '\t')
                {
                    tokens.emplace_back();
                }
                else
                {
                    tokens.back() += character;
                }
            }
            return tokens;
        }

        std::string SerializeScriptableValue(const scripting::ScriptFieldValue &value)
        {
            return std::visit([](const auto &typedValue) -> std::string
            {
                using T = std::decay_t<decltype(typedValue)>;
                if constexpr (std::is_same_v<T, std::monostate>) return {};
                else if constexpr (std::is_same_v<T, bool>) return typedValue ? "true" : "false";
                else if constexpr (std::is_same_v<T, std::string>) return typedValue;
                else if constexpr (std::is_same_v<T, glm::vec2>) return std::to_string(typedValue.x) + "," + std::to_string(typedValue.y);
                else if constexpr (std::is_same_v<T, glm::vec3>) return std::to_string(typedValue.x) + "," + std::to_string(typedValue.y) + "," + std::to_string(typedValue.z);
                else return std::to_string(typedValue);
            }, value);
        }

        scripting::ScriptFieldValue ParseScriptableValue(scripting::ScriptFieldType type, const std::string &text)
        {
            try
            {
                switch (type)
                {
                case scripting::ScriptFieldType::Boolean: return text == "true" || text == "1";
                case scripting::ScriptFieldType::Int32: return static_cast<int32_t>(std::stoi(text));
                case scripting::ScriptFieldType::Float: return std::stof(text);
                case scripting::ScriptFieldType::Double: return std::stod(text);
                case scripting::ScriptFieldType::String:
                case scripting::ScriptFieldType::PrefabAsset:
                case scripting::ScriptFieldType::ScriptableObjectAsset: return text;
                case scripting::ScriptFieldType::Vector2:
                {
                    glm::vec2 value{};
                    char comma{};
                    std::istringstream(text) >> value.x >> comma >> value.y;
                    return value;
                }
                case scripting::ScriptFieldType::Vector3:
                {
                    glm::vec3 value{};
                    char comma1{}, comma2{};
                    std::istringstream(text) >> value.x >> comma1 >> value.y >> comma2 >> value.z;
                    return value;
                }
                default: return scripting::MakeDefaultFieldValue(type);
                }
            }
            catch (...)
            {
                return scripting::MakeDefaultFieldValue(type);
            }
        }

        bool SaveScriptableObjectAsset(const std::filesystem::path &path,
                                       std::string_view className,
                                       const std::vector<scripting::ScriptFieldDefinition> &fields,
                                       const std::unordered_map<std::string, scripting::ScriptFieldValue> &values)
        {
            std::error_code errorCode;
            std::filesystem::create_directories(path.parent_path(), errorCode);
            std::ofstream output(path, std::ios::out | std::ios::trunc);
            if (!output.is_open()) return false;
            output << "SCRIPTABLE\t" << EscapeScriptableText(className) << '\n';
            for (const auto &field : fields)
            {
                const auto iterator = values.find(field.name);
                const auto &value = iterator != values.end() ? iterator->second : field.defaultValue;
                output << "FIELD\t" << EscapeScriptableText(field.name) << '\t'
                       << static_cast<int>(field.type) << '\t'
                       << EscapeScriptableText(SerializeScriptableValue(value)) << '\n';
            }
            return output.good();
        }

        bool LoadScriptableObjectAsset(const std::filesystem::path &path,
                                       std::string &className,
                                       std::unordered_map<std::string, scripting::ScriptFieldValue> &values)
        {
            std::ifstream input(path);
            std::string line;
            if (!std::getline(input, line)) return false;
            auto header = SplitScriptableLine(line);
            if (header.size() < 2 || header[0] != "SCRIPTABLE") return false;
            className = header[1];
            values.clear();
            while (std::getline(input, line))
            {
                auto tokens = SplitScriptableLine(line);
                if (tokens.size() >= 4 && tokens[0] == "FIELD")
                {
                    values[tokens[1]] = ParseScriptableValue(static_cast<scripting::ScriptFieldType>(std::stoi(tokens[2])), tokens[3]);
                }
            }
            return true;
        }

        std::string BuildEntityNameForMeshReference(const std::string &reference)
        {
            std::string displayName = DisplayAssetReference(reference);
            if (displayName.rfind("engine://", 0) == 0)
            {
                const auto separator = displayName.find_last_of('/');
                return separator == std::string::npos ? displayName : displayName.substr(separator + 1);
            }

            std::filesystem::path path(displayName);
            const auto stem = path.stem().string();
            return stem.empty() ? displayName : stem;
        }

        bool IsSupportedImportedModelReference(const std::string &reference)
        {
            const auto extension = std::filesystem::path(reference).extension().string();
            return extension == ".gltf" || extension == ".glb" || extension == ".fbx" ||
                   extension == ".GLTF" || extension == ".GLB" || extension == ".FBX";
        }

        std::string BrowseSourceModelPath()
        {
#ifdef _WIN32
            OPENFILENAMEA openFileName{};
            char fileName[MAX_PATH] = "";
            openFileName.lStructSize = sizeof(openFileName);
            openFileName.hwndOwner = nullptr;
            openFileName.lpstrFilter = "Model Files\0*.gltf;*.glb;*.fbx\0glTF Files\0*.gltf;*.glb\0FBX Files\0*.fbx\0All Files\0*.*\0";
            openFileName.lpstrFile = fileName;
            openFileName.nMaxFile = MAX_PATH;
            openFileName.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (!GetOpenFileNameA(&openFileName))
            {
                return {};
            }
            return std::filesystem::path(fileName).lexically_normal().string();
#else
            return {};
#endif
        }

        bool CopySourceModelPackageToAssets(const assets::Project &project,
                                            const std::filesystem::path &sourcePath,
                                            std::filesystem::path &importedSourcePath,
                                            std::string *errorMessage)
        {
            std::error_code errorCode;
            const auto normalizedSourcePath = std::filesystem::weakly_canonical(sourcePath, errorCode);
            const auto sourceFilePath = errorCode ? sourcePath.lexically_normal() : normalizedSourcePath;
            if (!std::filesystem::exists(sourceFilePath))
            {
                if (errorMessage)
                {
                    *errorMessage = "Selected model does not exist.";
                }
                return false;
            }

            if (!IsSupportedImportedModelReference(sourceFilePath.string()))
            {
                if (errorMessage)
                {
                    *errorMessage = "Selected file is not a supported source model. Use .gltf, .glb, or .fbx.";
                }
                return false;
            }

            if (project.IsInAssetDirectory(sourceFilePath))
            {
                importedSourcePath = sourceFilePath;
                return true;
            }

            std::string packageName = SanitizeAssetFileName(sourceFilePath.stem().string());
            if (packageName.empty())
            {
                packageName = "ImportedModel";
            }
            const auto packageDirectory = project.GetAssetDirectoryPath() / "SourceModels" / packageName;
            std::filesystem::create_directories(packageDirectory, errorCode);
            if (errorCode)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to create source model directory: " + errorCode.message();
                }
                return false;
            }

            const auto sourceParent = sourceFilePath.parent_path();
            for (std::filesystem::recursive_directory_iterator iterator(sourceParent, errorCode), end; iterator != end && !errorCode; iterator.increment(errorCode))
            {
                const auto &entry = *iterator;
                if (!entry.is_regular_file(errorCode) || errorCode)
                {
                    errorCode.clear();
                    continue;
                }

                const auto relativePath = std::filesystem::relative(entry.path(), sourceParent, errorCode);
                if (errorCode)
                {
                    break;
                }

                const auto destinationPath = packageDirectory / relativePath;
                std::filesystem::create_directories(destinationPath.parent_path(), errorCode);
                if (errorCode)
                {
                    break;
                }

                std::filesystem::copy_file(entry.path(), destinationPath, std::filesystem::copy_options::overwrite_existing, errorCode);
                if (errorCode)
                {
                    break;
                }
            }

            if (errorCode)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to copy source model package: " + errorCode.message();
                }
                return false;
            }

            importedSourcePath = packageDirectory / sourceFilePath.filename();
            const auto sourceSize = std::filesystem::file_size(sourceFilePath, errorCode);
            if (errorCode)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to read selected model size: " + errorCode.message();
                }
                return false;
            }

            const auto importedSize = std::filesystem::file_size(importedSourcePath, errorCode);
            if (errorCode)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to read copied model size: " + errorCode.message();
                }
                return false;
            }

            if (importedSize == 0 || importedSize != sourceSize)
            {
                if (errorMessage)
                {
                    *errorMessage = "Copied model file is incomplete. Source size=" + std::to_string(sourceSize) +
                                    " bytes, copied size=" + std::to_string(importedSize) + " bytes.";
                }
                return false;
            }
            return true;
        }

        bool WriteTextureTga(const std::filesystem::path &path,
                             const assetimport::ImportedTextureData &texture,
                             std::string *errorMessage)
        {
            if (texture.width <= 0 || texture.height <= 0 || texture.channels <= 0 || texture.pixels.empty())
            {
                if (errorMessage)
                {
                    *errorMessage = "Imported texture has no pixel data.";
                }
                return false;
            }

            std::error_code errorCode;
            std::filesystem::create_directories(path.parent_path(), errorCode);
            if (errorCode)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to create texture directory: " + errorCode.message();
                }
                return false;
            }

            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output.is_open())
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to write texture asset: " + path.string();
                }
                return false;
            }

            const unsigned char header[18] = {
                0,
                0,
                2,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                static_cast<unsigned char>(texture.width & 0xff),
                static_cast<unsigned char>((texture.width >> 8) & 0xff),
                static_cast<unsigned char>(texture.height & 0xff),
                static_cast<unsigned char>((texture.height >> 8) & 0xff),
                32,
                0x20 | 0x08,
            };
            output.write(reinterpret_cast<const char *>(header), sizeof(header));

            const std::size_t sourceChannels = static_cast<std::size_t>(texture.channels);
            const std::size_t pixelCount = static_cast<std::size_t>(texture.width) * static_cast<std::size_t>(texture.height);
            for (std::size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
            {
                const std::size_t sourceOffset = pixelIndex * sourceChannels;
                const unsigned char red = texture.pixels[sourceOffset + 0];
                const unsigned char green = sourceChannels > 1 ? texture.pixels[sourceOffset + 1] : red;
                const unsigned char blue = sourceChannels > 2 ? texture.pixels[sourceOffset + 2] : red;
                const unsigned char alpha = sourceChannels > 3 ? texture.pixels[sourceOffset + 3] : 255;
                const unsigned char bgra[4] = {blue, green, red, alpha};
                output.write(reinterpret_cast<const char *>(bgra), sizeof(bgra));
            }

            return output.good();
        }

        std::string SanitizeImportedAssetName(std::string text)
        {
            for (auto &character : text)
            {
                const unsigned char value = static_cast<unsigned char>(character);
                if (std::isalnum(value) == 0 && character != '_' && character != '-')
                {
                    character = '_';
                }
            }
            return text.empty() ? "Asset" : text;
        }

        std::string ImportTextureAsset(const assets::Project &project,
                                       const std::filesystem::path &importDirectory,
                                       const assetimport::ImportedTextureData &texture,
                                       int textureIndex,
                                       std::string *errorMessage)
        {
            const auto textureDirectory = importDirectory / "Textures";
            if (!texture.sourcePath.empty() && std::filesystem::exists(texture.sourcePath))
            {
                const auto sourcePath = std::filesystem::path(texture.sourcePath);
                const auto destinationPath = textureDirectory / sourcePath.filename();
                std::error_code errorCode;
                std::filesystem::create_directories(destinationPath.parent_path(), errorCode);
                if (!errorCode)
                {
                    std::filesystem::copy_file(sourcePath, destinationPath, std::filesystem::copy_options::overwrite_existing, errorCode);
                }
                if (errorCode)
                {
                    if (errorMessage)
                    {
                        *errorMessage = "Failed to copy imported texture: " + errorCode.message();
                    }
                    return {};
                }
                return project.MakeAssetReference(destinationPath);
            }

            const auto texturePath = textureDirectory / ("T_" + std::to_string(textureIndex) + ".tga");
            if (!WriteTextureTga(texturePath, texture, errorMessage))
            {
                return {};
            }
            return project.MakeAssetReference(texturePath);
        }

        render::MaterialConfig BuildGeneratedMaterialConfig(const assetimport::ImportedMaterialData &material,
                                                            const std::vector<std::string> &textureReferences,
                                                            std::deque<render::Texture> &textureHandles)
        {
            render::MaterialConfig config;
            config.color = material.color;
            config.surfaceType = material.surfaceType;
            config.alphaMode = material.alphaMode;
            config.alphaCutoff = material.alphaCutoff;
            config.castsShadow = material.castsShadow;
            config.metallic = material.metallic;
            config.roughness = material.roughness;
            config.emission = material.emission;
            config.transmission = material.transmission;
            config.ior = material.ior;
            config.thickness = material.thickness;
            config.attenuationColor = material.attenuationColor;
            config.attenuationDistance = material.attenuationDistance;
            config.flipNormalY = material.flipNormalY;

            auto assignTexture = [&](int textureIndex) -> render::Texture *
            {
                if (textureIndex < 0 || static_cast<std::size_t>(textureIndex) >= textureReferences.size() || textureReferences[static_cast<std::size_t>(textureIndex)].empty())
                {
                    return nullptr;
                }
                render::TextureConfig textureConfig;
                textureConfig.filePath = textureReferences[static_cast<std::size_t>(textureIndex)];
                textureHandles.emplace_back(textureConfig);
                return &textureHandles.back();
            };

            config.albedoTexture = assignTexture(material.albedoTextureIndex);
            config.normalTexture = assignTexture(material.normalTextureIndex);
            if (auto *packedTexture = assignTexture(material.metallicRoughnessTextureIndex))
            {
                config.roughnessTexture = packedTexture;
                config.roughnessTextureChannel = render::TextureChannel::Green;
                if (material.metallicRoughnessTextureHasMetallicChannel)
                {
                    config.metallicTexture = packedTexture;
                    config.metallicTextureChannel = render::TextureChannel::Blue;
                }
            }
            return config;
        }

        std::vector<render::Material *> LoadMaterialReferences(core::Engine &engine, const std::vector<std::string> &materialReferences)
        {
            std::vector<render::Material *> materials;
            materials.reserve(materialReferences.size());
            for (const auto &materialReference : materialReferences)
            {
                auto *material = materialReference.empty()
                                     ? engine.GetAssetManager().LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference))
                                     : engine.GetAssetManager().LoadMaterialAsset(materialReference);
                materials.push_back(material);
            }
            if (materials.empty())
            {
                materials.push_back(engine.GetAssetManager().LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference)));
            }
            return materials;
        }

        std::string FindSiblingAnimationAssetReference(const assets::Project *project, const std::string &meshReference)
        {
            if (!project || meshReference.empty() || assets::Project::IsEngineAssetReference(meshReference))
            {
                return {};
            }

            const auto meshPath = project->ResolveAssetReference(meshReference);
            const auto expectedAnimationPath = meshPath.parent_path() / (meshPath.stem().string() + ".plutoanim");
            if (std::filesystem::exists(expectedAnimationPath))
            {
                return project->MakeAssetReference(expectedAnimationPath);
            }

            for (const auto &asset : project->GetManifest().assetEntries)
            {
                if (asset.type != assets::ProjectAssetType::Animation)
                {
                    continue;
                }

                const auto animationPath = project->ResolveAssetReference(asset.reference);
                if (animationPath.parent_path() == meshPath.parent_path())
                {
                    return asset.reference;
                }
            }
            return {};
        }

        void AttachAnimationAsset(scene::Entity &entity, const std::string &animationReference)
        {
            if (animationReference.empty())
            {
                return;
            }

            auto *animationComponent = entity.GetComponent<scene::AnimationComponent>();
            if (!animationComponent)
            {
                animationComponent = entity.CreateComponent<scene::AnimationComponent>();
            }

            animationComponent->SetAnimationAssetReference(animationReference);
        }

        void AttachImportedAnimations(scene::Entity &entity,
                                      const std::string &sourceReference,
                                      const core::ImportedRenderMeshAsset &importedMeshAsset)
        {
            if (!importedMeshAsset.animations || importedMeshAsset.animations->empty())
            {
                return;
            }

            auto *animationComponent = entity.GetComponent<scene::AnimationComponent>();
            if (!animationComponent)
            {
                animationComponent = entity.CreateComponent<scene::AnimationComponent>();
            }

            animationComponent->SetClipsFromImportedAnimations(*importedMeshAsset.animations);
            animationComponent->SetSourceAnimationPath(sourceReference);
        }

        std::string MakeClipAssetFileName(const std::filesystem::path &sourcePath, const render::AnimationClip &clip, std::size_t clipIndex)
        {
            std::string clipName = SanitizeAssetFileName(clip.name);
            if (clipName.empty())
            {
                clipName = "Clip_" + std::to_string(clipIndex);
            }
            return sourcePath.stem().string() + "_" + clipName + "_" + std::to_string(clipIndex) + ".plutoclip";
        }

        bool SaveImportedAnimationClips(const assets::Project &project,
                                        const std::filesystem::path &clipDirectory,
                                        const std::filesystem::path &sourcePath,
                                        const std::vector<render::AnimationClip> &clips,
                                        std::vector<std::string> &clipReferences,
                                        std::string *errorMessage)
        {
            auto &assetManager = core::Engine::GetInstance().GetAssetManager();
            for (std::size_t clipIndex = 0; clipIndex < clips.size(); ++clipIndex)
            {
                const auto clipPath = clipDirectory / MakeClipAssetFileName(sourcePath, clips[clipIndex], clipIndex);
                const std::string clipReference = project.MakeAssetReference(clipPath);
                if (!assetManager.SaveAnimationClipAsset(clipReference, clips[clipIndex], errorMessage))
                {
                    return false;
                }
                if (std::find(clipReferences.begin(), clipReferences.end(), clipReference) == clipReferences.end())
                {
                    clipReferences.push_back(clipReference);
                }
            }
            return true;
        }

        bool EnsureAnimationAssetUsesClipReferences(const assets::Project &project,
                                                    const std::string &animationReference,
                                                    std::vector<std::string> &clipReferences,
                                                    std::string *errorMessage)
        {
            auto &assetManager = core::Engine::GetInstance().GetAssetManager();
            if (assetManager.LoadAnimationClipReferences(animationReference, clipReferences))
            {
                return true;
            }

            std::vector<render::AnimationClip> embeddedClips;
            if (!assetManager.LoadAnimationAsset(animationReference, embeddedClips))
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to load animation asset: " + animationReference;
                }
                return false;
            }

            const auto animationPath = project.ResolveAssetReference(animationReference);
            const auto clipDirectory = animationPath.parent_path() / "Clips";
            if (!SaveImportedAnimationClips(project, clipDirectory, animationPath, embeddedClips, clipReferences, errorMessage))
            {
                return false;
            }

            return assetManager.SaveAnimationAssetReferences(animationReference, clipReferences, errorMessage);
        }

        bool AddClipsFromSourceModelToAnimationAsset(const assets::Project &project,
                                                     const std::string &animationReference,
                                                     const std::filesystem::path &sourcePath,
                                                     std::string *errorMessage)
        {
            auto &engine = core::Engine::GetInstance();
            assetimport::ImportedMeshSourceAsset importedSourceAsset;
            try
            {
                importedSourceAsset = engine.GetMeshImporter().ImportMeshSourceAsset(sourcePath.string());
            }
            catch (const std::exception &exception)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to import animation source '" + sourcePath.string() + "': " + exception.what();
                }
                return false;
            }

            if (importedSourceAsset.animations.empty())
            {
                if (errorMessage)
                {
                    *errorMessage = "Source model contained no animation clips: " + sourcePath.string();
                }
                return false;
            }

            std::vector<std::string> clipReferences;
            if (!EnsureAnimationAssetUsesClipReferences(project, animationReference, clipReferences, errorMessage))
            {
                return false;
            }

            const auto animationPath = project.ResolveAssetReference(animationReference);
            const auto clipDirectory = animationPath.parent_path() / "Clips";
            if (!SaveImportedAnimationClips(project, clipDirectory, sourcePath, importedSourceAsset.animations, clipReferences, errorMessage))
            {
                return false;
            }

            return engine.GetAssetManager().SaveAnimationAssetReferences(animationReference, clipReferences, errorMessage);
        }

        bool ConfigureMeshComponentForReference(scene::Entity &entity,
                                                const std::string &reference,
                                                std::string *errorMessage)
        {
            auto &engine = core::Engine::GetInstance();
            auto *meshComponent = entity.CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{});

            if (auto *mesh = engine.GetAssetManager().LoadMeshAsset(reference))
            {
                meshComponent->SetMesh(mesh);
                meshComponent->SetSourceMeshPath(reference);
                if (assets::Project::IsEngineAssetReference(reference))
                {
                    meshComponent->SetModelObjectIdentity(reference, 1);
                }
                else
                {
                    const auto &metadata = engine.GetAssetManager().GetMeshAssetMetadata(reference);
                    if (metadata.sourceAssetId.empty() || metadata.sourceObjectId == 0) return false;
                    meshComponent->SetModelObjectIdentity(metadata.sourceAssetId, metadata.sourceObjectId);
                }
                const auto &materialReferences = engine.GetAssetManager().GetMeshAssetMaterialReferences(reference);
                const auto materials = LoadMaterialReferences(engine, materialReferences);
                meshComponent->SetMaterials(materials);
                for (size_t materialSlotIndex = 0; materialSlotIndex < materialReferences.size(); ++materialSlotIndex)
                {
                    meshComponent->SetMaterialAssetForMaterialSlot(materialSlotIndex, materialReferences[materialSlotIndex]);
                }
                AttachAnimationAsset(entity, FindSiblingAnimationAssetReference(EditorShell::GetInstance().GetProject(), reference));
                return true;
            }
            if (errorMessage) *errorMessage = "Mesh object is not a valid imported model artifact: " + reference;
            return false;
        }

        bool ImportSourceModelAsset(assets::Project &project, const assets::ProjectAssetEntry &asset, std::string *errorMessage)
        {
            if (asset.type != assets::ProjectAssetType::Model)
            {
                return false;
            }

            auto &engine = core::Engine::GetInstance();
            const auto sourcePath = project.ResolveAssetReference(asset.reference);
            assets::AssetDatabase assetDatabase;
            if (!assetDatabase.Scan(project, errorMessage))
            {
                return false;
            }
            const auto *sourceRecord = assetDatabase.FindByReference(asset.reference);
            assetimport::ImportedMeshSourceAsset importedSourceAsset;
            try
            {
                importedSourceAsset = engine.GetMeshImporter().ImportMeshSourceAsset(sourcePath.string());
            }
            catch (const std::exception &exception)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to import source model '" + asset.reference + "': " + exception.what();
                }
                return false;
            }

            const bool hasMesh = !importedSourceAsset.meshData.vertices.empty() && !importedSourceAsset.meshData.indices.empty();
            const bool hasAnimations = !importedSourceAsset.animations.empty();
            const bool hasTextures = !importedSourceAsset.textures.empty();
            const bool hasAuthoredMaterials = importedSourceAsset.materials.size() > 1;
            if (!hasMesh && !hasAnimations && !hasTextures && !hasAuthoredMaterials)
            {
                if (errorMessage)
                {
                    *errorMessage = "Source model contained no mesh, materials, textures, or animations: " + asset.reference;
                }
                return false;
            }

            const auto importDirectory = project.GetAssetDirectoryPath() / "Imported" / sourcePath.stem();
            const std::string meshReference = project.MakeAssetReference(importDirectory / (sourcePath.stem().string() + ".plutomesh"));
            assets::ModelAsset modelAsset;
            modelAsset.sourceReference = asset.reference;
            modelAsset.sourceAssetId = sourceRecord ? sourceRecord->id : std::string{};
            modelAsset.sourceContentHash = sourceRecord ? sourceRecord->contentHash : 0;
            modelAsset.importerVersion = 1;
            std::vector<std::string> textureReferences;
            if (!importedSourceAsset.textures.empty())
            {
                textureReferences.reserve(importedSourceAsset.textures.size());
                for (std::size_t textureIndex = 0; textureIndex < importedSourceAsset.textures.size(); ++textureIndex)
                {
                    const auto &texture = importedSourceAsset.textures[textureIndex];
                    textureReferences.push_back(ImportTextureAsset(project, importDirectory, texture, static_cast<int>(textureIndex), errorMessage));
                    if (textureReferences.back().empty() && (!texture.sourcePath.empty() || !texture.pixels.empty()))
                    {
                        return false;
                    }
                    const std::string textureName = !texture.sourcePath.empty()
                                                        ? std::filesystem::path(texture.sourcePath).stem().string()
                                                        : "Texture " + std::to_string(textureIndex);
                    modelAsset.objects.push_back({
                        .localId = assets::MakeModelSubAssetId(assets::ProjectAssetType::Texture, textureName),
                        .type = assets::ProjectAssetType::Texture,
                        .name = textureName,
                        .reference = textureReferences.back(),
                    });
                }
            }

            std::vector<std::string> materialReferences;
            if (hasMesh ? !importedSourceAsset.materials.empty() : hasAuthoredMaterials)
            {
                materialReferences.reserve(importedSourceAsset.materials.size());
                std::deque<render::Texture> textureHandles;
                for (size_t materialIndex = 0; materialIndex < importedSourceAsset.materials.size(); ++materialIndex)
                {
                    const std::string materialName = "M_" + sourcePath.stem().string() + "_" + std::to_string(materialIndex);
                    const std::string materialReference = project.MakeAssetReference(importDirectory / (materialName + ".plutomaterial"));
                    std::string materialError;
                    const auto materialConfig = BuildGeneratedMaterialConfig(importedSourceAsset.materials[materialIndex], textureReferences, textureHandles);
                    if (!engine.GetAssetManager().SaveMaterialAsset(materialReference, materialConfig, &materialError))
                    {
                        if (errorMessage)
                        {
                            *errorMessage = materialError.empty() ? "Failed to save imported material." : materialError;
                        }
                        return false;
                    }
                    materialReferences.push_back(materialReference);
                    modelAsset.objects.push_back({
                        .localId = assets::MakeModelSubAssetId(assets::ProjectAssetType::Material, materialName),
                        .type = assets::ProjectAssetType::Material,
                        .name = materialName,
                        .reference = materialReference,
                    });
                }
            }

            if (hasMesh)
            {
                render::MeshConfig meshConfig;
                meshConfig.data = importedSourceAsset.meshData;
                meshConfig.submeshes = importedSourceAsset.submeshes;
                meshConfig.hasLightmapUvs = importedSourceAsset.hasLightmapUvs;
                meshConfig.skeleton = importedSourceAsset.skeleton;
                meshConfig.animationNodes = importedSourceAsset.animationNodes;
                meshConfig.animations = importedSourceAsset.animations;

                assets::MeshAssetMetadata meshMetadata;
                meshMetadata.sourceAssetReference = asset.reference;
                const std::string meshName = sourcePath.stem().string();
                const auto meshObjectId = assets::MakeModelSubAssetId(assets::ProjectAssetType::Mesh, meshName);
                meshMetadata.sourceAssetId = modelAsset.sourceAssetId;
                meshMetadata.sourceObjectId = meshObjectId;
                if (!engine.GetAssetManager().SaveMeshAsset(meshReference, meshConfig, materialReferences, errorMessage, meshMetadata))
                {
                    return false;
                }
                modelAsset.objects.insert(modelAsset.objects.begin(), {
                    .localId = meshObjectId,
                    .type = assets::ProjectAssetType::Mesh,
                    .name = meshName,
                    .reference = meshReference,
                });
            }

            if (hasAnimations)
            {
                const std::string animationReference = project.MakeAssetReference(importDirectory / (sourcePath.stem().string() + ".plutoanim"));
                std::vector<std::string> clipReferences;
                if (!SaveImportedAnimationClips(project, importDirectory / "Clips", sourcePath, importedSourceAsset.animations, clipReferences, errorMessage))
                {
                    return false;
                }
                if (!engine.GetAssetManager().SaveAnimationAssetReferences(animationReference, clipReferences, errorMessage))
                {
                    return false;
                }
                modelAsset.objects.push_back({
                    .localId = assets::MakeModelSubAssetId(assets::ProjectAssetType::Animation, sourcePath.stem().string()),
                    .type = assets::ProjectAssetType::Animation,
                    .name = sourcePath.stem().string() + " Animations",
                    .reference = animationReference,
                });
                for (std::size_t clipIndex = 0; clipIndex < clipReferences.size(); ++clipIndex)
                {
                    const std::string clipName = clipIndex < importedSourceAsset.animations.size()
                                                     ? importedSourceAsset.animations[clipIndex].name
                                                     : "Clip " + std::to_string(clipIndex);
                    modelAsset.objects.push_back({
                        .localId = assets::MakeModelSubAssetId(assets::ProjectAssetType::AnimationClip, clipName),
                        .type = assets::ProjectAssetType::AnimationClip,
                        .name = clipName,
                        .reference = clipReferences[clipIndex],
                    });
                }
            }

            std::error_code directoryError;
            std::filesystem::create_directories(importDirectory, directoryError);
            if (directoryError)
            {
                if (errorMessage) *errorMessage = "Failed to create imported model directory: " + directoryError.message();
                return false;
            }
            const auto modelPath = importDirectory / (sourcePath.stem().string() + ".plutomodel");
            if (!assets::SaveModelAsset(modelPath.string(), modelAsset, errorMessage))
            {
                return false;
            }

            // Register every generated artifact immediately so scene references
            // receive stable IDs on the first drag, not only after an editor restart.
            assets::AssetDatabase generatedAssetDatabase;
            return generatedAssetDatabase.Scan(project, errorMessage);
        }

        bool ImportExternalSourceModelIntoAssets(assets::Project &project, std::string *importedReference, std::string *errorMessage)
        {
            const std::string selectedPath = BrowseSourceModelPath();
            if (selectedPath.empty())
            {
                return false;
            }

            std::filesystem::path importedSourcePath;
            if (!CopySourceModelPackageToAssets(project, selectedPath, importedSourcePath, errorMessage))
            {
                return false;
            }

            const std::string sourceReference = project.MakeAssetReference(importedSourcePath);
            assets::ProjectAssetEntry sourceAsset{};
            sourceAsset.reference = sourceReference;
            sourceAsset.type = assets::ProjectAssetType::Model;
            std::error_code errorCode;
            sourceAsset.size = std::filesystem::file_size(importedSourcePath, errorCode);

            if (!ImportSourceModelAsset(project, sourceAsset, errorMessage))
            {
                return false;
            }

            if (importedReference)
            {
                *importedReference = sourceReference;
            }
            return true;
        }

    }

    bool InstantiateMeshAssetIntoScene(std::string reference, scene::Entity *parent)
    {
        if (reference.empty() || assets::Project::GetAssetTypeForReference(reference) != assets::ProjectAssetType::Mesh)
        {
            return false;
        }

        auto &editorShell = EditorShell::GetInstance();
        auto *scene = editorShell.GetEngine().GetScene();
        if (!scene)
        {
            return false;
        }

        scene::Entity *createdEntity = nullptr;
        std::string errorMessage;
        editorShell.ExecuteSceneEdit("Instantiate Model Mesh Object",
                                     [scene, parent, reference, &createdEntity, &errorMessage]()
                                     {
                                         auto entity = std::make_unique<scene::Entity>(scene::EntityConfig{
                                             .name = BuildEntityNameForMeshReference(reference),
                                         });
                                         createdEntity = scene->AddEntity(std::move(entity), parent);
                                         if (!createdEntity || !ConfigureMeshComponentForReference(*createdEntity, reference, &errorMessage))
                                         {
                                             if (createdEntity)
                                             {
                                                 scene->RemoveEntity(createdEntity);
                                                 createdEntity = nullptr;
                                             }
                                             return;
                                         }
                                     });

        if (createdEntity)
        {
            editorShell.SetSelectedEntity(createdEntity);
            editorShell.MarkSceneDirty();
            return true;
        }

        if (!errorMessage.empty())
        {
            editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage);
        }
        return false;
    }

    bool InstantiateModelAssetIntoScene(const std::string &reference, scene::Entity *parent)
    {
        auto &editorShell = EditorShell::GetInstance();
        auto *project = editorShell.GetProject();
        if (!project || assets::Project::GetAssetTypeForReference(reference) != assets::ProjectAssetType::Model)
        {
            return false;
        }

        assets::ModelAsset model;
        if (!assets::LoadModelAsset(GetImportedModelManifestPath(*project, reference).string(), model))
        {
            editorShell.Log(EditorShell::ConsoleSeverity::Error, "Import the model before placing it in a scene.");
            return false;
        }
        const auto meshObject = std::find_if(model.objects.begin(), model.objects.end(), [](const auto &object)
                                             { return object.type == assets::ProjectAssetType::Mesh; });
        return meshObject != model.objects.end() && InstantiateMeshAssetIntoScene(meshObject->reference, parent);
    }

    void ContentBrowserPanel::Render()
    {
        auto &editorShell = EditorShell::GetInstance();
        auto *project = editorShell.GetProject();
        if (!project)
        {
            ImGui::TextDisabled("No project loaded.");
            return;
        }

        const auto renderAddMenu = [&]()
        {
            if (ImGui::BeginMenu("Rendering"))
            {
                if (ImGui::MenuItem("Material"))
                {
                    m_newMaterialNameBuffer.fill('\0');
                    m_pendingMenuAction = PendingMenuAction::CreateMaterial;
                }
                if (ImGui::MenuItem("Particle System"))
                {
                    m_newParticleSystemNameBuffer.fill('\0');
                    m_pendingMenuAction = PendingMenuAction::CreateParticleSystem;
                }
                if (ImGui::MenuItem("Post Process Preset"))
                {
                    m_newPostProcessPresetNameBuffer.fill('\0');
                    m_pendingMenuAction = PendingMenuAction::CreatePostProcessPreset;
                }
                if (ImGui::MenuItem("Shader Graph"))
                {
                    m_newShaderGraphNameBuffer.fill('\0');
                    m_pendingMenuAction = PendingMenuAction::CreateShaderGraph;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Animation"))
            {
                if (ImGui::MenuItem("Animation Graph"))
                {
                    m_newAnimationGraphNameBuffer.fill('\0');
                    m_pendingMenuAction = PendingMenuAction::CreateAnimationGraph;
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Scriptable Object"))
            {
                m_newScriptableObjectNameBuffer.fill('\0');
                m_newScriptableObjectClassIndex = 0;
                m_pendingMenuAction = PendingMenuAction::CreateScriptableObject;
            }
            if (ImGui::BeginMenu("Import"))
            {
                if (ImGui::MenuItem("3D Model..."))
                {
                    m_pendingMenuAction = PendingMenuAction::ImportModel;
                }
                ImGui::EndMenu();
            }
        };

        ImGui::SetNextItemWidth(240.0f);
        ImGui::InputText("Filter", m_filterBuffer.data(), m_filterBuffer.size());
        ImGui::SameLine();
        if (ImGui::Button("Refresh"))
        {
            project->RefreshAssetRegistry();
            m_assetCacheDirty = true;
            editorShell.Log(EditorShell::ConsoleSeverity::Info, "Refreshed project assets.");
        }
        ImGui::SameLine();
        if (ImGui::Button("Add..."))
        {
            ImGui::OpenPopup("ContentBrowserAddMenu");
        }
        if (ImGui::BeginPopup("ContentBrowserAddMenu"))
        {
            renderAddMenu();
            ImGui::EndPopup();
        }

        const auto pendingMenuAction = m_pendingMenuAction;
        m_pendingMenuAction = PendingMenuAction::None;
        switch (pendingMenuAction)
        {
        case PendingMenuAction::ImportModel:
        {
            std::string importedReference;
            std::string errorMessage;
            if (ImportExternalSourceModelIntoAssets(*project, &importedReference, &errorMessage))
            {
                project->RefreshAssetRegistry();
                m_assetCacheDirty = true;
                editorShell.MarkProjectDirty();
                editorShell.Log(EditorShell::ConsoleSeverity::Info, "Imported model into assets: " + importedReference);
            }
            else if (!errorMessage.empty())
            {
                editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage);
            }
            break;
        }
        case PendingMenuAction::CreateMaterial:
            ImGui::OpenPopup("Create Material Asset");
            break;
        case PendingMenuAction::CreateParticleSystem:
            ImGui::OpenPopup("Create Particle System Asset");
            break;
        case PendingMenuAction::CreatePostProcessPreset:
            ImGui::OpenPopup("Create Post Process Preset Asset");
            break;
        case PendingMenuAction::CreateShaderGraph:
            ImGui::OpenPopup("Create Shader Graph Asset");
            break;
        case PendingMenuAction::CreateAnimationGraph:
            ImGui::OpenPopup("Create Animation Graph Asset");
            break;
        case PendingMenuAction::CreateScriptableObject:
            ImGui::OpenPopup("Create Scriptable Object Asset");
            break;
        case PendingMenuAction::None:
        default:
            break;
        }

        if (ImGui::BeginPopupModal("Create Material Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", m_newMaterialNameBuffer.data(), m_newMaterialNameBuffer.size());
            const std::string sanitizedName = SanitizeAssetFileName(m_newMaterialNameBuffer.data());
            const auto createDirectory = GetCreateDirectory(*project, m_selectedFolder, "Materials");
            if (!sanitizedName.empty())
            {
                const std::string fileName = sanitizedName + ".plutomaterial";
                ImGui::TextDisabled("Creates %s", DisplayCreatePath(*project, createDirectory, fileName).c_str());
            }
            else
            {
                ImGui::TextDisabled("Enter a material name.");
            }

            ImGui::BeginDisabled(sanitizedName.empty());
            if (ImGui::Button("Create"))
            {
                const auto materialPath = createDirectory / (sanitizedName + ".plutomaterial");
                const std::string reference = project->MakeAssetReference(materialPath);
                render::MaterialConfig config;
                config.color = glm::vec4(0.82f, 0.84f, 0.88f, 1.0f);
                config.metallic = 0.0f;
                config.roughness = 0.55f;

                std::string errorMessage;
                if (core::Engine::GetInstance().GetAssetManager().SaveMaterialAsset(reference, config, &errorMessage))
                {
                    project->RefreshAssetRegistry();
                    m_assetCacheDirty = true;
                    editorShell.OpenMaterialAsset(reference);
                    editorShell.MarkProjectDirty();
                    editorShell.Log(EditorShell::ConsoleSeverity::Info, "Created material: " + reference);
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage.empty() ? "Failed to create material." : errorMessage);
                }
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Create Scriptable Object Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            auto &scriptEngine = editorShell.GetEngine().GetScriptEngine();
            const auto classNames = scriptEngine.GetScriptableObjectClassNames();
            ImGui::InputText("Name", m_newScriptableObjectNameBuffer.data(), m_newScriptableObjectNameBuffer.size());
            if (classNames.empty())
            {
                ImGui::TextDisabled("Build a concrete ScriptableObject subclass first.");
            }
            else
            {
                m_newScriptableObjectClassIndex = std::clamp(m_newScriptableObjectClassIndex, 0, static_cast<int>(classNames.size()) - 1);
                if (ImGui::BeginCombo("Type", classNames[static_cast<std::size_t>(m_newScriptableObjectClassIndex)].c_str()))
                {
                    for (int index = 0; index < static_cast<int>(classNames.size()); ++index)
                    {
                        if (ImGui::Selectable(classNames[static_cast<std::size_t>(index)].c_str(), index == m_newScriptableObjectClassIndex))
                        {
                            m_newScriptableObjectClassIndex = index;
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            const std::string sanitizedName = SanitizeAssetFileName(m_newScriptableObjectNameBuffer.data());
            const auto createDirectory = GetCreateDirectory(*project, m_selectedFolder, "Data");
            ImGui::BeginDisabled(sanitizedName.empty() || classNames.empty());
            if (ImGui::Button("Create"))
            {
                const auto &className = classNames[static_cast<std::size_t>(m_newScriptableObjectClassIndex)];
                const auto *definition = scriptEngine.FindClass(className);
                std::unordered_map<std::string, scripting::ScriptFieldValue> values;
                if (definition)
                {
                    for (const auto &field : definition->fields) values[field.name] = field.defaultValue;
                }
                const auto assetPath = createDirectory / (sanitizedName + ".plutoscriptable");
                if (definition && SaveScriptableObjectAsset(assetPath, className, definition->fields, values))
                {
                    project->RefreshAssetRegistry();
                    m_assetCacheDirty = true;
                    editorShell.MarkProjectDirty();
                    editorShell.Log(EditorShell::ConsoleSeverity::Info, "Created scriptable object: " + project->MakeAssetReference(assetPath));
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    editorShell.Log(EditorShell::ConsoleSeverity::Error, "Failed to create scriptable object asset.");
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Create Particle System Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", m_newParticleSystemNameBuffer.data(), m_newParticleSystemNameBuffer.size());
            const std::string sanitizedName = SanitizeAssetFileName(m_newParticleSystemNameBuffer.data());
            const auto createDirectory = GetCreateDirectory(*project, m_selectedFolder, "Particles");
            if (!sanitizedName.empty())
            {
                const std::string fileName = sanitizedName + ".plutoparticles";
                ImGui::TextDisabled("Creates %s", DisplayCreatePath(*project, createDirectory, fileName).c_str());
            }
            else
            {
                ImGui::TextDisabled("Enter a particle system name.");
            }

            ImGui::BeginDisabled(sanitizedName.empty());
            if (ImGui::Button("Create"))
            {
                const auto particlePath = createDirectory / (sanitizedName + ".plutoparticles");
                const std::string reference = project->MakeAssetReference(particlePath);
                std::string errorMessage;
                if (core::Engine::GetInstance().GetAssetManager().SaveParticleSystemAsset(reference, assets::CreateDefaultParticleSystemAsset(), &errorMessage))
                {
                    project->RefreshAssetRegistry();
                    m_assetCacheDirty = true;
                    editorShell.OpenParticleSystemAsset(reference);
                    editorShell.MarkProjectDirty();
                    editorShell.Log(EditorShell::ConsoleSeverity::Info, "Created particle system: " + reference);
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage.empty() ? "Failed to create particle system." : errorMessage);
                }
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Create Post Process Preset Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", m_newPostProcessPresetNameBuffer.data(), m_newPostProcessPresetNameBuffer.size());
            const std::string sanitizedName = SanitizeAssetFileName(m_newPostProcessPresetNameBuffer.data());
            const auto createDirectory = GetCreateDirectory(*project, m_selectedFolder, "PostProcessing");
            ImGui::BeginDisabled(sanitizedName.empty());
            if (ImGui::Button("Create"))
            {
                const auto path = createDirectory / (sanitizedName + ".plutopostprocess");
                const std::string reference = project->MakeAssetReference(path);
                std::string errorMessage;
                if (core::Engine::GetInstance().GetAssetManager().SavePostProcessPresetAsset(reference, assets::CreateDefaultPostProcessPresetAsset(), &errorMessage))
                {
                    project->RefreshAssetRegistry();
                    m_assetCacheDirty = true;
                    editorShell.MarkProjectDirty();
                    editorShell.Log(EditorShell::ConsoleSeverity::Info, "Created post process preset: " + reference);
                    ImGui::CloseCurrentPopup();
                }
                else
                    editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Create Shader Graph Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", m_newShaderGraphNameBuffer.data(), m_newShaderGraphNameBuffer.size());
            const std::string sanitizedName = SanitizeAssetFileName(m_newShaderGraphNameBuffer.data());
            const auto createDirectory = GetCreateDirectory(*project, m_selectedFolder, "Shaders");
            if (!sanitizedName.empty())
            {
                const std::string fileName = sanitizedName + ".plutoshadergraph";
                ImGui::TextDisabled("Creates %s", DisplayCreatePath(*project, createDirectory, fileName).c_str());
            }
            else
            {
                ImGui::TextDisabled("Enter a shader graph name.");
            }

            ImGui::BeginDisabled(sanitizedName.empty());
            if (ImGui::Button("Create"))
            {
                const auto graphPath = createDirectory / (sanitizedName + ".plutoshadergraph");
                const std::string reference = project->MakeAssetReference(graphPath);
                std::string errorMessage;
                if (core::Engine::GetInstance().GetAssetManager().SaveShaderGraphAsset(reference, render::CreateDefaultShaderGraph(), &errorMessage))
                {
                    project->RefreshAssetRegistry();
                    m_assetCacheDirty = true;
                    editorShell.OpenShaderGraphAsset(reference);
                    editorShell.MarkProjectDirty();
                    editorShell.Log(EditorShell::ConsoleSeverity::Info, "Created shader graph: " + reference);
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage.empty() ? "Failed to create shader graph." : errorMessage);
                }
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Create Animation Graph Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", m_newAnimationGraphNameBuffer.data(), m_newAnimationGraphNameBuffer.size());
            const std::string sanitizedName = SanitizeAssetFileName(m_newAnimationGraphNameBuffer.data());
            const auto createDirectory = GetCreateDirectory(*project, m_selectedFolder, "AnimGraphs");
            if (!sanitizedName.empty())
            {
                const std::string fileName = sanitizedName + ".plutoanimgraph";
                ImGui::TextDisabled("Creates %s", DisplayCreatePath(*project, createDirectory, fileName).c_str());
            }
            else
            {
                ImGui::TextDisabled("Enter an animation graph name.");
            }

            ImGui::BeginDisabled(sanitizedName.empty());
            if (ImGui::Button("Create"))
            {
                const auto graphPath = createDirectory / (sanitizedName + ".plutoanimgraph");
                const std::string reference = project->MakeAssetReference(graphPath);
                std::string errorMessage;
                if (core::Engine::GetInstance().GetAssetManager().SaveAnimationGraphAsset(reference, assets::CreateDefaultAnimationGraphAsset(), &errorMessage))
                {
                    project->RefreshAssetRegistry();
                    m_assetCacheDirty = true;
                    editorShell.OpenAnimationGraphAsset(reference);
                    editorShell.MarkProjectDirty();
                    editorShell.Log(EditorShell::ConsoleSeverity::Info, "Created animation graph: " + reference);
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage.empty() ? "Failed to create animation graph." : errorMessage);
                }
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        const auto &assets = project->GetManifest().assetEntries;
        const std::string_view filter(m_filterBuffer.data());
        const bool registryChanged = m_assetCacheDirty ||
                                     m_cachedProject != project ||
                                     m_cachedAssetReferences.size() != assets.size();

        if (registryChanged)
        {
            if (m_thumbnailCache) m_thumbnailCache->Clear();
            m_assetCacheDirty = false;
            m_cachedProject = project;
            m_cachedAssetReferences.clear();
            m_cachedAssetFolders.clear();
            m_cachedAssetFileNames.clear();
            m_cachedAssetRelativePaths.clear();
            m_cachedFolders.clear();
            m_cachedAssetReferences.reserve(assets.size());
            m_cachedAssetFolders.reserve(assets.size());
            m_cachedAssetFileNames.reserve(assets.size());
            m_cachedAssetRelativePaths.reserve(assets.size());

            std::set<std::string> folderSet;
            folderSet.insert("");

            for (int index = 0; index < static_cast<int>(assets.size()); ++index)
            {
                const auto &asset = assets[static_cast<std::size_t>(index)];
                m_cachedAssetReferences.push_back(asset.reference);

                const std::string relativePath = GetReferenceRelativePath(*project, asset);
                const bool internalArtifact = relativePath == "Imported" || relativePath.rfind("Imported/", 0) == 0;
                const std::string folder = internalArtifact ? "__internal__" : GetAssetFolderParent(relativePath);
                const auto separator = relativePath.find_last_of('/');
                const std::string fileName = separator == std::string::npos ? relativePath : relativePath.substr(separator + 1);
                m_cachedAssetRelativePaths.push_back(relativePath);
                m_cachedAssetFolders.push_back(folder);
                m_cachedAssetFileNames.push_back(fileName.empty() ? DisplayAssetReference(asset.reference) : fileName);

                std::string folderPath = internalArtifact ? std::string{} : folder;
                while (!folderPath.empty())
                {
                    folderSet.insert(folderPath);
                    const std::string parentPath = GetAssetFolderParent(folderPath);
                    if (parentPath.empty() || parentPath == folderPath)
                    {
                        break;
                    }
                    folderPath = parentPath;
                }
            }

            std::error_code directoryError;
            const auto assetDirectory = project->GetAssetDirectoryPath();
            if (std::filesystem::exists(assetDirectory, directoryError))
            {
                for (std::filesystem::recursive_directory_iterator iterator(assetDirectory, directoryError), end; iterator != end && !directoryError; iterator.increment(directoryError))
                {
                    if (!iterator->is_directory(directoryError) || directoryError)
                    {
                        directoryError.clear();
                        continue;
                    }

                    std::error_code relativeError;
                    const auto relativePath = std::filesystem::relative(iterator->path(), assetDirectory, relativeError);
                    if (!relativeError)
                    {
                        const auto normalized = NormalizeAssetRelativePath(relativePath);
                        if (normalized != "Imported" && normalized.rfind("Imported/", 0) != 0)
                        {
                            folderSet.insert(normalized);
                        }
                    }
                }
            }

            m_cachedFolders.assign(folderSet.begin(), folderSet.end());
            m_cachedFolderParents.assign(m_cachedFolders.size(), {});
            m_cachedFolderLabels.assign(m_cachedFolders.size(), {});
            m_cachedFolderHasChildren.assign(m_cachedFolders.size(), false);
            m_cachedFolderChildIndices.assign(m_cachedFolders.size(), {});
            m_cachedRootFolderIndices.clear();

            std::unordered_map<std::string, int> folderIndexByPath;
            folderIndexByPath.reserve(m_cachedFolders.size());
            for (std::size_t folderIndex = 0; folderIndex < m_cachedFolders.size(); ++folderIndex)
            {
                folderIndexByPath.emplace(m_cachedFolders[folderIndex], static_cast<int>(folderIndex));
                m_cachedFolderParents[folderIndex] = GetAssetFolderParent(m_cachedFolders[folderIndex]);
                m_cachedFolderLabels[folderIndex] = GetAssetFolderName(m_cachedFolders[folderIndex]);
            }

            for (std::size_t folderIndex = 0; folderIndex < m_cachedFolders.size(); ++folderIndex)
            {
                const auto &folder = m_cachedFolders[folderIndex];
                if (folder.empty())
                {
                    continue;
                }

                const auto &parent = m_cachedFolderParents[folderIndex];
                if (parent.empty())
                {
                    m_cachedRootFolderIndices.push_back(static_cast<int>(folderIndex));
                    continue;
                }

                const auto parentEntry = folderIndexByPath.find(parent);
                if (parentEntry != folderIndexByPath.end())
                {
                    m_cachedFolderChildIndices[static_cast<std::size_t>(parentEntry->second)].push_back(static_cast<int>(folderIndex));
                    m_cachedFolderHasChildren[static_cast<std::size_t>(parentEntry->second)] = true;
                }
            }
            m_cachedFilter.clear();
            m_cachedFolder = "__registry_refresh__";
            m_selectedAssetIndex = (m_selectedAssetIndex >= 0 && m_selectedAssetIndex < static_cast<int>(assets.size())) ? m_selectedAssetIndex : -1;
        }

        if (registryChanged || m_cachedFilter != filter || m_cachedFolder != m_selectedFolder)
        {
            m_cachedFilter = filter;
            m_cachedFolder = m_selectedFolder;
            m_filteredAssetIndices.clear();
            m_filteredAssetDisplayNames.clear();
            m_cachedChildFolders.clear();
            m_cachedChildFolderLabels.clear();
            m_cachedChildFolderDisplayNames.clear();
            m_filteredAssetIndices.reserve(assets.size());
            m_filteredAssetDisplayNames.reserve(assets.size());

            for (int index = 0; index < static_cast<int>(assets.size()); ++index)
            {
                const auto &asset = assets[static_cast<std::size_t>(index)];
                const std::size_t cacheIndex = static_cast<std::size_t>(index);
                if (m_cachedAssetFolders[cacheIndex] == "__internal__")
                {
                    continue;
                }
                const bool inVisibleScope = filter.empty()
                                                ? m_cachedAssetFolders[cacheIndex] == m_selectedFolder
                                                : IsFolderAncestorOrSelf(m_selectedFolder, m_cachedAssetFolders[cacheIndex]);
                std::string displayName = std::string("[") + std::string(assets::Project::GetAssetTypeName(asset.type)) + "] " +
                                          (filter.empty() ? m_cachedAssetFileNames[cacheIndex] : m_cachedAssetRelativePaths[cacheIndex]);
                if (inVisibleScope && ContainsInsensitive(displayName, filter))
                {
                    m_filteredAssetIndices.push_back(index);
                    m_filteredAssetDisplayNames.push_back(std::move(displayName));
                }
            }

            const std::vector<int> *childFolderIndices = &m_cachedRootFolderIndices;
            if (!m_selectedFolder.empty())
            {
                for (std::size_t folderIndex = 0; folderIndex < m_cachedFolders.size(); ++folderIndex)
                {
                    if (m_cachedFolders[folderIndex] == m_selectedFolder)
                    {
                        childFolderIndices = &m_cachedFolderChildIndices[folderIndex];
                        break;
                    }
                }
            }

            m_cachedChildFolders.reserve(childFolderIndices->size());
            m_cachedChildFolderLabels.reserve(childFolderIndices->size());
            m_cachedChildFolderDisplayNames.reserve(childFolderIndices->size());
            for (const int folderIndex : *childFolderIndices)
            {
                if (folderIndex >= 0 && static_cast<std::size_t>(folderIndex) < m_cachedFolders.size())
                {
                    m_cachedChildFolders.push_back(m_cachedFolders[static_cast<std::size_t>(folderIndex)]);
                    m_cachedChildFolderLabels.push_back(m_cachedFolderLabels[static_cast<std::size_t>(folderIndex)]);
                    m_cachedChildFolderDisplayNames.push_back("[Folder] " + m_cachedFolderLabels[static_cast<std::size_t>(folderIndex)]);
                }
            }
        }

        const auto renderFolderTree = [&](const auto &self, const std::vector<int> &folderIndices) -> void
        {
            for (const int rawFolderIndex : folderIndices)
            {
                if (rawFolderIndex < 0)
                {
                    continue;
                }
                const std::size_t folderIndex = static_cast<std::size_t>(rawFolderIndex);
                if (folderIndex >= m_cachedFolders.size())
                {
                    continue;
                }

                const auto &folder = m_cachedFolders[folderIndex];
                if (folder.empty())
                {
                    continue;
                }

                const bool selected = folder == m_selectedFolder;
                const bool hasChildren = m_cachedFolderHasChildren[folderIndex];
                const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                                 ImGuiTreeNodeFlags_SpanAvailWidth |
                                                 (selected ? ImGuiTreeNodeFlags_Selected : 0) |
                                                 (hasChildren ? 0 : ImGuiTreeNodeFlags_Leaf);
                ImGui::PushID(folder.c_str());
                const bool open = ImGui::TreeNodeEx("Folder", flags, "%s", m_cachedFolderLabels[folderIndex].c_str());
                if (ImGui::IsItemClicked())
                {
                    m_selectedFolder = folder;
                    m_selectedAssetIndex = -1;
                }
                if (open)
                {
                    if (hasChildren)
                    {
                        self(self, m_cachedFolderChildIndices[folderIndex]);
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        };

        const bool hasSearch = !m_cachedFilter.empty();
        ImGui::TextDisabled("Assets: %zu", assets.size());
        ImGui::SameLine();
        ImGui::TextDisabled("| Folder: %s", m_selectedFolder.empty() ? "Assets" : m_selectedFolder.c_str());
        ImGui::Separator();

        const float footerHeight = ImGui::GetFrameHeightWithSpacing() * 3.0f;
        ImGui::BeginChild("ContentBrowserBody", ImVec2(0.0f, -footerHeight), true);
        const float leftWidth = std::min(260.0f, std::max(180.0f, ImGui::GetContentRegionAvail().x * 0.28f));
        ImGui::BeginChild("FolderTree", ImVec2(leftWidth, 0.0f), false);
        const ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_DefaultOpen |
                                             ImGuiTreeNodeFlags_OpenOnArrow |
                                             ImGuiTreeNodeFlags_SpanAvailWidth |
                                             (m_selectedFolder.empty() ? ImGuiTreeNodeFlags_Selected : 0);
        const bool rootOpen = ImGui::TreeNodeEx("AssetsRoot", rootFlags, "Assets");
        if (ImGui::IsItemClicked())
        {
            m_selectedFolder.clear();
            m_selectedAssetIndex = -1;
        }
        if (rootOpen)
        {
            renderFolderTree(renderFolderTree, m_cachedRootFolderIndices);
            ImGui::TreePop();
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("AssetPane", ImVec2(0.0f, 0.0f), false);
        if (!m_selectedFolder.empty())
        {
            if (ImGui::SmallButton("Assets"))
            {
                m_selectedFolder.clear();
                m_selectedAssetIndex = -1;
            }
            std::string breadcrumb;
            std::size_t partStart = 0;
            while (partStart < m_selectedFolder.size())
            {
                const auto partEnd = m_selectedFolder.find('/', partStart);
                const std::string label = m_selectedFolder.substr(partStart, partEnd == std::string::npos ? std::string::npos : partEnd - partStart);
                breadcrumb = breadcrumb.empty() ? label : breadcrumb + "/" + label;
                ImGui::SameLine();
                ImGui::TextUnformatted("/");
                ImGui::SameLine();
                ImGui::PushID(breadcrumb.c_str());
                if (ImGui::SmallButton(label.c_str()))
                {
                    m_selectedFolder = breadcrumb;
                    m_selectedAssetIndex = -1;
                }
                ImGui::PopID();

                if (partEnd == std::string::npos)
                {
                    break;
                }
                partStart = partEnd + 1;
            }
        }
        else
        {
            ImGui::TextDisabled("Assets");
        }
        ImGui::Separator();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::SliderFloat("Thumbnail Size", &m_thumbnailSize, 72.0f, 144.0f, "%.0f px");
        ImGui::Separator();

        if (!m_thumbnailCache) m_thumbnailCache = std::make_unique<AssetThumbnailCache>();
        m_thumbnailCache->BeginFrame();
        const float cardWidth = m_thumbnailSize + 18.0f;
        const float cardHeight = m_thumbnailSize + ImGui::GetTextLineHeightWithSpacing() * 2.4f + 12.0f;
        const int columnCount = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cardWidth));

        const auto drawCardBackground = [&](bool selected, bool hovered)
        {
            const ImVec2 minimum = ImGui::GetItemRectMin();
            const ImVec2 maximum = ImGui::GetItemRectMax();
            const ImU32 fill = ImGui::GetColorU32(selected ? ImGuiCol_HeaderActive : hovered ? ImGuiCol_HeaderHovered : ImGuiCol_FrameBg);
            ImGui::GetWindowDrawList()->AddRectFilled(minimum, maximum, fill, 5.0f);
            ImGui::GetWindowDrawList()->AddRect(minimum, maximum, ImGui::GetColorU32(selected ? ImGuiCol_NavHighlight : ImGuiCol_Border), 5.0f);
        };

        if (ImGui::BeginTable("AssetIconGrid", columnCount, ImGuiTableFlags_SizingFixedFit))
        {
            if (!hasSearch)
            {
                for (std::size_t folderIndex = 0; folderIndex < m_cachedChildFolders.size(); ++folderIndex)
                {
                    ImGui::TableNextColumn();
                    const auto &folder = m_cachedChildFolders[folderIndex];
                    const auto &label = m_cachedChildFolderLabels[folderIndex];
                    ImGui::PushID(folder.c_str());
                    ImGui::InvisibleButton("FolderCard", ImVec2(cardWidth - 6.0f, cardHeight));
                    const bool hovered = ImGui::IsItemHovered();
                    drawCardBackground(false, hovered);
                    const ImVec2 minimum = ImGui::GetItemRectMin();
                    const ImVec2 iconMin(minimum.x + 10.0f, minimum.y + 18.0f);
                    const ImVec2 iconMax(minimum.x + cardWidth - 16.0f, minimum.y + m_thumbnailSize - 4.0f);
                    auto *drawList = ImGui::GetWindowDrawList();
                    const ImU32 folderColor = IM_COL32(216, 167, 64, 255);
                    drawList->AddRectFilled(ImVec2(iconMin.x, iconMin.y + 13.0f), iconMax, folderColor, 5.0f);
                    drawList->AddRectFilled(iconMin, ImVec2(iconMin.x + (iconMax.x - iconMin.x) * 0.46f, iconMin.y + 25.0f), folderColor, 4.0f);
                    const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
                    drawList->AddText(ImVec2(minimum.x + (cardWidth - 6.0f - std::min(textSize.x, cardWidth - 16.0f)) * 0.5f,
                                             minimum.y + m_thumbnailSize + 10.0f),
                                      ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
                    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        m_selectedFolder = folder;
                        m_selectedAssetIndex = -1;
                    }
                    ImGui::PopID();
                }
            }
            else
            {
                ImGui::TextDisabled("Search results in %s", m_selectedFolder.empty() ? "Assets" : m_selectedFolder.c_str());
            }

            for (int filteredIndex = 0; filteredIndex < static_cast<int>(m_filteredAssetIndices.size()); ++filteredIndex)
            {
                const int index = m_filteredAssetIndices[static_cast<std::size_t>(filteredIndex)];
                const auto &asset = assets[static_cast<std::size_t>(index)];
                const std::string &fileName = m_cachedAssetFileNames[static_cast<std::size_t>(index)];
                const bool selected = m_selectedAssetIndex == index;
                ImGui::TableNextColumn();
                ImGui::PushID(index);
                ImGui::InvisibleButton("AssetCard", ImVec2(cardWidth - 6.0f, cardHeight));
                const bool hovered = ImGui::IsItemHovered();
                drawCardBackground(selected, hovered);
                const ImVec2 minimum = ImGui::GetItemRectMin();
                const ImVec2 previewMin(minimum.x + 8.0f, minimum.y + 8.0f);
                const ImVec2 previewMax(minimum.x + cardWidth - 14.0f, minimum.y + m_thumbnailSize + 2.0f);
                auto *drawList = ImGui::GetWindowDrawList();
                drawList->AddRectFilled(previewMin, previewMax, IM_COL32(27, 30, 35, 255), 4.0f);
                const GLuint thumbnail = m_thumbnailCache->Get(*project, editorShell.GetEngine(), asset);
                if (thumbnail != 0)
                {
                    drawList->AddImage(static_cast<ImTextureID>(thumbnail), previewMin, previewMax,
                                       ImVec2(0, 1), ImVec2(1, 0));
                }
                else
                {
                    const std::string typeLabel(assets::Project::GetAssetTypeName(asset.type));
                    const ImVec2 typeSize = ImGui::CalcTextSize(typeLabel.c_str());
                    drawList->AddText(ImVec2((previewMin.x + previewMax.x - typeSize.x) * 0.5f,
                                             (previewMin.y + previewMax.y - typeSize.y) * 0.5f),
                                      ImGui::GetColorU32(ImGuiCol_TextDisabled), typeLabel.c_str());
                }
                const std::string shownName = fileName.size() > 22 ? fileName.substr(0, 20) + "..." : fileName;
                const ImVec2 textSize = ImGui::CalcTextSize(shownName.c_str());
                drawList->AddText(ImVec2(minimum.x + std::max(5.0f, (cardWidth - 6.0f - textSize.x) * 0.5f),
                                         minimum.y + m_thumbnailSize + 10.0f),
                                  ImGui::GetColorU32(ImGuiCol_Text), shownName.c_str());
                const std::string typeName(assets::Project::GetAssetTypeName(asset.type));
                const ImVec2 typeSize = ImGui::CalcTextSize(typeName.c_str());
                drawList->AddText(ImVec2(minimum.x + std::max(5.0f, (cardWidth - 6.0f - typeSize.x) * 0.5f),
                                         minimum.y + m_thumbnailSize + 10.0f + ImGui::GetTextLineHeightWithSpacing()),
                                  ImGui::GetColorU32(ImGuiCol_TextDisabled), typeName.c_str());

                if (ImGui::IsItemClicked()) m_selectedAssetIndex = index;
                if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) OpenAsset(editorShell, *project, asset);
                if (ImGui::BeginDragDropSource())
                {
                    ImGui::SetDragDropPayload(kContentBrowserAssetDragDropPayload, asset.reference.c_str(), asset.reference.size() + 1);
                    ImGui::TextUnformatted(fileName.c_str());
                    ImGui::EndDragDropSource();
                }
                if (hovered) ImGui::SetTooltip("%s\n%s", fileName.c_str(), asset.reference.c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (ImGui::BeginPopupContextWindow("ContentBrowserContextMenu", ImGuiPopupFlags_NoOpenOverItems))
        {
            renderAddMenu();
            ImGui::EndPopup();
        }

        ImGui::EndChild();
        ImGui::EndChild();

        if (m_selectedAssetIndex >= 0 && m_selectedAssetIndex < static_cast<int>(assets.size()))
        {
            const auto &asset = assets[static_cast<std::size_t>(m_selectedAssetIndex)];
            const auto resolvedPath = project->ResolveAssetReference(asset.reference);
            const std::string typeName(assets::Project::GetAssetTypeName(asset.type));
            ImGui::Text("Type: %s", typeName.c_str());
            ImGui::TextWrapped("Reference: %s", asset.reference.c_str());
            if (assets::Project::IsEngineAssetReference(asset.reference))
            {
                ImGui::TextWrapped("Engine Asset");
            }
            else
            {
                ImGui::TextWrapped("Path: %s", resolvedPath.string().c_str());
            }

            if (asset.type == assets::ProjectAssetType::ScriptableObject)
            {
                std::string className;
                std::unordered_map<std::string, scripting::ScriptFieldValue> values;
                auto &scriptEngine = editorShell.GetEngine().GetScriptEngine();
                if (!LoadScriptableObjectAsset(resolvedPath, className, values))
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Invalid scriptable object asset.");
                }
                else if (const auto *definition = scriptEngine.FindClass(className))
                {
                    ImGui::SeparatorText(className.c_str());
                    bool changed = false;
                    int fieldIndex = 0;
                    for (const auto &field : definition->fields)
                    {
                        auto iterator = values.find(field.name);
                        if (iterator == values.end() || !scripting::IsFieldValueCompatible(field.type, iterator->second))
                        {
                            iterator = values.insert_or_assign(field.name, field.defaultValue).first;
                        }
                        ImGui::PushID(fieldIndex++);
                        switch (field.type)
                        {
                        case scripting::ScriptFieldType::Boolean:
                        {
                            bool value = std::get<bool>(iterator->second);
                            if (ImGui::Checkbox(field.name.c_str(), &value)) { iterator->second = value; changed = true; }
                            break;
                        }
                        case scripting::ScriptFieldType::Int32:
                        {
                            int value = std::get<int32_t>(iterator->second);
                            if (ImGui::DragInt(field.name.c_str(), &value)) { iterator->second = static_cast<int32_t>(value); changed = true; }
                            break;
                        }
                        case scripting::ScriptFieldType::Float:
                        {
                            float value = std::get<float>(iterator->second);
                            if (ImGui::DragFloat(field.name.c_str(), &value, 0.01f)) { iterator->second = value; changed = true; }
                            break;
                        }
                        case scripting::ScriptFieldType::Double:
                        {
                            double value = std::get<double>(iterator->second);
                            if (ImGui::InputDouble(field.name.c_str(), &value)) { iterator->second = value; changed = true; }
                            break;
                        }
                        case scripting::ScriptFieldType::Vector2:
                        {
                            auto value = std::get<glm::vec2>(iterator->second);
                            if (ImGui::DragFloat2(field.name.c_str(), &value.x, 0.01f)) { iterator->second = value; changed = true; }
                            break;
                        }
                        case scripting::ScriptFieldType::Vector3:
                        {
                            auto value = std::get<glm::vec3>(iterator->second);
                            if (ImGui::DragFloat3(field.name.c_str(), &value.x, 0.01f)) { iterator->second = value; changed = true; }
                            break;
                        }
                        case scripting::ScriptFieldType::String:
                        case scripting::ScriptFieldType::PrefabAsset:
                        case scripting::ScriptFieldType::ScriptableObjectAsset:
                        {
                            std::array<char, 512> buffer{};
                            const auto &value = std::get<std::string>(iterator->second);
                            strncpy_s(buffer.data(), buffer.size(), value.c_str(), _TRUNCATE);
                            if (ImGui::InputText(field.name.c_str(), buffer.data(), buffer.size())) { iterator->second = std::string(buffer.data()); changed = true; }
                            break;
                        }
                        default:
                            ImGui::TextDisabled("%s (unsupported asset field)", field.name.c_str());
                            break;
                        }
                        ImGui::PopID();
                    }
                    if (changed)
                    {
                        if (SaveScriptableObjectAsset(resolvedPath, className, definition->fields, values))
                        {
                            editorShell.MarkProjectDirty();
                        }
                        else
                        {
                            editorShell.Log(EditorShell::ConsoleSeverity::Error, "Failed to save scriptable object: " + asset.reference);
                        }
                    }
                }
                else
                {
                    ImGui::TextDisabled("Type '%s' is not loaded. Build scripts to edit this asset.", className.c_str());
                }
            }
            else if (asset.type == assets::ProjectAssetType::Material)
            {
                if (ImGui::Button("Open Material"))
                {
                    editorShell.OpenMaterialAsset(asset.reference);
                }
            }
            else if (asset.type == assets::ProjectAssetType::Animation)
            {
                if (ImGui::Button("Add Clips From Model"))
                {
                    const std::string selectedPath = BrowseSourceModelPath();
                    if (!selectedPath.empty())
                    {
                        std::string errorMessage;
                        if (AddClipsFromSourceModelToAnimationAsset(*project, asset.reference, selectedPath, &errorMessage))
                        {
                            project->RefreshAssetRegistry();
                            m_assetCacheDirty = true;
                            editorShell.MarkProjectDirty();
                            editorShell.Log(EditorShell::ConsoleSeverity::Info, "Added animation clips to: " + asset.reference);
                        }
                        else
                        {
                            editorShell.Log(EditorShell::ConsoleSeverity::Error,
                                            errorMessage.empty() ? "Failed to add animation clips." : errorMessage);
                        }
                    }
                }
            }
            else if (asset.type == assets::ProjectAssetType::Mesh)
            {
                if (ImGui::Button("Open Mesh"))
                {
                    editorShell.OpenMeshAsset(asset.reference);
                }
            }
            else if (asset.type == assets::ProjectAssetType::Model)
            {
                assets::ModelAsset model;
                std::string modelError;
                const auto manifestPath = GetImportedModelManifestPath(*project, asset.reference);
                const bool imported = assets::LoadModelAsset(manifestPath.string(), model, &modelError);
                if (imported)
                {
                    ImGui::SeparatorText("Imported Model");
                    ImGui::Text("Objects: %zu", model.objects.size());
                    ImGui::TextWrapped("Asset ID: %s", model.sourceAssetId.empty() ? "Unavailable" : model.sourceAssetId.c_str());
                    ImGui::Text("Source hash: %016llx", static_cast<unsigned long long>(model.sourceContentHash));
                    ImGui::Text("Importer version: %u", model.importerVersion);
                }
                else
                {
                    ImGui::TextDisabled("This model has not been imported yet.");
                }
                if (ImGui::Button(imported ? "Reimport" : "Import"))
                {
                    std::string errorMessage;
                    if (ImportSourceModelAsset(*project, asset, &errorMessage))
                    {
                        project->RefreshAssetRegistry();
                        m_assetCacheDirty = true;
                        editorShell.MarkProjectDirty();
                        editorShell.Log(EditorShell::ConsoleSeverity::Info, "Imported model: " + asset.reference);
                    }
                    else
                    {
                        editorShell.Log(EditorShell::ConsoleSeverity::Error,
                                        errorMessage.empty() ? "Failed to import model." : errorMessage);
                    }
                }
            }
            else if (asset.type == assets::ProjectAssetType::ShaderGraph)
            {
                if (ImGui::Button("Open Shader Graph"))
                {
                    editorShell.OpenShaderGraphAsset(asset.reference);
                }
            }
            else if (asset.type == assets::ProjectAssetType::AnimationGraph)
            {
                if (ImGui::Button("Open Animation Graph"))
                {
                    editorShell.OpenAnimationGraphAsset(asset.reference);
                }
            }
            else if (asset.type == assets::ProjectAssetType::ParticleSystem)
            {
                if (ImGui::Button("Open Particle System"))
                {
                    editorShell.OpenParticleSystemAsset(asset.reference);
                }
            }
        }
    }
}
