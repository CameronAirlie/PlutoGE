#include "PlutoGE/platform/FilesystemPaths.h"
#include "PlutoGE/asset_import/ImportFileTransaction.h"
#include "PlutoGE/asset_import/ProjectImportLock.h"
#include "PlutoGE/assets/AssetMetadata.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace
{
    void Require(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }
    void Write(const std::filesystem::path &path, const std::string &bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary);
        output << bytes;
        output.close();
        Require(static_cast<bool>(output), "Cannot write fixture");
    }
    std::string Read(const std::filesystem::path &path)
    {
        std::ifstream input(path, std::ios::binary);
        Require(static_cast<bool>(input), "Cannot read fixture");
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }
    struct Scratch
    {
        std::filesystem::path root = std::filesystem::temp_directory_path() /
            ("PlutoGE-import-transaction-" + PlutoGE::assets::GenerateAssetId());
        Scratch() { std::filesystem::create_directories(root / "Assets"); }
        ~Scratch() { std::error_code ignored; std::filesystem::remove_all(root, ignored); }
    };
}

int main()
{
    using PlutoGE::assetimport::ImportFileTransaction;
    try
    {
        Scratch scratch;
        const auto assets = scratch.root / "Assets";
        Write(assets / "A", "old");
        std::string error;
        std::filesystem::path prospective = "sentinel";
        Require(PlutoGE::content::ResolveDirectoryForCreation(assets / "Missing/Deep", prospective, &error) &&
                prospective == std::filesystem::canonical(assets) / "Missing/Deep" && !std::filesystem::exists(assets / "Missing"),
                "Prospective directory resolution failed or created directories");
        const auto retainedProspective = prospective;
        Require(!PlutoGE::content::ResolveDirectoryForCreation(assets / "A/Child", prospective, &error) && prospective == retainedProspective,
                "File prefix was accepted or changed resolver output");
        std::error_code linkError;
        std::filesystem::create_directory_symlink(scratch.root / "MissingTarget", assets / "Dangling", linkError);
        if (!linkError) Require(!PlutoGE::content::ResolveDirectoryForCreation(assets / "Dangling/Child", prospective, &error),
                                "Dangling directory link was treated as a safe missing path");

        {
            PlutoGE::assetimport::ProjectImportLock competing;
            {
                PlutoGE::assetimport::ProjectImportLock held;
                Require(held.TryAcquire(scratch.root, &error), "Cannot acquire import lock");
                Require(!competing.TryAcquire(scratch.root, &error), "Concurrent project importer accepted");
            }
            Require(competing.TryAcquire(scratch.root, &error), "Released import lock was not reusable");
        }
        {
            ImportFileTransaction transaction(scratch.root);
            Write(transaction.GetOutputRoot() / "A", "new");
            Write(transaction.GetOutputRoot() / "B", "created");
            Require(transaction.Publish(assets, {"A", "B"}, &error), "Publication failed");
            Require(Read(assets / "A") == "new" && Read(assets / "B") == "created", "Published files missing");
        }
        Require(Read(assets / "A") == "old" && !std::filesystem::exists(assets / "B"), "RAII rollback failed");
        {
            ImportFileTransaction transaction(scratch.root);
            Write(transaction.GetOutputRoot() / "A", "accepted");
            Require(!transaction.Publish(assets, {"../A"}, &error), "Escaping path accepted");
            Require(transaction.Publish(assets, {"A"}, &error) && transaction.Accept(&error), "Accept failed");
        }
        Require(Read(assets / "A") == "accepted", "Accepted generation rolled back");
        {
            ImportFileTransaction transaction(scratch.root);
            Write(transaction.GetOutputRoot() / "Textures/Deep/Imported.tga", "pixels");
            Require(transaction.Publish(assets, {"Textures/Deep/Imported.tga"}, &error), "Missing nested output parents rejected");
            Require(Read(assets / "Textures/Deep/Imported.tga") == "pixels", "Nested output was not published");
        }
        Require(!std::filesystem::exists(assets / "Textures/Deep/Imported.tga"), "Nested output rollback failed");
        const auto interrupted = scratch.root / ".pluto-import-transactions" / "interrupted";
        Write(interrupted / "journal", "PLUTOIMPORT\t1\n\"A\" 1\n\"B\" 0\n");
        Write(interrupted / "Backup/A", "original-before-crash");
        Write(assets / "A", "unaccepted");
        Write(assets / "B", "unaccepted-new");
        if (!ImportFileTransaction::Recover(scratch.root, assets, &error)) throw std::runtime_error(error);
        Require(Read(assets / "A") == "original-before-crash" && !std::filesystem::exists(assets / "B"), "Recovery did not restore prior generation");
        Require(!std::filesystem::exists(interrupted), "Recovery journal retained after success");
        Require(ImportFileTransaction::Recover(scratch.root, assets, &error), "Repeated recovery failed");
        const auto accepted = scratch.root / ".pluto-import-transactions" / "accepted";
        Write(accepted / "accepted", "PLUTOIMPORT_ACCEPTED\n");
        Write(accepted / "Backup/A", "obsolete-original");
        Require(ImportFileTransaction::Recover(scratch.root, assets, &error) && Read(assets / "A") == "original-before-crash", "Accepted recovery restored obsolete data");
        for (const auto &badJournal : {std::string("PLUTOIMPORT\t1\n\"A\" 1\n\"unfinished"),
                                       std::string("PLUTOIMPORT\t1\n\"A\" 1\n\"A\" 0\n")})
        {
            const auto malformed = scratch.root / ".pluto-import-transactions" / "malformed";
            Write(malformed / "journal", badJournal);
            Write(malformed / "Backup/A", "must-not-restore-partial-journal");
            Require(!ImportFileTransaction::Recover(scratch.root, assets, &error) && Read(assets / "A") == "original-before-crash",
                    "Invalid journal applied a partial recovery");
            std::filesystem::remove_all(malformed);
        }
        const auto invalid = scratch.root / ".pluto-import-transactions" / "invalid";
        Write(invalid / "journal", "PLUTOIMPORT\t1\n\"../outside\" 0\n");
        Write(scratch.root / "outside", "protected");
        Require(!ImportFileTransaction::Recover(scratch.root, assets, &error) && Read(scratch.root / "outside") == "protected",
                "Recovery accepted an escaping path");
        Require(std::filesystem::exists(invalid / "journal"), "Failed recovery removed journal");
        std::cout << "Import transaction tests passed\n";
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
