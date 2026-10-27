#include "PlutoGE/ui/input/InputMappingSession.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/ui/EditorShell.h"

#include <fstream>
#include <iterator>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr std::size_t kUndoLimit = 200;

        class FileStorage final : public InputMappingStorage
        {
        public:
            explicit FileStorage(std::filesystem::path path) : m_path(std::move(path)) {}

            std::string GetLabel() const override { return m_path.filename().string(); }

            bool Read(std::string &text, std::string &error) override
            {
                std::ifstream in(m_path, std::ios::binary);
                if (!in)
                {
                    error = "Could not open " + m_path.string();
                    return false;
                }
                text.assign(std::istreambuf_iterator<char>(in), {});
                return true;
            }

            bool Write(const std::string &text, std::string &error) override
            {
                const auto temporary = std::filesystem::path(m_path).concat(".saving");
                {
                    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
                    out << text;
                    if (!out)
                    {
                        error = "Could not write " + temporary.string();
                        return false;
                    }
                }
                std::error_code code;
                std::filesystem::rename(temporary, m_path, code);
                if (code)
                {
                    error = "Could not replace the mapping: " + code.message();
                    return false;
                }
                return true;
            }

        private:
            std::filesystem::path m_path;
        };
    }

    std::unique_ptr<InputMappingStorage> MakeInputMappingFileStorage(std::filesystem::path path)
    {
        return std::make_unique<FileStorage>(std::move(path));
    }

    std::unique_ptr<InputMappingSession> InputMappingSession::Open(std::unique_ptr<InputMappingStorage> storage, std::string &error)
    {
        std::unique_ptr<InputMappingSession> session(new InputMappingSession(std::move(storage)));
        std::string text;
        if (!session->m_storage->Read(text, error))
            return nullptr;
        std::string parseError;
        if (!session->m_document.Parse(text, &parseError))
        {
            error = "This mapping cannot be edited here (" + parseError + "). Fix the file by hand; it was left unchanged.";
            return nullptr;
        }
        session->m_text = session->m_savedText = session->m_document.Serialize();
        return session;
    }

    std::unique_ptr<InputMappingSession> InputMappingSession::OpenAsset(const std::string &reference, std::string &error)
    {
        const auto *project = EditorShell::GetInstance().GetProject();
        if (!project)
        {
            error = "Open a project to edit input mappings.";
            return nullptr;
        }
        return Open(MakeInputMappingFileStorage(project->ResolveAssetReference(reference)), error);
    }

    void InputMappingSession::Modify(const std::function<void(InputMappingDocument &)> &edit)
    {
        InputMappingDocument before = m_document;
        edit(m_document);
        std::string text = m_document.Serialize();
        if (text == m_text)
            return;
        if (!m_groupOpen)
        {
            m_undo.push_back(std::move(before));
            if (m_undo.size() > kUndoLimit)
                m_undo.erase(m_undo.begin());
            m_redo.clear();
            m_groupOpen = true;
        }
        m_text = std::move(text);
        ++m_revision;
    }

    void InputMappingSession::EndFrame(bool controlHeld)
    {
        if (!controlHeld)
            m_groupOpen = false;
    }

    void InputMappingSession::Undo()
    {
        if (m_undo.empty())
            return;
        m_redo.push_back(std::move(m_document));
        m_document = std::move(m_undo.back());
        m_undo.pop_back();
        Changed();
    }

    void InputMappingSession::Redo()
    {
        if (m_redo.empty())
            return;
        m_undo.push_back(std::move(m_document));
        m_document = std::move(m_redo.back());
        m_redo.pop_back();
        Changed();
    }

    bool InputMappingSession::Save(std::string &error)
    {
        if (!m_storage->Write(m_text, error))
            return false;
        m_savedText = m_text;
        return true;
    }

    void InputMappingSession::Revert()
    {
        InputMappingDocument saved;
        if (!saved.Parse(m_savedText))
            return;
        m_groupOpen = false;
        Modify([&](InputMappingDocument &document) { document = std::move(saved); });
        m_groupOpen = false;
    }

    void InputMappingSession::Changed()
    {
        m_groupOpen = false;
        m_text = m_document.Serialize();
        ++m_revision;
    }
}
