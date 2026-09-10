add_executable(PlutoGEContentPackTests ContentPackTests.cpp)
target_link_libraries(PlutoGEContentPackTests PRIVATE PlutoGE::Content)
add_test(NAME PlutoGEContentPackTests COMMAND PlutoGEContentPackTests)

add_executable(PlutoGERhiTypesTests RhiTypesTests.cpp)
add_executable(PlutoGEVirtualShadowClipmapTests VirtualShadowClipmapTests.cpp)
target_link_libraries(PlutoGEVirtualShadowClipmapTests PRIVATE PlutoGE::Render)
add_test(NAME PlutoGEVirtualShadowClipmapTests COMMAND PlutoGEVirtualShadowClipmapTests)
add_executable(PlutoGEVirtualShadowMapCacheTests VirtualShadowMapCacheTests.cpp)
target_include_directories(PlutoGEVirtualShadowMapCacheTests PRIVATE ${CMAKE_SOURCE_DIR}/engine/render/include)
target_compile_features(PlutoGEVirtualShadowMapCacheTests PRIVATE cxx_std_20)
add_test(NAME PlutoGEVirtualShadowMapCacheTests COMMAND PlutoGEVirtualShadowMapCacheTests)
add_executable(PlutoGENormalMipmapsTests NormalMipmapsTests.cpp)
target_include_directories(PlutoGENormalMipmapsTests PRIVATE ${CMAKE_SOURCE_DIR}/engine/render/src)
target_compile_features(PlutoGENormalMipmapsTests PRIVATE cxx_std_20)
add_test(NAME PlutoGENormalMipmapsTests COMMAND PlutoGENormalMipmapsTests)
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

if (TARGET OpenAL)
    add_custom_command(TARGET PlutoGEEngineGraphicsApiTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:OpenAL> $<TARGET_FILE_DIR:PlutoGEEngineGraphicsApiTests>/$<TARGET_FILE_NAME:OpenAL>)
endif()
add_test(NAME PlutoGEEngineGraphicsApiTests COMMAND PlutoGEEngineGraphicsApiTests)

add_executable(PlutoGEOpenGLRhiTests OpenGLRhiTests.cpp)
target_link_libraries(PlutoGEOpenGLRhiTests PRIVATE PlutoGE::Render PlutoGE::Platform)
target_compile_definitions(PlutoGEOpenGLRhiTests PRIVATE
    PLUTO_RHI_TEST_SHADER_DIR="${CMAKE_BINARY_DIR}/engine/render/shaders")
add_test(NAME PlutoGEOpenGLRhiTests COMMAND PlutoGEOpenGLRhiTests)
add_test(NAME PlutoGEOpenGLSsrTests COMMAND PlutoGEOpenGLRhiTests --ssr-only)
add_test(NAME PlutoGEOpenGLVsmOnlyTests COMMAND PlutoGEOpenGLRhiTests --vsm-only)

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
if (PLUTO_ENABLE_FSR2)
    add_test(NAME PlutoGEVulkanFsr2Tests COMMAND PlutoGEVulkanRhiTests --fsr2-only)
endif()
add_test(NAME PlutoGEVulkanSsrTests COMMAND PlutoGEVulkanRhiTests --ssr-only)
add_test(NAME PlutoGEVulkanVsmOnlyTests COMMAND PlutoGEVulkanRhiTests --vsm-only)
add_test(NAME PlutoGEVulkanVsmPerformanceTests COMMAND PlutoGEVulkanRhiTests --vsm-performance)
add_test(NAME PlutoGEVctWorldCacheRenderingTests COMMAND PlutoGEVulkanRhiTests --vct-world-cache)

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
add_test(NAME PlutoGEVctEmissionCoverageTests COMMAND PlutoGEPostProcessInitializationTests --vct-coverage)

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

add_executable(PlutoGEMultiEntityEditTests
    MultiEntityEditTests.cpp
    ../editor/ui/src/MultiEntityEdit.cpp)
target_include_directories(PlutoGEMultiEntityEditTests PRIVATE ../editor/ui/include)
if (CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED)
    target_link_libraries(PlutoGEMultiEntityEditTests PRIVATE
        "$<LINK_GROUP:RESCAN,PlutoGE::Scene,PlutoGE::Render,PlutoGE::Assets,PlutoGE::Core,PlutoGE::Import,PlutoGE::Scripting>")
else()
    target_link_libraries(PlutoGEMultiEntityEditTests PRIVATE PlutoGE::Scene)
endif()
add_test(NAME PlutoGEMultiEntityEditTests COMMAND PlutoGEMultiEntityEditTests)

if (TARGET OpenAL)
    add_custom_command(TARGET PlutoGEMultiEntityEditTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:OpenAL> $<TARGET_FILE_DIR:PlutoGEMultiEntityEditTests>/$<TARGET_FILE_NAME:OpenAL>)
endif()

add_executable(PlutoGECameraRigTests
    CameraRigTests.cpp
)
target_include_directories(PlutoGECameraRigTests PRIVATE ../editor/ui/include)
if (CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED)
    target_link_libraries(PlutoGECameraRigTests PRIVATE
        "$<LINK_GROUP:RESCAN,PlutoGE::Scene,PlutoGE::Render,PlutoGE::Assets,PlutoGE::Core,PlutoGE::Import,PlutoGE::Scripting>")
else()
    target_link_libraries(PlutoGECameraRigTests PRIVATE PlutoGE::Scene)
endif()
add_test(NAME PlutoGECameraRigTests COMMAND PlutoGECameraRigTests)

if (TARGET OpenAL)
    add_custom_command(TARGET PlutoGECameraRigTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:OpenAL> $<TARGET_FILE_DIR:PlutoGECameraRigTests>/$<TARGET_FILE_NAME:OpenAL>)
endif()
if (PLUTOGE_TEST_DOTNET)
    add_test(NAME PlutoGECameraRigManagedTests COMMAND ${PLUTOGE_TEST_DOTNET} run
        --project ${CMAKE_CURRENT_SOURCE_DIR}/managed/PlutoGE.CameraRigSmokeTests/PlutoGE.CameraRigSmokeTests.csproj --configuration Release)
endif()

add_executable(PlutoGESurfaceResponseTests
    SurfaceResponseTests.cpp
)
target_include_directories(PlutoGESurfaceResponseTests PRIVATE ../editor/ui/include)
if (CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED)
    target_link_libraries(PlutoGESurfaceResponseTests PRIVATE
        "$<LINK_GROUP:RESCAN,PlutoGE::Scene,PlutoGE::Render,PlutoGE::Assets,PlutoGE::Core,PlutoGE::Import,PlutoGE::Scripting>")
else()
    target_link_libraries(PlutoGESurfaceResponseTests PRIVATE PlutoGE::Scene)
endif()
add_test(NAME PlutoGESurfaceResponseTests COMMAND PlutoGESurfaceResponseTests)

if (TARGET OpenAL)
    add_custom_command(TARGET PlutoGESurfaceResponseTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:OpenAL> $<TARGET_FILE_DIR:PlutoGESurfaceResponseTests>/$<TARGET_FILE_NAME:OpenAL>)
endif()
if (PLUTOGE_TEST_DOTNET)
    add_test(NAME PlutoGESurfaceResponseManagedTests COMMAND ${PLUTOGE_TEST_DOTNET} run
        --project ${CMAKE_CURRENT_SOURCE_DIR}/managed/PlutoGE.SurfaceResponseSmokeTests/PlutoGE.SurfaceResponseSmokeTests.csproj --configuration Release)
endif()

add_executable(PlutoGEPrefabVariantTests
    PrefabVariantTests.cpp
)
target_include_directories(PlutoGEPrefabVariantTests PRIVATE ../editor/ui/include)
if (CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED)
    target_link_libraries(PlutoGEPrefabVariantTests PRIVATE
        "$<LINK_GROUP:RESCAN,PlutoGE::Scene,PlutoGE::Render,PlutoGE::Assets,PlutoGE::Core,PlutoGE::Import,PlutoGE::Scripting>")
else()
    target_link_libraries(PlutoGEPrefabVariantTests PRIVATE PlutoGE::Scene)
endif()
add_test(NAME PlutoGEPrefabVariantTests COMMAND PlutoGEPrefabVariantTests)

if (TARGET OpenAL)
    add_custom_command(TARGET PlutoGEPrefabVariantTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:OpenAL> $<TARGET_FILE_DIR:PlutoGEPrefabVariantTests>/$<TARGET_FILE_NAME:OpenAL>)
endif()

add_test(NAME PlutoGEVirtualShadowPerformanceTests COMMAND PlutoGEVulkanRhiTests --vsm-performance)

if (TARGET PlutoGEEditorUI)
    add_executable(PlutoGEEditorProfilerTests EditorProfilerTests.cpp)
    target_link_libraries(PlutoGEEditorProfilerTests PRIVATE PlutoGE::EditorUI)
    if (CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED)
        target_link_libraries(PlutoGEEditorProfilerTests PRIVATE
            "$<LINK_GROUP:RESCAN,PlutoGE::Render,PlutoGE::Assets,PlutoGE::Core,PlutoGE::Scene,PlutoGE::Import,PlutoGE::Scripting>")
    endif()
    if (TARGET OpenAL)
        add_custom_command(TARGET PlutoGEEditorProfilerTests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:OpenAL> $<TARGET_FILE_DIR:PlutoGEEditorProfilerTests>/$<TARGET_FILE_NAME:OpenAL>)
    endif()
    add_test(NAME PlutoGEEditorProfilerTests COMMAND PlutoGEEditorProfilerTests)
endif()

add_executable(PlutoGECpuTraceTests CpuTraceTests.cpp)
target_include_directories(PlutoGECpuTraceTests PRIVATE ../engine/core/include)
target_compile_features(PlutoGECpuTraceTests PRIVATE cxx_std_20)
add_test(NAME PlutoGECpuTraceTests COMMAND PlutoGECpuTraceTests)

if (TARGET PlutoGEEditorUI)
    add_executable(PlutoGEEditorProfilerPanelTests EditorProfilerPanelTests.cpp)
    target_link_libraries(PlutoGEEditorProfilerPanelTests PRIVATE PlutoGE::EditorUI)
    if (CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED)
        target_link_libraries(PlutoGEEditorProfilerPanelTests PRIVATE
            "$<LINK_GROUP:RESCAN,PlutoGE::Render,PlutoGE::Assets,PlutoGE::Core,PlutoGE::Scene,PlutoGE::Import,PlutoGE::Scripting>")
    endif()
    if (TARGET OpenAL)
        add_custom_command(TARGET PlutoGEEditorProfilerPanelTests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:OpenAL> $<TARGET_FILE_DIR:PlutoGEEditorProfilerPanelTests>/$<TARGET_FILE_NAME:OpenAL>)
    endif()
    add_test(NAME PlutoGEEditorProfilerPanelTests COMMAND PlutoGEEditorProfilerPanelTests)
    set_tests_properties(PlutoGEEditorProfilerPanelTests PROPERTIES SKIP_RETURN_CODE 77)
endif()

add_test(NAME PlutoGEVulkanParticlePointTests COMMAND PlutoGEVulkanRhiTests --particles-points-only)
add_test(NAME PlutoGEOpenGLParticlePointTests COMMAND PlutoGEOpenGLRhiTests --particles-points-only)

add_test(NAME PlutoGEVulkanTemporalMotionTests COMMAND PlutoGEVulkanRhiTests --temporal-motion)
add_test(NAME PlutoGEOpenGLTemporalMotionTests COMMAND PlutoGEOpenGLRhiTests --temporal-motion)
