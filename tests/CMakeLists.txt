add_executable(PlutoGELargeMeshOptimizationTests
    LargeMeshOptimizationTests.cpp
)

target_link_libraries(PlutoGELargeMeshOptimizationTests PRIVATE
    PlutoGE::Import
    PlutoGE::Render
)

add_test(NAME PlutoGELargeMeshOptimizationTests COMMAND PlutoGELargeMeshOptimizationTests)

add_executable(PlutoGEModelAssetTests
    ModelAssetTests.cpp
)

target_link_libraries(PlutoGEModelAssetTests PRIVATE
    PlutoGE::Assets
)

add_test(NAME PlutoGEModelAssetTests COMMAND PlutoGEModelAssetTests)
