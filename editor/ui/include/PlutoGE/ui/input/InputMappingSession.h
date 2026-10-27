#pragma once

#include "PlutoGE/ui/InputMappingDocument.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace PlutoGE::ui
{
    // Where a mapping's JSON lives. Project assets use a file; tests and
    // generated mappings implement their own.
    class InputMappingStorage
    {
    public:
        virtual ~InputMappingStorage() = default;
        [[nodiscard]] virtual std::string GetLabel() const = 0;
        virtual bool Read(std::string &text, std::string &error) = 0;
        virtual bool Write(const std::string &text, std::string &error) = 0;
    };

    // Reads and writes `path`; a save goes through a temporary file so a
    // failed write cannot truncate the mapping.
    [[nodiscard]] std::unique_ptr<InputMappingStorage> MakeInputMappingFileStorage(std::filesystem::path path);

    // An open input mapping with undo and unsaved-change tracking.
    class InputMappingSession
    {
    public:
        // Null with `error` set when the mapping cannot be read or parsed;
        // nothing is opened then, so a mapping this editor cannot represent
        // is never overwritten.
        static std::unique_ptr<InputMappingSession> Open(std::unique_ptr<InputMappingStorage> storage, std::string &error);
        // Opens a project asset reference ("project://Input/Player.plutoinput").
        static std::unique_ptr<InputMappingSession> OpenAsset(const std::string &reference, std::string &error);

        [[nodiscard]] std::string GetLabel() const { return m_storage->GetLabel(); }
        [[nodiscard]] const InputMappingDocument &GetDocument() const { return m_document; }
        [[nodiscard]] const std::vector<InputMappingAction> &GetActions() const { return m_document.Actions(); }

        // Changes the mapping. Changes made while a control is held (a drag,
        // typing a name) become one undo step until EndFrame sees it released.
        void Modify(const std::function<void(InputMappingDocument &)> &edit);
        void EndFrame(bool controlHeld);

        [[nodiscard]] bool CanUndo() const { return !m_undo.empty(); }
        [[nodiscard]] bool CanRedo() const { return !m_redo.empty(); }
        void Undo();
        void Redo();

        [[nodiscard]] bool IsDirty() const { return m_text != m_savedText; }
        bool Save(std::string &error);
        void Revert();
        // Increments on every change, for views that cache derived data.
        [[nodiscard]] std::uint64_t GetRevision() const { return m_revision; }

    private:
        explicit InputMappingSession(std::unique_ptr<InputMappingStorage> storage) : m_storage(std::move(storage)) {}
        void Changed();

        std::unique_ptr<InputMappingStorage> m_storage;
        InputMappingDocument m_document;
        std::string m_text;      // m_document serialized
        std::string m_savedText; // what the storage holds, serialized the same way
        std::vector<InputMappingDocument> m_undo;
        std::vector<InputMappingDocument> m_redo;
        bool m_groupOpen = false;
        std::uint64_t m_revision = 1;
    };
}
