#include "PlutoGE/asset_import/ModelImportService.h"
#include "PlutoGE/asset_import/ImportState.h"
#include "PlutoGE/asset_import/ImportDependencyIndex.h"
#include "PlutoGE/asset_import/ModelSourceSnapshot.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/assets/AssetMigrationAudit.h"
#include "PlutoGE/assets/AssetMigrationPlan.h"

#include <algorithm>
#include <exception>
#include <iostream>
#include <iomanip>
#include <string_view>
#include <vector>

namespace
{
    void Usage()
    {
        std::cerr << "Usage: PlutoGEImportModel <project.plutoproject> <project://source.fbx|--all> [--force]\n"
                     "       PlutoGEImportModel <project.plutoproject> --check\n"
                     "       PlutoGEImportModel <project.plutoproject> --audit\n"
                     "       PlutoGEImportModel <project.plutoproject> --inspect-source <project://source.glb>\n"
                     "       PlutoGEImportModel <project.plutoproject> --plan-migration [--renamed <old-project-ref> <new-project-ref>]...\n"
                     "       PlutoGEImportModel <project.plutoproject> --affected <absolute-input-path>\n";
    }
}

int main(int argc, char **argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help") { Usage(); return 0; }
    if (argc < 3 || (std::string_view(argv[2]) != "--plan-migration" &&
        ((argc != 3 && argc != 4) || (argc == 4 && std::string_view(argv[2]) != "--affected" && std::string_view(argv[2]) != "--inspect-source" && std::string_view(argv[3]) != "--force"))))
    { Usage(); return 2; }
    try
    {
        std::string error;
        auto project = PlutoGE::assets::Project::Load(argv[1], &error);
        if (!project) { std::cerr << error << '\n'; return 1; }
        if (std::string_view(argv[2]) == "--inspect-source")
        {
            if (argc != 4 || !PlutoGE::assets::Project::IsProjectAssetReference(argv[3]) ||
                PlutoGE::assets::Project::GetAssetTypeForReference(argv[3]) != PlutoGE::assets::ProjectAssetType::Model)
            { Usage(); return 2; }
            PlutoGE::assetimport::MeshImportOptions options;
            PlutoGE::assetimport::ModelSourceSnapshot snapshot;
            if (!PlutoGE::assetimport::ModelImportService{}.ReadOptions(*project, argv[3], options, &error) ||
                !PlutoGE::assetimport::ReadModelSourceSnapshot(project->ResolveAssetReference(argv[3]), options, snapshot, &error))
            { std::cerr << error << '\n'; return 1; }
            const auto source = project->ResolveAssetReference(argv[3]);
            auto object = [&](std::string_view type, const std::string &name, const std::string &key)
            { std::cout << "OBJECT\t" << type << '\t' << std::quoted(name) << '\t' << std::quoted(key) << '\n'; };
            const auto &imported = snapshot.imported;
            if (!imported.meshData.vertices.empty() && !imported.meshData.indices.empty()) object("Mesh", source.stem().string(), "mesh/main");
            for (std::size_t index = 0; index < imported.textures.size(); ++index)
            {
                const auto &texture = imported.textures[index];
                const auto name = texture.sourcePath.empty() ? "Texture " + std::to_string(index) : std::filesystem::path(texture.sourcePath).stem().string();
                const auto key = PlutoGE::assetimport::GetModelTextureSourceKey(source, texture, index);
                object("Texture", name, key);
            }
            for (std::size_t index = 0; index < imported.materials.size(); ++index)
                object("Material", "M_" + source.stem().string() + "_" + std::to_string(index), "material/slot/" + std::to_string(index));
            for (const auto &clip : imported.animations) object("Animation Clip", clip.name, "clip/" + clip.name);
            if (!imported.animations.empty()) object("Animation", source.stem().string(), "animation/set");
            return 0;
        }
        if (std::string_view(argv[2]) == "--plan-migration")
        {
            if ((argc - 3) % 3 != 0) { Usage(); return 2; }
            PlutoGE::assets::AssetMigrationOptions options;
            for (int index = 3; index < argc; index += 3)
            {
                if (std::string_view(argv[index]) != "--renamed") { Usage(); return 2; }
                options.confirmedRenames.push_back({argv[index + 1], argv[index + 2]});
            }
            PlutoGE::assets::AssetMigrationPlan plan;
            if (!PlutoGE::assets::PlanAssetReferenceMigration(*project, options, plan, &error))
            { std::cerr << error << '\n'; return 1; }
            for (std::size_t index = 0; index < plan.audit.issues.size(); ++index)
            {
                const auto &issue = plan.audit.issues[index];
                const bool resolved = std::find(plan.resolvedAuditIssues.begin(), plan.resolvedAuditIssues.end(), index) != plan.resolvedAuditIssues.end();
                std::cout << (resolved ? "proposed resolution: " : "unresolved: ") << issue.reference << ": " << issue.message << '\n';
            }
            for (const auto &evidence : plan.renames)
            {
                std::cout << "Confirmed rename: " << evidence.rename.previousReference << " -> " << evidence.rename.replacementReference
                    << " (preserve active ID " << evidence.replacementAssetId << ")\n";
                std::cout << "  quarantine only after verified backup: " << evidence.orphanMetadataReference << " ["
                    << PlutoGE::content::DigestToHex(evidence.orphanMetadataHash) << "]\n";
                std::cout << "  replacement metadata [" << PlutoGE::content::DigestToHex(evidence.replacementMetadataHash)
                    << "]; source [" << PlutoGE::content::DigestToHex(evidence.replacementContentHash) << "]\n";
            }
            if (!plan.catalogError.empty()) std::cout << "Catalog: " << plan.catalogError << '\n';
            std::size_t mappings = 0;
            for (const auto &file : plan.files)
            {
                std::cout << file.reference << " [" << PlutoGE::content::DigestToHex(file.contentHash) << "]" <<
                    (file.imported ? " (imported; reimport required)" : "") << '\n';
                for (const auto &mapping : file.mappings)
                {
                    std::cout << "  line " << mapping.line << ": " << mapping.previousReference << " -> " << mapping.logicalReference << '\n';
                    ++mappings;
                }
                for (const auto &diagnostic : file.diagnostics) std::cout << "  unresolved: " << diagnostic << '\n';
            }
            std::cout << mappings << " possible reference mappings. Dry run; no files changed.\n";
            return plan.HasBlockingIssues() ? 1 : plan.audit.issues.empty() ? 0 : 3;
        }
        if (std::string_view(argv[2]) == "--audit")
        {
            if (argc != 3) { Usage(); return 2; }
            PlutoGE::assets::AssetMigrationAudit audit;
            if (!PlutoGE::assets::AuditAssetMigration(*project, audit, &error))
            { std::cerr << error << '\n'; return 1; }
            for (const auto &issue : audit.issues)
            {
                std::cout << (issue.severity == PlutoGE::assets::MigrationIssueSeverity::Error ? "error: " : "warning: ")
                    << issue.reference << ": " << issue.message;
                if (!issue.relatedReference.empty()) std::cout << " (also " << issue.relatedReference << ")";
                std::cout << '\n';
            }
            std::cout << audit.assets << " assets, " << audit.metadataFiles << " metadata files, "
                << audit.issues.size() << " issues. Read-only inventory; no conversion performed.\n";
            return audit.HasBlockingIssues() ? 1 : audit.issues.empty() ? 0 : 3;
        }
        if (std::string_view(argv[2]) == "--affected")
        {
            if (argc != 4) { Usage(); return 2; }
            std::vector<std::string> sources;
            if (!PlutoGE::assetimport::FindAffectedModelImports(*project, {std::filesystem::path(argv[3])}, sources, &error))
            { std::cerr << error << '\n'; return 1; }
            for (const auto &source : sources) std::cout << source << '\n';
            return 0;
        }
        if (std::string_view(argv[2]) == "--check")
        {
            if (argc != 3) { Usage(); return 2; }
            std::vector<PlutoGE::assetimport::ImportAssessment> assessments;
            if (!PlutoGE::assetimport::ReconcileModelImports(*project, assessments, &error))
            { std::cerr << error << '\n'; return 1; }
            int exitCode = 0;
            for (const auto &assessment : assessments)
            {
                using Status = PlutoGE::assetimport::ImportReconciliationStatus;
                const auto status = assessment.status;
                std::cout << assessment.sourceReference << ": " <<
                    (status == Status::Current ? "current" : status == Status::Blocked ? "blocked" : "needs import") <<
                    " — " << assessment.reason << '\n';
                if (status == Status::Blocked) exitCode = 1;
                else if (status == Status::NeedsImport && exitCode == 0) exitCode = 3;
            }
            return exitCode;
        }
        const bool all = std::string_view(argv[2]) == "--all";
        const bool force = argc == 4;
        std::vector<std::string> sources;
        if (all)
        {
            project->RefreshAssetRegistry();
            for (const auto &entry : project->GetManifest().assetEntries)
                if (entry.type == PlutoGE::assets::ProjectAssetType::Model) sources.push_back(entry.reference);
            std::sort(sources.begin(), sources.end());
            sources.erase(std::unique(sources.begin(), sources.end()), sources.end());
        }
        else
        {
            if (!PlutoGE::assets::Project::IsProjectAssetReference(argv[2])) { Usage(); return 2; }
            sources.emplace_back(argv[2]);
        }
        std::size_t failures = 0;
        for (const auto &source : sources)
        {
            PlutoGE::assetimport::ModelImportRequest request;
            request.sourceReference = source;
            request.forceReimport = force;
            request.progress = [&](std::string_view stage) { std::clog << source << ": " << stage << '\n'; };
            PlutoGE::assetimport::ModelImportResult result;
            if (!PlutoGE::assetimport::ModelImportService{}.Import(*project, request, result, &error))
            {
                std::cerr << source << ": " << error << '\n';
                ++failures;
                continue;
            }
            std::cout << result.modelReference << '\n';
        }
        if (all) std::clog << (sources.size() - failures) << " imported, " << failures << " failed.\n";
        return failures == 0 ? 0 : 1;
    }
    catch (const std::exception &exception)
    {
        std::cerr << "Model import command failed: " << exception.what() << '\n';
        return 1;
    }
}
