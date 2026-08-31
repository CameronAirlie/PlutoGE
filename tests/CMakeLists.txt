add_executable(PlutoGERhiTypesTests RhiTypesTests.cpp)
target_link_libraries(PlutoGERhiTypesTests PRIVATE PlutoGE::Render)
target_include_directories(PlutoGERhiTypesTests PRIVATE ${CMAKE_SOURCE_DIR}/engine/render/src)
add_test(NAME PlutoGERhiTypesTests COMMAND PlutoGERhiTypesTests)

add_executable(PlutoGERhiPostProcessAdapterTests RhiPostProcessAdapterTests.cpp)
if (CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED)
    target_link_libraries(PlutoGERhiPostProcessAdapterTests PRIVATE
        "$<LINK_GROUP:RESCAN,PlutoGE::Render,PlutoGE::Assets,PlutoGE::Core,PlutoGE::Scene,PlutoGE::Import,PlutoGE::Scripting>")
else()
    target_link_libraries(PlutoGERhiPostProcessAdapterTests PRIVATE
        PlutoGE::Render PlutoGE::Assets PlutoGE::Core PlutoGE::Scene PlutoGE::Import PlutoGE::Scripting)
endif()
add_test(NAME PlutoGERhiPostProcessAdapterTests COMMAND PlutoGERhiPostProcessAdapterTests)

add_executable(PlutoGEPostProcessGraphTests PostProcessGraphTests.cpp)
target_link_libraries(PlutoGEPostProcessGraphTests PRIVATE PlutoGE::Render)
add_test(NAME PlutoGEPostProcessGraphTests COMMAND PlutoGEPostProcessGraphTests)

add_executable(PlutoGEPostProcessResourcePoolTests PostProcessResourcePoolTests.cpp)
target_link_libraries(PlutoGEPostProcessResourcePoolTests PRIVATE PlutoGE::Render)
add_test(NAME PlutoGEPostProcessResourcePoolTests COMMAND PlutoGEPostProcessResourcePoolTests)

add_executable(PlutoGEWindowClientApiTests WindowClientApiTests.cpp)
target_link_libraries(PlutoGEWindowClientApiTests PRIVATE PlutoGE::Platform)
add_test(NAME PlutoGEWindowClientApiTests COMMAND PlutoGEWindowClientApiTests)

add_executable(PlutoGEEngineGraphicsApiTests EngineGraphicsApiTests.cpp)
target_link_libraries(PlutoGEEngineGraphicsApiTests PRIVATE PlutoGE::Core)
add_test(NAME PlutoGEEngineGraphicsApiTests COMMAND PlutoGEEngineGraphicsApiTests)

add_executable(PlutoGEOpenGLRhiTests OpenGLRhiTests.cpp)
target_link_libraries(PlutoGEOpenGLRhiTests PRIVATE PlutoGE::Render PlutoGE::Platform)
target_compile_definitions(PlutoGEOpenGLRhiTests PRIVATE
    PLUTO_RHI_TEST_SHADER_DIR="${CMAKE_BINARY_DIR}/engine/render/shaders")
add_test(NAME PlutoGEOpenGLRhiTests COMMAND PlutoGEOpenGLRhiTests)

add_executable(PlutoGEVulkanBootstrapTests VulkanBootstrapTests.cpp)
target_link_libraries(PlutoGEVulkanBootstrapTests PRIVATE PlutoGE::Render)
add_test(NAME PlutoGEVulkanBootstrapTests COMMAND PlutoGEVulkanBootstrapTests)

add_executable(PlutoGEVulkanRhiTests VulkanRhiTests.cpp)
target_link_libraries(PlutoGEVulkanRhiTests PRIVATE PlutoGE::Render)
target_compile_definitions(PlutoGEVulkanRhiTests PRIVATE
    PLUTO_RHI_TEST_SHADER_DIR="${CMAKE_BINARY_DIR}/engine/render/shaders")
add_test(NAME PlutoGEVulkanRhiTests COMMAND PlutoGEVulkanRhiTests)

add_executable(PlutoGEVulkanPresentationTests VulkanPresentationTests.cpp)
target_link_libraries(PlutoGEVulkanPresentationTests PRIVATE PlutoGE::Render PlutoGE::Platform)
target_compile_definitions(PlutoGEVulkanPresentationTests PRIVATE
    PLUTO_RHI_TEST_SHADER_DIR="${CMAKE_BINARY_DIR}/engine/render/shaders")
add_test(NAME PlutoGEVulkanPresentationTests COMMAND PlutoGEVulkanPresentationTests)

add_executable(PlutoGELargeMeshOptimizationTests
    LargeMeshOptimizationTests.cpp
)

if (CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED)
    target_link_libraries(PlutoGELargeMeshOptimizationTests PRIVATE
        "$<LINK_GROUP:RESCAN,PlutoGE::Import,PlutoGE::Render,PlutoGE::Assets,PlutoGE::Core,PlutoGE::Scene,PlutoGE::Scripting>"
    )
else()
    target_link_libraries(PlutoGELargeMeshOptimizationTests PRIVATE
        PlutoGE::Import PlutoGE::Render PlutoGE::Assets PlutoGE::Core
        PlutoGE::Scene PlutoGE::Scripting
    )
endif()

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

if (CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED)
    target_link_libraries(PlutoGEPostProcessInitializationTests PRIVATE
        "$<LINK_GROUP:RESCAN,PlutoGE::Render,PlutoGE::Assets,PlutoGE::Core,PlutoGE::Scene,PlutoGE::Import,PlutoGE::Scripting>"
    )
else()
    target_link_libraries(PlutoGEPostProcessInitializationTests PRIVATE
        PlutoGE::Render PlutoGE::Assets PlutoGE::Core PlutoGE::Scene
        PlutoGE::Import PlutoGE::Scripting
    )
endif()

add_test(NAME PlutoGEPostProcessInitializationTests COMMAND PlutoGEPostProcessInitializationTests)

add_executable(PlutoGERuntimeUITests
    RuntimeUITests.cpp
)

if (CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED)
    target_link_libraries(PlutoGERuntimeUITests PRIVATE
        "$<LINK_GROUP:RESCAN,PlutoGE::Scene,PlutoGE::Render,PlutoGE::Assets,PlutoGE::Core,PlutoGE::Import,PlutoGE::Scripting>"
    )
else()
    target_link_libraries(PlutoGERuntimeUITests PRIVATE
        PlutoGE::Scene PlutoGE::Render PlutoGE::Assets PlutoGE::Core
        PlutoGE::Import PlutoGE::Scripting
    )
endif()

add_test(NAME PlutoGERuntimeUITests COMMAND PlutoGERuntimeUITests)

add_executable(PlutoGESkeletonAttachmentTests
    SkeletonAttachmentTests.cpp
)

if (CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED)
    target_link_libraries(PlutoGESkeletonAttachmentTests PRIVATE
        "$<LINK_GROUP:RESCAN,PlutoGE::Scene,PlutoGE::Render,PlutoGE::Assets,PlutoGE::Core,PlutoGE::Import,PlutoGE::Scripting>"
    )
else()
    target_link_libraries(PlutoGESkeletonAttachmentTests PRIVATE
        PlutoGE::Scene PlutoGE::Render PlutoGE::Assets PlutoGE::Core
        PlutoGE::Import PlutoGE::Scripting
    )
endif()

add_test(NAME PlutoGESkeletonAttachmentTests COMMAND PlutoGESkeletonAttachmentTests)

add_executable(PlutoGEFoliageCollisionTests
    FoliageCollisionTests.cpp
)

if (CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED)
    target_link_libraries(PlutoGEFoliageCollisionTests PRIVATE
        "$<LINK_GROUP:RESCAN,PlutoGE::Scene,PlutoGE::Render,PlutoGE::Assets,PlutoGE::Core,PlutoGE::Import,PlutoGE::Scripting>"
    )
else()
    target_link_libraries(PlutoGEFoliageCollisionTests PRIVATE
        PlutoGE::Scene PlutoGE::Render PlutoGE::Assets PlutoGE::Core
        PlutoGE::Import PlutoGE::Scripting
    )
endif()

add_test(NAME PlutoGEFoliageCollisionTests COMMAND PlutoGEFoliageCollisionTests)

add_executable(PlutoGEPrefabMeshExporterTests
    PrefabMeshExporterTests.cpp
)

if (CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED)
    target_link_libraries(PlutoGEPrefabMeshExporterTests PRIVATE
        "$<LINK_GROUP:RESCAN,PlutoGE::Scene,PlutoGE::Render,PlutoGE::Assets,PlutoGE::Core,PlutoGE::Import,PlutoGE::Scripting>"
    )
else()
    target_link_libraries(PlutoGEPrefabMeshExporterTests PRIVATE
        PlutoGE::Scene PlutoGE::Render PlutoGE::Assets PlutoGE::Core
        PlutoGE::Import PlutoGE::Scripting
    )
endif()

add_test(NAME PlutoGEPrefabMeshExporterTests COMMAND PlutoGEPrefabMeshExporterTests)

if (TARGET OpenAL)
    add_custom_command(TARGET PlutoGEPostProcessInitializationTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:OpenAL>
            $<TARGET_FILE_DIR:PlutoGEPostProcessInitializationTests>/$<TARGET_FILE_NAME:OpenAL>
    )
endif()
