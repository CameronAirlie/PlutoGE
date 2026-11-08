#pragma once

#include <imgui.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::ui
{
    class EditorSettings;

    // Where a command's shortcut listens. The owner of the registry decides when each scope is active (for the
    // editor shell: Global outside text fields and popups, Editor also outside asset editors that take keys,
    // Edit also outside the Content Browser, Scene only while a scene panel has focus).
    enum class EditorCommandScope
    {
        Global,
        Editor,
        Edit,
        Scene,
    };

    // An editor action reachable from a keyboard shortcut and the command palette.
    struct EditorCommandDefinition
    {
        std::string id;           // Stable: stored with user key bindings ("scene.duplicate").
        std::string label;        // "Duplicate"
        std::string category;     // "Edit", "Scene", "Play", "File"...
        ImGuiKeyChord defaultShortcut = ImGuiKey_None;
        // A fixed second key (Pause for Pause, Ctrl+Shift+Z for Redo); active while the shortcut is not customised.
        ImGuiKeyChord alternateShortcut = ImGuiKey_None;
        EditorCommandScope scope = EditorCommandScope::Global;
        bool repeat = false; // Holding the keys repeats the command (Undo, Redo).
        bool showInPalette = true;
    };

    // Registered commands with their user-assignable shortcuts. Handlers are bound every frame by the code that
    // owns each action (so they can use that frame's state) and cleared at the start of the next frame; a command
    // without a bound handler is unavailable this frame and is hidden from the palette.
    class EditorCommandRegistry
    {
    public:
        explicit EditorCommandRegistry(EditorSettings *settings = nullptr) : m_settings(settings) {}

        void Register(EditorCommandDefinition definition);
        [[nodiscard]] const EditorCommandDefinition *Find(std::string_view id) const;
        [[nodiscard]] const std::vector<EditorCommandDefinition> &List() const { return m_commands; }

        // The user's binding when set (ImGuiKey_None to unbind), otherwise the default.
        [[nodiscard]] ImGuiKeyChord GetShortcut(std::string_view id) const;
        void SetShortcut(std::string_view id, ImGuiKeyChord chord);
        void ResetShortcut(std::string_view id);
        [[nodiscard]] bool IsCustomized(std::string_view id) const;
        // Another command using `chord`, for conflict warnings.
        [[nodiscard]] const EditorCommandDefinition *FindByShortcut(ImGuiKeyChord chord, std::string_view except = {}) const;

        void BeginFrame();
        void Bind(std::string_view id, std::function<void()> handler, bool enabled = true);
        [[nodiscard]] bool IsAvailable(std::string_view id) const;
        // Runs the first available command whose shortcut was pressed this frame in an active scope.
        // Returns true when one ran.
        bool DispatchShortcuts(const std::function<bool(EditorCommandScope)> &scopeActive);
        // The shortcut as menus show it ("Ctrl+Shift+S"), empty when unbound.
        [[nodiscard]] std::string GetShortcutText(std::string_view id) const;
        // Runs `id` at the next RunQueued (the palette queues, so its handler is the current frame's).
        void Queue(std::string_view id) { m_queued = std::string(id); }
        void RunQueued();
        bool Run(std::string_view id);
        // Call every frame a shortcut is being recorded: no shortcut runs then or the frame after, so recording Ctrl+S
        // does not also save. Lapses by itself once the recorder stops calling (closed window, other page).
        void MarkRecordingShortcut();

    private:
        struct Binding
        {
            std::string id;
            std::function<void()> handler;
            bool enabled = true;
        };
        [[nodiscard]] const Binding *FindBinding(std::string_view id) const;

        EditorSettings *m_settings = nullptr;
        std::vector<EditorCommandDefinition> m_commands;
        std::vector<Binding> m_bindings;
        std::string m_queued;
        int m_recordingFrame = -2;
    };

    // "Ctrl+Shift+S"; empty for ImGuiKey_None.
    [[nodiscard]] std::string FormatShortcut(ImGuiKeyChord chord);
    // A key pressed this frame with the held modifiers, for assigning shortcuts. Ignores lone modifier keys and mouse
    // buttons; Escape cancels and is never returned.
    [[nodiscard]] std::optional<ImGuiKeyChord> CapturePressedShortcut();

    // Preferences page listing every command with its shortcut, for rebinding.
    void RenderKeyboardShortcutsPage(EditorCommandRegistry &commands);

    // Case-insensitive subsequence match for the palette: -1 when `query` does not match `text`; higher is better
    // (consecutive letters and word starts score more, so "dup" ranks "Duplicate" above "Drop to Ground").
    [[nodiscard]] int FuzzyMatchScore(std::string_view text, std::string_view query);
}
