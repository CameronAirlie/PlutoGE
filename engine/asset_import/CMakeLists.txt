add_library(PlutoGEAssetImport
    src/ArtifactCollection.cpp
    src/AssetMigrationBackup.cpp
    src/ModelNodeRepairService.cpp
    src/ModelObjectExtractionService.cpp
    src/ModelGenerationExtraction.cpp
    src/AssetMoveService.cpp
    src/ImportWatch.cpp
    src/ImportDependencyIndex.cpp
    src/ImportState.cpp
    src/ModelImportTask.cpp
    src/ModelGenerationPublication.cpp
    src/ModelCacheRestore.cpp
    src/ModelArtifactSettings.cpp
    src/ImportedTextureWriter.cpp
    src/ArtifactCache.cpp
    src/ImportFileTransaction.cpp
    src/ModelSourceSnapshot.cpp
    src/ModelOutputProvenance.cpp
    src/ModelImportPublication.cpp
    src/ModelImportService.cpp
)
add_library(PlutoGE::AssetImport ALIAS PlutoGEAssetImport)
target_include_directories(PlutoGEAssetImport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(PlutoGEAssetImport PUBLIC PlutoGE::Assets PlutoGE::Import)

find_package(Threads REQUIRED)
target_link_libraries(PlutoGEAssetImport PUBLIC Threads::Threads)
