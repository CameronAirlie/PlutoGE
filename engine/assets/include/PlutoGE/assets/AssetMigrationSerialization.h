#pragma once
#include "PlutoGE/assets/AssetMigrationPlan.h"
#include <string_view>

namespace PlutoGE::assets
{
    // Prepares bytes only. Requires exact audited input and resolved mappings.
    // Unknown fields and original line endings survive unchanged. Publication
    // still requires backup, locked revalidation and a recovery journal.
    bool PrepareMaterialReferenceMigration(const MigrationReferenceFile &plan,
        std::string_view input, std::string &output, std::string *errorMessage = nullptr);
    // Version 1 shader graphs: Pass, TextureParameter.reference and Subgraph
    // node parameters only; names and expressions are never reference fields.
    bool PrepareShaderGraphReferenceMigration(const MigrationReferenceFile &plan,
        std::string_view input, std::string &output, std::string *errorMessage = nullptr);
}
