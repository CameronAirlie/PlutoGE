#include "PlutoGE/ui/theme/UIThemeDocument.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/UIStyleComponents.h"
#include "PlutoGE/scene/ui/UIThemeAssets.h"
#include "PlutoGE/scene/ui/UIThemeFormat.h"
#include "PlutoGE/ui/EditorShell.h"

#include <imgui.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr std::size_t kUndoLimit = 200;

        std::vector<UIThemeDocument *> &OpenDocuments()
        {
            static std::vector<UIThemeDocument *> documents;
            return documents;
        }

        scene::Entity *FindEntity(std::uint32_t id)
        {
            auto *scene = EditorShell::GetInstance().GetScene();
            return scene ? scene->FindEntityByID(id) : nullptr;
        }

        bool ReadText(const std::filesystem::path &path, std::string &text, std::string &error)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
            {
                error = "Cannot read " + path.string();
                return false;
            }
            std::stringstream stream;
            stream << file.rdbuf();
            text = stream.str();
            return true;
        }

        bool WriteText(const std::filesystem::path &path, const std::string &text, std::string &error)
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file << text;
            file.flush();
            if (!file)
            {
                error = "Cannot write " + path.string();
                return false;
            }
            return true;
        }

        std::filesystem::path ResolveAsset(const std::string &reference)
        {
            const auto *project = EditorShell::GetInstance().GetProject();
            return project ? project->ResolveAssetReference(reference) : std::filesystem::path{};
        }

        void WriteLocalStyles(scene::Entity &entity, scene::UIThemeComponent &component, std::string text)
        {
            component.SetLocalStyles(std::move(text));
            entity.AddPrefabOverride("Component:UIStyleSheetComponent:Rules");
            EditorShell::GetInstance().MarkSceneDirty();
        }

        // A .plutotheme file. Unsaved edits show everywhere as a live theme.
        class AssetStorage final : public UIThemeStorage
        {
        public:
            explicit AssetStorage(std::string reference) : m_reference(std::move(reference)) {}

            std::string GetLabel() const override { return std::filesystem::path(m_reference).filename().string(); }
            bool SavesSeparately() const override { return true; }

            bool Read(scene::UITheme &theme, std::vector<std::string> &problems, std::string &error) override
            {
                std::string text;
                if (!ReadText(ResolveAsset(m_reference), text, error))
                    return false;
                theme = scene::ParseUITheme(text, &problems);
                return true;
            }

            void Preview(const scene::UITheme &theme, bool unsaved) override
            {
                auto &assets = scene::UIThemeAssets::Get();
                if (unsaved)
                    assets.SetLiveTheme(m_reference, theme);
                else
                    assets.ClearLiveTheme(m_reference);
            }

            bool Save(const scene::UITheme &theme, std::string &error) override
            {
                if (!WriteText(ResolveAsset(m_reference), scene::WriteUITheme(theme), error))
                    return false;
                scene::UIThemeAssets::Get().Invalidate(); // re-read now rather than at the next file check
                return true;
            }

            bool PollExternalChange(const scene::UITheme &stored, scene::UITheme &theme, std::vector<std::string> &problems) override
            {
                const auto &entry = scene::UIThemeAssets::Get().Load(m_reference);
                if (!entry.theme || *entry.theme == stored)
                    return false;
                theme = *entry.theme;
                problems = entry.errors;
                return true;
            }

            void EndPreview() override { scene::UIThemeAssets::Get().ClearLiveTheme(m_reference); }

        private:
            std::string m_reference;
        };

        // The local styles of a theme component; edits apply to the scene directly.
        class LocalStylesStorage final : public UIThemeStorage
        {
        public:
            explicit LocalStylesStorage(std::uint32_t entity) : m_entity(entity) {}

            std::string GetLabel() const override
            {
                const auto *entity = FindEntity(m_entity);
                return "Local styles on " + (entity ? entity->GetName() : std::string("<missing>"));
            }
            bool IsAvailable() const override { return Component() != nullptr; }
            bool SavesSeparately() const override { return false; }

            bool Read(scene::UITheme &theme, std::vector<std::string> &problems, std::string &error) override
            {
                const auto *component = Component();
                if (!component)
                {
                    error = "The entity no longer has a theme component";
                    return false;
                }
                theme = component->GetLocalTheme();
                problems = component->GetLocalErrors();
                m_written = component->GetLocalStyles();
                return true;
            }

            void Preview(const scene::UITheme &theme, bool) override
            {
                auto *entity = FindEntity(m_entity);
                auto *component = entity ? entity->GetComponent<scene::UIThemeComponent>() : nullptr;
                if (!component)
                    return;
                m_written = scene::WriteUITheme(theme);
                if (component->GetLocalStyles() != m_written)
                    WriteLocalStyles(*entity, *component, m_written);
            }

            bool Save(const scene::UITheme &, std::string &) override { return true; }

            bool PollExternalChange(const scene::UITheme &, scene::UITheme &theme, std::vector<std::string> &problems) override
            {
                // Scene undo or a script may have replaced the text.
                const auto *component = Component();
                if (!component || component->GetLocalStyles() == m_written)
                    return false;
                m_written = component->GetLocalStyles();
                theme = component->GetLocalTheme();
                problems = component->GetLocalErrors();
                return true;
            }

            void AppendUnderlyingLayers(std::vector<std::shared_ptr<const scene::UITheme>> &layers, std::vector<std::string> &errors) override
            {
                auto *entity = FindEntity(m_entity);
                if (!entity)
                    return;
                if (const auto *parent = entity->GetParent())
                    if (const auto outer = scene::FindEffectiveUITheme(*parent))
                        layers = outer->GetLayers();
                if (const auto *component = Component(); component && !component->GetThemeAsset().empty())
                    scene::UIThemeAssets::Get().AppendLayers(component->GetThemeAsset(), layers, errors);
            }

        private:
            const scene::UIThemeComponent *Component() const
            {
                const auto *entity = FindEntity(m_entity);
                return entity ? entity->GetComponent<scene::UIThemeComponent>() : nullptr;
            }

            std::uint32_t m_entity;
            std::string m_written;
        };
    }

    std::unique_ptr<UIThemeDocument> UIThemeDocument::Open(const UIThemeTarget &target, std::string &error)
    {
        std::unique_ptr<UIThemeStorage> storage;
        if (target.kind == UIThemeTarget::Kind::Asset)
            storage = std::make_unique<AssetStorage>(target.asset);
        else
            storage = std::make_unique<LocalStylesStorage>(target.entity);
        return Open(target, std::move(storage), error);
    }

    std::unique_ptr<UIThemeDocument> UIThemeDocument::Open(UIThemeTarget target, std::unique_ptr<UIThemeStorage> storage, std::string &error)
    {
        std::unique_ptr<UIThemeDocument> document(new UIThemeDocument(std::move(target), std::move(storage)));
        if (!document->m_storage->Read(document->m_theme, document->m_loadErrors, error))
            return nullptr;
        document->m_saved = document->m_theme;
        OpenDocuments().push_back(document.get());
        return document;
    }

    UIThemeDocument::~UIThemeDocument()
    {
        std::erase(OpenDocuments(), this);
        m_storage->EndPreview();
    }

    void UIThemeDocument::Modify(const std::function<void(scene::UITheme &)> &edit)
    {
        scene::UITheme before = m_theme;
        edit(m_theme);
        if (m_theme == before)
            return;
        if (!m_groupOpen)
        {
            m_undo.push_back(std::move(before));
            if (m_undo.size() > kUndoLimit)
                m_undo.erase(m_undo.begin());
            m_redo.clear();
            m_groupOpen = true;
        }
        Changed();
    }

    void UIThemeDocument::Replace(scene::UITheme theme)
    {
        Modify([&](scene::UITheme &current) { current = std::move(theme); });
    }

    void UIThemeDocument::EndFrame()
    {
        // A held control keeps its edits in one undo step until released.
        if (m_groupOpen && !ImGui::IsAnyItemActive())
            m_groupOpen = false;
        if (m_groupOpen || IsDirty())
            return;
        // Follow edits made elsewhere while nothing is pending here.
        scene::UITheme external;
        if (m_storage->PollExternalChange(m_saved, external, m_loadErrors))
        {
            m_theme = m_saved = std::move(external);
            ++m_revision;
        }
    }

    void UIThemeDocument::Undo()
    {
        if (m_undo.empty())
            return;
        m_redo.push_back(std::move(m_theme));
        m_theme = std::move(m_undo.back());
        m_undo.pop_back();
        m_groupOpen = false;
        Changed();
    }

    void UIThemeDocument::Redo()
    {
        if (m_redo.empty())
            return;
        m_undo.push_back(std::move(m_theme));
        m_theme = std::move(m_redo.back());
        m_redo.pop_back();
        m_groupOpen = false;
        Changed();
    }

    bool UIThemeDocument::IsDirty() const
    {
        return SavesSeparately() && m_theme != m_saved;
    }

    bool UIThemeDocument::Save(std::string &error)
    {
        if (!m_storage->Save(m_theme, error))
            return false;
        m_saved = m_theme;
        m_loadErrors.clear();
        m_storage->Preview(m_theme, false);
        return true;
    }

    void UIThemeDocument::Revert()
    {
        Modify([this](scene::UITheme &theme) { theme = m_saved; });
        m_groupOpen = false;
    }

    void UIThemeDocument::Changed()
    {
        ++m_revision;
        if (!SavesSeparately())
            m_saved = m_theme; // applied directly, so nothing is pending
        m_storage->Preview(m_theme, IsDirty());
    }

    void UIThemeDocument::Resolve()
    {
        auto &assets = scene::UIThemeAssets::Get();
        if (m_resolved && m_resolvedRevision == m_revision && m_resolvedAssets == assets.GetRevision())
            return;
        std::vector<std::shared_ptr<const scene::UITheme>> layers;
        std::vector<std::string> errors;
        m_storage->AppendUnderlyingLayers(layers, errors);
        for (const auto &base : m_theme.baseThemes)
            assets.AppendLayers(base, layers, errors);
        m_inherited = scene::ResolveUITheme(layers, errors);
        layers.push_back(std::make_shared<scene::UITheme>(m_theme));
        m_resolved = scene::ResolveUITheme(std::move(layers), std::move(errors));
        m_resolvedRevision = m_revision;
        m_resolvedAssets = assets.GetRevision();
    }

    const scene::UIResolvedTheme &UIThemeDocument::GetResolved()
    {
        Resolve();
        return *m_resolved;
    }

    const scene::UIResolvedTheme &UIThemeDocument::GetInherited()
    {
        Resolve();
        return *m_inherited;
    }

    UIThemeDocument *FindOpenUIThemeDocument(const UIThemeTarget &target)
    {
        for (auto *document : OpenDocuments())
            if (document->GetTarget() == target)
                return document;
        return nullptr;
    }

    bool EditUITheme(const UIThemeTarget &target, const std::function<void(scene::UITheme &)> &edit, std::string &error)
    {
        if (auto *document = FindOpenUIThemeDocument(target))
        {
            document->Modify(edit);
            return true;
        }
        auto document = UIThemeDocument::Open(target, error);
        if (!document)
            return false;
        if (!document->GetLoadErrors().empty())
        {
            error = "Fix the errors in " + document->GetLabel() + " first: " + document->GetLoadErrors().front();
            return false;
        }
        document->Modify(edit);
        return document->Save(error);
    }

    std::filesystem::path GetDefaultUIThemeDirectory()
    {
        const auto *project = EditorShell::GetInstance().GetProject();
        return project ? project->ResolveAssetReference("project://UI/Themes") : std::filesystem::path{};
    }

    std::string CreateUIThemeAsset(const std::filesystem::path &directory, const std::string &name, const scene::UITheme &theme,
                                   std::string &error)
    {
        auto &shell = EditorShell::GetInstance();
        auto *project = shell.GetProject();
        if (!project || directory.empty())
        {
            error = "No project is open";
            return {};
        }
        std::error_code ignored;
        std::filesystem::create_directories(directory, ignored);
        auto path = directory / (name + ".plutotheme");
        for (int suffix = 2; std::filesystem::exists(path); ++suffix)
            path = directory / (name + "-" + std::to_string(suffix) + ".plutotheme");
        if (!WriteText(path, scene::WriteUITheme(theme), error))
            return {};
        shell.RefreshProjectAssets();
        return project->MakeAssetReference(path);
    }
}
