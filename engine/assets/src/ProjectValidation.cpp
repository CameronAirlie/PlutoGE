#include "PlutoGE/assets/ProjectValidation.h"
#include "PlutoGE/assets/AssetReferences.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <limits>
#include <tuple>
#include <stdexcept>
#include <cctype>

namespace PlutoGE::assets
{
    namespace
    {
        std::string Utf8(const std::filesystem::path &path)
        {
            const auto text = path.generic_u8string();
            return {reinterpret_cast<const char *>(text.data()), text.size()};
        }
        std::vector<std::string> Fields(const std::string &line)
        {
            std::vector<std::string> fields(1);
            for (std::size_t i = 0; i < line.size(); ++i)
            {
                char c = line[i];
                if (c == '\t') { fields.emplace_back(); continue; }
                if (c == '\\' && i + 1 < line.size())
                {
                    c = line[++i];
                    if (c == 't') c = '\t'; else if (c == 'n') c = '\n'; else if (c == 'r') c = '\r';
                }
                fields.back().push_back(c);
            }
            return fields;
        }
        std::uint32_t Id(const std::string &value)
        {
            std::size_t used = 0;
            const auto id = std::stoull(value, &used);
            if (used != value.size() || id > std::numeric_limits<std::uint32_t>::max()) throw std::invalid_argument("entity ID");
            return static_cast<std::uint32_t>(id);
        }
        bool Vector(const std::string &value, bool positive, bool nonzero = false)
        {
            std::istringstream in(value);
            double x, y, z;
            char a, b;
            if (!(in >> x >> a >> y >> b >> z) || a != ',' || b != ',' || !(in >> std::ws).eof()) return false;
            const std::array values{x, y, z};
            return std::all_of(values.begin(), values.end(), [&](double v) {
                return std::isfinite(v) && std::abs(v) <= std::numeric_limits<float>::max() &&
                    (!positive || v >= std::numeric_limits<float>::denorm_min()) &&
                    (!nonzero || std::abs(v) >= std::numeric_limits<float>::denorm_min());
            });
        }
        struct Component
        {
            std::uint32_t entity;
            std::string type;
            bool enabled;
            std::size_t line;
            std::map<std::string, std::string> properties;
        };
        struct Entity
        {
            std::uint32_t parent;
            bool active;
            std::string scale;
        };
        struct Validator
        {
            const ProjectValidationInput &input;
            ProjectValidationResult result;
            void Add(std::string code, const std::string &owner, std::uint32_t entity, std::size_t line,
                     std::string message, ValidationSeverity severity = ValidationSeverity::Error)
            { result.diagnostics.push_back({severity, std::move(code), owner, entity, line, std::move(message)}); }
            void Reference(const std::string &value, const std::string &owner, std::uint32_t entity, std::size_t line)
            {
                if (!value.starts_with("project://") && !value.starts_with("engine://")) return;
                const auto reference = NormalizeAssetReference(value);
                if (reference.empty()) { Add("asset.invalid", owner, entity, line, "Invalid asset reference: " + value); return; }
                if (input.builtinReferences.contains(reference)) return;
                std::filesystem::path path;
                if (reference.starts_with("project://"))
                {
                    const auto relative = reference.substr(10);
                    path = input.assetRoot / std::filesystem::path(std::u8string(relative.begin(), relative.end()));
                }
                else if (input.resolveEngineReference) path = input.resolveEngineReference(reference);
                else { Add("asset.unverified", owner, entity, line, "Cannot resolve engine asset: " + reference, ValidationSeverity::Warning); return; }
                std::error_code ec;
                if (path.empty() || !std::filesystem::is_regular_file(path, ec))
                    Add("asset.missing", owner, entity, line, "Missing asset: " + reference + (ec ? " (" + ec.message() + ")" : ""));
            }
            void Scene(std::istream &stream, const std::string &owner, bool prefab)
            {
                std::map<std::uint32_t, Entity> entities;
                std::vector<Component> components;
                Component *component = nullptr;
                std::string line;
                std::size_t number = 0;
                while (std::getline(stream, line))
                {
                    ++number;
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (number == 1 && line != "SCENE\t1") { Add("scene.header", owner, 0, 1, "Unsupported or missing scene header."); return; }
                    if (line.size() > 1024 * 1024) { Add("scan.incomplete", owner, 0, number, "Scene record exceeds 1 MiB; record not validated."); continue; }
                    const auto fields = Fields(line);
                    try
                    {
                        if (fields[0] == "ENTITY")
                        {
                            if (fields.size() < 8) throw std::invalid_argument("entity record");
                            const auto id = Id(fields[1]);
                            if (!id || !entities.emplace(id, Entity{Id(fields[2]), fields[3] == "1", fields[7]}).second)
                                Add("scene.entity", owner, id, number, "Entity ID is zero or duplicated.");
                            component = nullptr;
                        }
                        else if (fields[0] == "COMPONENT")
                        {
                            if (fields.size() < 4) throw std::invalid_argument("component record");
                            if (component) Add("scene.component", owner, component->entity, number, "Missing END_COMPONENT record.");
                            components.push_back({Id(fields[1]), fields[2], fields[3] == "1", number, {}});
                            component = &components.back();
                        }
                        else if (fields[0] == "END_COMPONENT") component = nullptr;
                        else if (fields[0] == "PROPERTY")
                        {
                            if (!component || fields.size() < 4) throw std::invalid_argument("property record");
                            component->properties[fields[1]] = fields[3];
                            Reference(fields[3], owner, component->entity, number);
                        }
                        else if (fields[0] == "PREFAB" && fields.size() >= 3) Reference(fields[2], owner, Id(fields[1]), number);
                        else if (fields[0] == "ENVIRONMENT" && fields.size() >= 2) Reference(fields[1], owner, 0, number);
                        else if (fields[0] == "IBL_CAPTURE" && fields.size() >= 4) Reference(fields[3], owner, 0, number);
                    }
                    catch (const std::exception &) { Add("scene.record", owner, 0, number, "Malformed serialized record."); }
                }
                if (!number || stream.bad()) Add("scan.incomplete", owner, 0, number, "Scene is empty or could not be fully read.");
                if (component) Add("scene.component", owner, component->entity, component->line, "Unterminated component record.");
                const auto active = [&](std::uint32_t id) {
                    std::set<std::uint32_t> visited;
                    while (id)
                    {
                        const auto found = entities.find(id);
                        if (found == entities.end() || !visited.insert(id).second || !found->second.active) return false;
                        id = found->second.parent;
                    }
                    return true;
                };
                bool camera = false;
                for (auto &c : components)
                {
                    if (!entities.contains(c.entity)) { Add("scene.owner", owner, c.entity, c.line, "Component owner does not exist."); continue; }
                    if (c.type == "CameraComponent" && c.enabled && active(c.entity)) camera = true;
                    if (c.type == "ScriptComponent")
                    {
                        const auto name = c.properties["Source"];
                        if (name.empty()) Add("script.empty", owner, c.entity, c.line, "Script component has no class assigned.", ValidationSeverity::Warning);
                        else if (!input.scriptClasses) Add("script.unverified", owner, c.entity, c.line, "Class catalogue unavailable; cannot verify " + name, ValidationSeverity::Warning);
                        else if (!input.scriptClasses->contains(name)) Add("script.missing", owner, c.entity, c.line, "Script class is not loaded: " + name);
                    }
                    if (c.type != "ColliderComponent") continue;
                    const auto value = [&](const std::string &key, const std::string &fallback) { const auto it = c.properties.find(key); return it == c.properties.end() ? fallback : it->second; };
                    const auto scalar = [&](const std::string &key, const std::string &fallback) {
                        const auto text = value(key, fallback);
                        std::size_t used = 0;
                        const double v = std::stod(text, &used);
                        if (used != text.size() || !std::isfinite(v) || v < std::numeric_limits<float>::denorm_min() ||
                            v > std::numeric_limits<float>::max()) throw std::invalid_argument(key);
                        return v;
                    };
                    try
                    {
                        const auto shape = value("Shape", "0");
                        if (shape != "0" && shape != "1" && shape != "2" && shape != "3" && shape != "4") throw std::invalid_argument("shape");
                        if (!Vector(value("Center", "0,0,0"), false)) throw std::invalid_argument("center");
                        if (shape == "0" && !Vector(value("Size", "1,1,1"), true)) throw std::invalid_argument("size");
                        if (shape == "1" || shape == "2")
                        {
                            const double radius = scalar("Radius", "0.5");
                            if (shape == "2" && scalar("Height", "2") < 2 * radius) throw std::invalid_argument("capsule height must be at least its diameter");
                        }
                        std::set<std::uint32_t> visited;
                        auto id = c.entity;
                        while (id && entities.contains(id) && visited.insert(id).second)
                        {
                            if (!Vector(entities.at(id).scale, false, true)) throw std::invalid_argument("zero or non-finite hierarchy scale");
                            id = entities.at(id).parent;
                        }
                        if (shape == "3" || shape == "4")
                        {
                            const std::string required = shape == "3" ? "TerrainComponent" : "MeshComponent";
                            if (std::none_of(components.begin(), components.end(), [&](const auto &other) { return other.entity == c.entity && other.type == required && other.enabled; }))
                                throw std::invalid_argument("missing enabled " + required);
                        }
                    }
                    catch (const std::exception &error) { Add("collider.invalid", owner, c.entity, c.line, "Invalid collider: " + std::string(error.what())); }
                }
                for (const auto &[id, entity] : entities)
                {
                    std::set<std::uint32_t> visited;
                    auto parent = id;
                    while (parent && entities.contains(parent) && visited.insert(parent).second) parent = entities.at(parent).parent;
                    if (parent) Add("scene.hierarchy", owner, id, 0, "Missing parent or cyclic entity hierarchy.");
                }
                if (!prefab && !camera) Add("camera.missing", owner, 0, 0, "Scene has no enabled camera on an active hierarchy. Runtime-created cameras cannot be verified.", ValidationSeverity::Warning);
            }
        };
    }
    bool ProjectValidationResult::HasErrors() const
    { return std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto &d) { return d.severity == ValidationSeverity::Error; }); }

    ProjectValidationResult ValidateProject(const ProjectValidationInput &input)
    {
        Validator v{input, {}};
        if (input.startupScene.empty()) v.Add("project.startup", "Project", 0, 0, "No startup scene is configured.");
        else if (NormalizeAssetReference(input.startupScene).empty()) v.Add("project.startup", "Project", 0, 0, "Startup scene must be a valid project:// or engine:// asset reference.");
        else v.Reference(input.startupScene, "Project", 0, 0);
        std::error_code ec;
        if (!input.scriptAssembly.empty() && !std::filesystem::is_regular_file(input.scriptAssembly, ec))
            v.Add("script.assembly", "Project", 0, 0, "Configured script assembly is missing or unreadable.");
        ec.clear();
        std::filesystem::recursive_directory_iterator it(input.assetRoot, ec), end;
        while (!ec && it != end)
        {
            const auto path = it->path();
            if (it->is_regular_file(ec) && SupportsAssetReferenceScan(path))
            {
                const auto owner = "project://" + Utf8(path.lexically_relative(input.assetRoot));
                ++v.result.checkedFiles;
                if (!(input.currentScene && owner == input.currentSceneOwner))
                {
                    auto extension = path.extension().string();
                    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (extension == ".plutoscene" || extension == ".plutoprefab")
                    {
                        const auto size = std::filesystem::file_size(path, ec);
                        if (ec || size > 256ull * 1024 * 1024) v.Add("scan.incomplete", owner, 0, 0, "Scene unreadable or exceeds the 256 MiB validation limit.");
                        else { std::ifstream stream(path); v.Scene(stream, owner, extension == ".plutoprefab"); }
                    }
                    else
                    {
                        const auto scan = ScanAssetReferences(path, {}, input.assetRoot);
                        for (const auto &error : scan.errors) v.Add("scan.incomplete", owner, 0, 0, error);
                        for (const auto &reference : scan.occurrences) v.Reference(reference.reference, owner, 0, reference.line);
                    }
                }
            }
            if (!ec) it.increment(ec);
        }
        if (ec) v.Add("scan.incomplete", "Project", 0, 0, "Could not enumerate all assets: " + ec.message());
        if (input.currentScene)
        {
            if (input.currentScene->size() > 256ull * 1024 * 1024) v.Add("scan.incomplete", input.currentSceneOwner, 0, 0, "Current scene exceeds the 256 MiB validation limit.");
            else { std::istringstream stream(*input.currentScene); v.Scene(stream, input.currentSceneOwner, input.currentSceneOwner.ends_with(".plutoprefab")); }
        }
        std::stable_sort(v.result.diagnostics.begin(), v.result.diagnostics.end(), [](const auto &a, const auto &b) {
            if (a.severity != b.severity) return a.severity == ValidationSeverity::Error;
            return std::tie(a.owner, a.entity, a.line, a.code) < std::tie(b.owner, b.entity, b.line, b.code);
        });
        return std::move(v.result);
    }
}
