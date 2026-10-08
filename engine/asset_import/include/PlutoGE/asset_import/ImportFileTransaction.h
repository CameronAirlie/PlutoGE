#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace PlutoGE::assetimport
{
    // Keeps backups outside disposable Library until validation accepts the
    // batch. Durable intent records allow startup recovery after interruption.
    class ImportFileTransaction
    {
    public:
        explicit ImportFileTransaction(const std::filesystem::path &projectRoot);
        ~ImportFileTransaction();
        ImportFileTransaction(const ImportFileTransaction &) = delete;
        ImportFileTransaction &operator=(const ImportFileTransaction &) = delete;

        const std::filesystem::path &GetOutputRoot() const noexcept { return m_outputRoot; }
        bool Publish(const std::filesystem::path &assetRoot,
                     const std::vector<std::filesystem::path> &relativeOutputs, std::string *errorMessage);
        bool Rollback(std::string *errorMessage = nullptr);
        bool Accept(std::string *errorMessage = nullptr);
        static bool Recover(const std::filesystem::path &projectRoot,
                            const std::filesystem::path &assetRoot, std::string *errorMessage = nullptr);

    private:
        struct PublishedFile
        {
            std::filesystem::path destination;
            std::filesystem::path backup;
            bool hadOriginal = false;
            bool installed = false;
        };
        std::filesystem::path m_root;
        std::filesystem::path m_outputRoot;
        std::vector<PublishedFile> m_published;
        bool m_accepted = false;
    };
}
