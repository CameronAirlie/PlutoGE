#pragma once

#include "PlutoGE/scene/ui/UITheme.h"
#include "PlutoGE/scene/ui/UIThemeResolver.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace PlutoGE::ui
{
    // Identifies a theme being edited: a .plutotheme asset, or the local
    // styles of a theme component in the open scene.
    struct UIThemeTarget
    {
        enum class Kind
        {
            Asset,
            LocalStyles,
        };
        Kind kind = Kind::Asset;
        std::string asset;
        std::uint32_t entity = 0;

        static UIThemeTarget ForAsset(std::string reference) { return {Kind::Asset, std::move(reference), 0}; }
        static UIThemeTarget ForLocalStyles(std::uint32_t entity) { return {Kind::LocalStyles, {}, entity}; }
        bool operator==(const UIThemeTarget &) const = default;
    };

    // Where a document's theme lives. Assets and local styles are built in;
    // other homes (tests, generated themes) implement this.
    class UIThemeStorage
    {
    public:
        virtual ~UIThemeStorage() = default;
        [[nodiscard]] virtual std::string GetLabel() const = 0;
        // False once the theme's home is gone (a deleted entity).
        [[nodiscard]] virtual bool IsAvailable() const { return true; }
        // True when edits need an explicit Save; false when they apply directly.
        [[nodiscard]] virtual bool SavesSeparately() const = 0;
        virtual bool Read(scene::UITheme &theme, std::vector<std::string> &problems, std::string &error) = 0;
        // Shows the current edits to the scene; `unsaved` is false once they match what is stored.
        virtual void Preview(const scene::UITheme &theme, bool unsaved) = 0;
        virtual bool Save(const scene::UITheme &theme, std::string &error) = 0;
        // Fills `theme` and returns true when the stored theme changed elsewhere.
        virtual bool PollExternalChange(const scene::UITheme &stored, scene::UITheme &theme, std::vector<std::string> &problems) = 0;
        // Themes beneath this one other than its own base themes, lowest priority first.
        virtual void AppendUnderlyingLayers(std::vector<std::shared_ptr<const scene::UITheme>> &, std::vector<std::string> &) {}
        // The document is closing: drop any preview of unsaved edits.
        virtual void EndPreview() {}
    };

    // An open theme with undo and live preview.
    class UIThemeDocument
    {
    public:
        // Opens an asset or local styles through the built-in storage.
        static std::unique_ptr<UIThemeDocument> Open(const UIThemeTarget &target, std::string &error);
        static std::unique_ptr<UIThemeDocument> Open(UIThemeTarget target, std::unique_ptr<UIThemeStorage> storage, std::string &error);
        ~UIThemeDocument();
        UIThemeDocument(const UIThemeDocument &) = delete;
        UIThemeDocument &operator=(const UIThemeDocument &) = delete;

        [[nodiscard]] const UIThemeTarget &GetTarget() const { return m_target; }
        [[nodiscard]] std::string GetLabel() const { return m_storage->GetLabel(); }
        [[nodiscard]] const scene::UITheme &GetTheme() const { return m_theme; }
        // Problems found when the theme was read (lines that did not parse).
        [[nodiscard]] const std::vector<std::string> &GetLoadErrors() const { return m_loadErrors; }
        [[nodiscard]] bool IsValid() const { return m_storage->IsAvailable(); }

        // Changes the theme. Changes made while a control is held (a drag, a
        // colour picker) become one undo step; call EndFrame once per frame.
        void Modify(const std::function<void(scene::UITheme &)> &edit);
        void Replace(scene::UITheme theme);
        void EndFrame();

        [[nodiscard]] bool CanUndo() const { return !m_undo.empty(); }
        [[nodiscard]] bool CanRedo() const { return !m_redo.empty(); }
        void Undo();
        void Redo();

        [[nodiscard]] bool SavesSeparately() const { return m_storage->SavesSeparately(); }
        [[nodiscard]] bool IsDirty() const;
        bool Save(std::string &error);
        void Revert();

        // This theme over everything beneath it.
        [[nodiscard]] const scene::UIResolvedTheme &GetResolved();
        // The same without this theme: what it inherits.
        [[nodiscard]] const scene::UIResolvedTheme &GetInherited();
        // Increments on every change, for views that cache.
        [[nodiscard]] std::uint64_t GetRevision() const { return m_revision; }

    private:
        UIThemeDocument(UIThemeTarget target, std::unique_ptr<UIThemeStorage> storage)
            : m_target(std::move(target)), m_storage(std::move(storage)) {}
        void Changed();
        void Resolve();

        UIThemeTarget m_target;
        std::unique_ptr<UIThemeStorage> m_storage;
        scene::UITheme m_theme;
        scene::UITheme m_saved;
        std::vector<std::string> m_loadErrors;
        std::vector<scene::UITheme> m_undo;
        std::vector<scene::UITheme> m_redo;
        bool m_groupOpen = false;
        std::uint64_t m_revision = 1;
        std::uint64_t m_resolvedRevision = 0;
        std::uint64_t m_resolvedAssets = 0;
        std::shared_ptr<const scene::UIResolvedTheme> m_resolved;
        std::shared_ptr<const scene::UIResolvedTheme> m_inherited;
    };

    // The open document for `target`, if the theme editor has one.
    [[nodiscard]] UIThemeDocument *FindOpenUIThemeDocument(const UIThemeTarget &target);

    // Applies `edit` to a theme: through its open document when there is one
    // (undoable, unsaved), otherwise straight to the file or component.
    bool EditUITheme(const UIThemeTarget &target, const std::function<void(scene::UITheme &)> &edit, std::string &error);

    // Writes a new theme asset in `directory` (made unique if the name is
    // taken) and returns its reference, or empty with `error` set.
    [[nodiscard]] std::string CreateUIThemeAsset(const std::filesystem::path &directory, const std::string &name,
                                                 const scene::UITheme &theme, std::string &error);
    // UI/Themes in the open project, where new themes go by default.
    [[nodiscard]] std::filesystem::path GetDefaultUIThemeDirectory();
}
