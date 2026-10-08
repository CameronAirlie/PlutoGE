#pragma once
#include "PlutoGE/assets/AssetMigrationPlan.h"
namespace PlutoGE::assets
{
    bool PlanMigrationRenames(const Project &project, const AssetMigrationOptions &options,
        AssetMigrationPlan &candidate, std::string *errorMessage, std::stop_token stop);
    std::string ResolveMigrationRename(const Project &project, const AssetMigrationPlan &plan, const std::string &reference);
}
