add_executable(PlutoGELargeMeshOptimizationTests
    LargeMeshOptimizationTests.cpp
)

target_link_libraries(PlutoGELargeMeshOptimizationTests PRIVATE
    PlutoGE::Import
    PlutoGE::Render
)

add_test(NAME PlutoGELargeMeshOptimizationTests COMMAND PlutoGELargeMeshOptimizationTests)

if (TARGET OpenAL)
    add_custom_command(TARGET PlutoGELargeMeshOptimizationTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:OpenAL>
            $<TARGET_FILE_DIR:PlutoGELargeMeshOptimizationTests>/$<TARGET_FILE_NAME:OpenAL>
    )
endif()

add_executable(PlutoGEModelAssetTests
    ModelAssetTests.cpp
)

target_link_libraries(PlutoGEModelAssetTests PRIVATE
    PlutoGE::Assets
)

add_test(NAME PlutoGEModelAssetTests COMMAND PlutoGEModelAssetTests)

add_executable(PlutoGEPostProcessInitializationTests
    PostProcessInitializationTests.cpp
)

target_link_libraries(PlutoGEPostProcessInitializationTests PRIVATE
    PlutoGE::Render
)

add_test(NAME PlutoGEPostProcessInitializationTests COMMAND PlutoGEPostProcessInitializationTests)

add_executable(PlutoGERuntimeUITests
    RuntimeUITests.cpp
)

target_link_libraries(PlutoGERuntimeUITests PRIVATE
    PlutoGE::Scene
)

add_test(NAME PlutoGERuntimeUITests COMMAND PlutoGERuntimeUITests)

if (TARGET OpenAL)
    add_custom_command(TARGET PlutoGEPostProcessInitializationTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:OpenAL>
            $<TARGET_FILE_DIR:PlutoGEPostProcessInitializationTests>/$<TARGET_FILE_NAME:OpenAL>
    )
endif()
