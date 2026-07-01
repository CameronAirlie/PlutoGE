add_executable(PlutoGELargeMeshOptimizationTests
    LargeMeshOptimizationTests.cpp
)

target_link_libraries(PlutoGELargeMeshOptimizationTests PRIVATE
    PlutoGE::Import
    PlutoGE::Render
)

add_test(NAME PlutoGELargeMeshOptimizationTests COMMAND PlutoGELargeMeshOptimizationTests)
