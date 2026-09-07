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
# MinGW needs to rescan the render/import cycle for the stb image implementation.
if (CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED)
    target_link_libraries(PlutoGEVulkanRhiTests PRIVATE
        "$<LINK_GROUP:RESCAN,PlutoGE::Render,PlutoGE::Assets,PlutoGE::Core,PlutoGE::Scene,PlutoGE::Import,PlutoGE::Scripting>")
else()
    target_link_libraries(PlutoGEVulkanRhiTests PRIVATE PlutoGE::Render)
endif()
target_compile_definitions(PlutoGEVulkanRhiTests PRIVATE
    PLUTO_RHI_TEST_SHADER_DIR="${CMAKE_BINARY_DIR}/engine/render/shaders")
add_test(NAME PlutoGEVulkanRhiTests COMMAND PlutoGEVulkanRhiTests)

add_executable(PlutoGEVulkanPresentationTests VulkanPresentationTests.cpp)
if (CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED)
    target_link_libraries(PlutoGEVulkanPresentationTests PRIVATE
        "$<LINK_GROUP:RESCAN,PlutoGE::Render,PlutoGE::Assets,PlutoGE::Core,PlutoGE::Scene,PlutoGE::Import,PlutoGE::Scripting>"
        PlutoGE::Platform)
else()
    target_link_libraries(PlutoGEVulkanPresentationTests PRIVATE PlutoGE::Render PlutoGE::Platform)
endif()
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

# Editor data tests run without initializing a window or graphics device.
add_executable(PlutoGEDebugDrawTests DebugDrawTests.cpp ../engine/render/src/DebugDraw.cpp)
target_include_directories(PlutoGEDebugDrawTests PRIVATE ../engine/render/include)
target_link_libraries(PlutoGEDebugDrawTests PRIVATE glm::glm)
add_test(NAME PlutoGEDebugDrawTests COMMAND PlutoGEDebugDrawTests)
find_program(PLUTOGE_TEST_DOTNET dotnet)
if (PLUTOGE_TEST_DOTNET)
    add_test(NAME PlutoGEDebugDrawManagedTests COMMAND ${PLUTOGE_TEST_DOTNET} run
        --project ${CMAKE_CURRENT_SOURCE_DIR}/managed/PlutoGE.DebugDrawSmokeTests/PlutoGE.DebugDrawSmokeTests.csproj
        --configuration Release)
endif()
add_executable(PlutoGEProjectValidationTests ProjectValidationTests.cpp
    ../engine/assets/src/ProjectValidation.cpp ../engine/assets/src/AssetReferences.cpp)
target_include_directories(PlutoGEProjectValidationTests PRIVATE ../engine/assets/include)
add_test(NAME PlutoGEProjectValidationTests COMMAND PlutoGEProjectValidationTests)
add_executable(PlutoGESceneRecoveryTests SceneRecoveryTests.cpp ../editor/ui/src/SceneRecovery.cpp)
target_include_directories(PlutoGESceneRecoveryTests PRIVATE ../editor/ui/include)
add_test(NAME PlutoGESceneRecoveryTests COMMAND PlutoGESceneRecoveryTests)
add_executable(PlutoGESceneHistoryTests SceneHistoryTests.cpp)
target_include_directories(PlutoGESceneHistoryTests PRIVATE ../editor/ui/include)
if (CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED)
    target_link_libraries(PlutoGESceneHistoryTests PRIVATE
        "$<LINK_GROUP:RESCAN,PlutoGE::Scene,PlutoGE::Render,PlutoGE::Assets,PlutoGE::Core,PlutoGE::Import,PlutoGE::Scripting>")
else()
    target_link_libraries(PlutoGESceneHistoryTests PRIVATE PlutoGE::Scene PlutoGE::Render PlutoGE::Assets PlutoGE::Core PlutoGE::Import PlutoGE::Scripting)
endif()
add_test(NAME PlutoGESceneHistoryTests COMMAND PlutoGESceneHistoryTests)
if (TARGET OpenAL)
    add_custom_command(TARGET PlutoGESceneHistoryTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:OpenAL> $<TARGET_FILE_DIR:PlutoGESceneHistoryTests>/$<TARGET_FILE_NAME:OpenAL>)
endif()

add_executable(PlutoGEAssetReferenceCookingTests AssetReferenceCookingTests.cpp)
if (CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED)
    target_link_libraries(PlutoGEAssetReferenceCookingTests PRIVATE
        "$<LINK_GROUP:RESCAN,PlutoGE::Scene,PlutoGE::Render,PlutoGE::Assets,PlutoGE::Core,PlutoGE::Import,PlutoGE::Scripting>")
else()
    target_link_libraries(PlutoGEAssetReferenceCookingTests PRIVATE
        PlutoGE::Scene PlutoGE::Render PlutoGE::Assets PlutoGE::Core PlutoGE::Import PlutoGE::Scripting)
endif()
add_test(NAME PlutoGEAssetReferenceCookingTests COMMAND PlutoGEAssetReferenceCookingTests)
if (TARGET OpenAL)
    add_custom_command(TARGET PlutoGEAssetReferenceCookingTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:OpenAL> $<TARGET_FILE_DIR:PlutoGEAssetReferenceCookingTests>/$<TARGET_FILE_NAME:OpenAL>)
endif()

add_executable(PlutoGEAssetReferencesTests
    AssetReferencesTests.cpp
    ../engine/assets/src/AssetReferences.cpp)
target_include_directories(PlutoGEAssetReferencesTests PRIVATE ../engine/assets/include)
add_test(NAME PlutoGEAssetReferencesTests COMMAND PlutoGEAssetReferencesTests)

add_executable(PlutoGEViewportBookmarksTests
    ViewportBookmarksTests.cpp
    ../editor/ui/src/ViewportBookmarks.cpp)
target_include_directories(PlutoGEViewportBookmarksTests PRIVATE ../editor/ui/include)
target_link_libraries(PlutoGEViewportBookmarksTests PRIVATE glm::glm)
add_test(NAME PlutoGEViewportBookmarksTests COMMAND PlutoGEViewportBookmarksTests)

add_executable(PlutoGEPlayModeChangesTests
    PlayModeChangesTests.cpp
    ../editor/ui/src/PlayModeChanges.cpp)
target_include_directories(PlutoGEPlayModeChangesTests PRIVATE ../editor/ui/include)
if (CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED)
    target_link_libraries(PlutoGEPlayModeChangesTests PRIVATE
        "$<LINK_GROUP:RESCAN,PlutoGE::Scene,PlutoGE::Render,PlutoGE::Assets,PlutoGE::Core,PlutoGE::Import,PlutoGE::Scripting>")
else()
    target_link_libraries(PlutoGEPlayModeChangesTests PRIVATE PlutoGE::Scene)
endif()
add_test(NAME PlutoGEPlayModeChangesTests COMMAND PlutoGEPlayModeChangesTests)
add_executable(PlutoGEGroundPlacementTests
    GroundPlacementTests.cpp
    ../editor/ui/src/GroundPlacement.cpp)
target_include_directories(PlutoGEGroundPlacementTests PRIVATE ../editor/ui/include)
if (CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED)
    target_link_libraries(PlutoGEGroundPlacementTests PRIVATE
        "$<LINK_GROUP:RESCAN,PlutoGE::Scene,PlutoGE::Render,PlutoGE::Assets,PlutoGE::Core,PlutoGE::Import,PlutoGE::Scripting>")
else()
    target_link_libraries(PlutoGEGroundPlacementTests PRIVATE PlutoGE::Scene)
endif()
add_test(NAME PlutoGEGroundPlacementTests COMMAND PlutoGEGroundPlacementTests)
if (TARGET OpenAL)
    add_custom_command(TARGET PlutoGEGroundPlacementTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:OpenAL> $<TARGET_FILE_DIR:PlutoGEGroundPlacementTests>/$<TARGET_FILE_NAME:OpenAL>)
    add_custom_command(TARGET PlutoGEPlayModeChangesTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:OpenAL> $<TARGET_FILE_DIR:PlutoGEPlayModeChangesTests>/$<TARGET_FILE_NAME:OpenAL>)
endif()

# Collider sizing is independent of the renderer/engine initialization.
add_executable(PlutoGEColliderCcdTests
    ColliderCcdTests.cpp
    ../engine/scene/src/components/ColliderComponent.cpp)
target_include_directories(PlutoGEColliderCcdTests PRIVATE ../engine/scene/include)
target_link_libraries(PlutoGEColliderCcdTests PRIVATE glm::glm)
add_test(NAME PlutoGEColliderCcdTests COMMAND PlutoGEColliderCcdTests)
