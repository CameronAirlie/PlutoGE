add_library(PlutoGEAssets
    src/ModelActiveGeneration.cpp
    src/ModelGenerationRetention.cpp
    src/ModelGenerationSnapshot.cpp
    src/ArtifactGenerationLock.cpp
    src/SceneModelInstanceRecord.cpp
    src/ModelInstanceState.cpp
    src/ModelInstanceReconciliation.cpp
    src/ModelHierarchyAsset.cpp
    src/ModelNodeCorrespondence.cpp
    src/ModelArtifactStorage.cpp
    src/AssetStorageMap.cpp
    src/AssetMigrationSerialization.cpp
    src/AssetMigrationRenames.cpp
    src/AssetMigrationPlan.cpp
    src/AssetMigrationAudit.cpp
    src/AssetPathPolicy.cpp
    src/ProjectAssetLock.cpp
    src/ModelSourcePackage.cpp
    src/ModelImportSettings.cpp
    src/MaterialAssetSerialization.cpp
    src/AssetCatalogSerialization.cpp
    src/AssetCatalog.cpp
    src/AssetReference.cpp
    src/AssetMetadata.cpp
    src/AssetType.cpp
    src/AssetDatabase.cpp
    src/AssetReferences.cpp
    src/ProjectValidation.cpp
    src/ModelAsset.cpp
    src/AssetManager.cpp
    src/SurfaceResponseAsset.cpp
    src/LoadingScreenAsset.cpp
    src/ParticleSystemAsset.cpp
    src/PostProcessPresetAsset.cpp
    src/Project.cpp
)

add_library(PlutoGE::Assets ALIAS PlutoGEAssets)

target_link_libraries(PlutoGEAssets PUBLIC
    PlutoGE::Math
    PlutoGE::Platform
    PlutoGE::Render
    PlutoGE::glm
)

target_include_directories(PlutoGEAssets PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
