# Links the whole engine. MinGW needs the mutually dependent static libraries
# rescanned; other toolchains resolve them through the transitive link graph.
function(pluto_link_engine target)
    if (CMAKE_LINK_GROUP_USING_RESCAN_SUPPORTED)
        target_link_libraries(${target} PRIVATE
            "$<LINK_GROUP:RESCAN,PlutoGE::Scene,PlutoGE::Render,PlutoGE::Assets,PlutoGE::Core,PlutoGE::Import,PlutoGE::Scripting>")
    else()
        target_link_libraries(${target} PRIVATE PlutoGE::Scene)
    endif()
endfunction()

if (MSVC)
    # Test executables are never distributed. FASTLINK PDBs reference the
    # object files instead of copying their debug info, which keeps relinking
    # every test after an engine change cheap.
    add_link_options("$<$<CONFIG:Debug,RelWithDebInfo>:/DEBUG:FASTLINK>")
endif()

add_executable(PlutoGEContentPackTests ContentPackTests.cpp)
add_executable(PlutoGEProjectBenchmarkTests ProjectBenchmarkTests.cpp)
target_include_directories(PlutoGEProjectBenchmarkTests PRIVATE ${CMAKE_SOURCE_DIR}/runtime/src)
target_compile_features(PlutoGEProjectBenchmarkTests PRIVATE cxx_std_20)
add_test(NAME PlutoGEProjectBenchmarkTests COMMAND PlutoGEProjectBenchmarkTests)
add_executable(PlutoGEInputAxisHistoryTests InputAxisHistoryTests.cpp)
target_include_directories(PlutoGEInputAxisHistoryTests PRIVATE ${CMAKE_SOURCE_DIR}/engine/platform/include)
target_compile_features(PlutoGEInputAxisHistoryTests PRIVATE cxx_std_20)
add_test(NAME PlutoGEInputAxisHistoryTests COMMAND PlutoGEInputAxisHistoryTests)
target_link_libraries(PlutoGEContentPackTests PRIVATE PlutoGE::Content)
add_test(NAME PlutoGEContentPackTests COMMAND PlutoGEContentPackTests)

add_executable(PlutoGEGraphicsQualityTests GraphicsQualityTests.cpp)
target_include_directories(PlutoGEGraphicsQualityTests PRIVATE ${CMAKE_SOURCE_DIR}/engine/render/include)
target_link_libraries(PlutoGEGraphicsQualityTests PRIVATE glm::glm)
target_compile_features(PlutoGEGraphicsQualityTests PRIVATE cxx_std_20)
add_test(NAME PlutoGEGraphicsQualityTests COMMAND PlutoGEGraphicsQualityTests)

add_executable(PlutoGERhiTypesTests RhiTypesTests.cpp)
add_executable(PlutoGESkinningCpuTests SkinningCpuTests.cpp)
target_include_directories(PlutoGESkinningCpuTests PRIVATE ${CMAKE_SOURCE_DIR}/engine/render/src)
target_link_libraries(PlutoGESkinningCpuTests PRIVATE PlutoGE::Render)
add_test(NAME PlutoGESkinningCpuTests COMMAND PlutoGESkinningCpuTests)
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
pluto_link_engine(PlutoGERhiPostProcessAdapterTests)
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
add_test(NAME PlutoGEWindowResizeTests COMMAND PlutoGEWindowClientApiTests --resize-only)

add_executable(PlutoGEEngineGraphicsApiTests EngineGraphicsApiTests.cpp)
target_link_libraries(PlutoGEEngineGraphicsApiTests PRIVATE PlutoGE::Core)

add_test(NAME PlutoGEEngineGraphicsApiTests COMMAND PlutoGEEngineGraphicsApiTests)

add_executable(PlutoGEOpenGLRhiTests OpenGLRhiTests.cpp)
target_link_libraries(PlutoGEOpenGLRhiTests PRIVATE PlutoGE::Render PlutoGE::Platform PlutoGE::Scene)
target_compile_definitions(PlutoGEOpenGLRhiTests PRIVATE
    PLUTO_RHI_TEST_SHADER_DIR="${CMAKE_BINARY_DIR}/engine/render/shaders")
add_test(NAME PlutoGEOpenGLRhiTests COMMAND PlutoGEOpenGLRhiTests)
add_test(NAME PlutoGEOpenGLOcclusionTests COMMAND PlutoGEOpenGLRhiTests --occlusion)
add_test(NAME PlutoGEOpenGLGeometryDiagnosticTests COMMAND PlutoGEOpenGLRhiTests --geometry-diagnostics)
add_test(NAME PlutoGEMeshReplacementRenderingTests COMMAND PlutoGEOpenGLRhiTests --mesh-replacement)
add_test(NAME PlutoGEOpenGLRenderOptimizationTests COMMAND PlutoGEOpenGLRhiTests --render-optimizations)
add_test(NAME PlutoGEOpenGLSsrTests COMMAND PlutoGEOpenGLRhiTests --ssr-only)
add_test(NAME PlutoGEOpenGLVsmOnlyTests COMMAND PlutoGEOpenGLRhiTests --vsm-only)

add_executable(PlutoGEVulkanBootstrapTests VulkanBootstrapTests.cpp)
target_link_libraries(PlutoGEVulkanBootstrapTests PRIVATE PlutoGE::Render)
add_test(NAME PlutoGEVulkanBootstrapTests COMMAND PlutoGEVulkanBootstrapTests)

add_executable(PlutoGEVulkanRhiTests VulkanRhiTests.cpp)
pluto_attach_slang_runtime(PlutoGEVulkanRhiTests)
# MinGW needs to rescan the render/import cycle for the stb image implementation.
pluto_link_engine(PlutoGEVulkanRhiTests)
target_compile_definitions(PlutoGEVulkanRhiTests PRIVATE
    PLUTO_RHI_TEST_SHADER_DIR="${CMAKE_BINARY_DIR}/engine/render/shaders")
add_test(NAME PlutoGEVulkanRhiTests COMMAND PlutoGEVulkanRhiTests)
add_test(NAME PlutoGEOpenGLTransparencyTests COMMAND PlutoGEOpenGLRhiTests --transparency-only)
add_test(NAME PlutoGEVulkanTransparencyTests COMMAND PlutoGEVulkanRhiTests --transparency-only)
add_test(NAME PlutoGEVulkanOcclusionTests COMMAND PlutoGEVulkanRhiTests --occlusion)
add_test(NAME PlutoGEVulkanGeometryDiagnosticTests COMMAND PlutoGEVulkanRhiTests --geometry-diagnostics)
add_test(NAME PlutoGEOpenGLFogTests COMMAND PlutoGEOpenGLRhiTests --fog-only)
add_test(NAME PlutoGEVulkanFogTests COMMAND PlutoGEVulkanRhiTests --fog-only)
add_test(NAME PlutoGEVulkanSkyQuadratureTests COMMAND PlutoGEVulkanRhiTests --sky-quadrature)
add_test(NAME PlutoGEOpenGLSkyQuadratureTests COMMAND PlutoGEOpenGLRhiTests --sky-quadrature)
add_test(NAME PlutoGEVulkanRenderOptimizationTests COMMAND PlutoGEVulkanRhiTests --render-optimizations)
add_test(NAME PlutoGEVulkanSkinningTests COMMAND PlutoGEVulkanRhiTests --skinning)
if (PLUTO_ENABLE_FSR2)
    add_test(NAME PlutoGEOrthographicUpscalerTests COMMAND PlutoGEVulkanRhiTests --orthographic-upscalers)
    add_test(NAME PlutoGEVulkanFsr2Tests COMMAND PlutoGEVulkanRhiTests --fsr2-only)
endif()
add_test(NAME PlutoGEVulkanSsrTests COMMAND PlutoGEVulkanRhiTests --ssr-only)
add_test(NAME PlutoGEVulkanVsmOnlyTests COMMAND PlutoGEVulkanRhiTests --vsm-only)
add_test(NAME PlutoGEVulkanVsmMembershipTests COMMAND PlutoGEVulkanRhiTests --vsm-membership)
add_test(NAME PlutoGEOpenGLVsmMembershipTests COMMAND PlutoGEOpenGLRhiTests --vsm-membership)
add_test(NAME PlutoGEVulkanVctPublicationTests COMMAND PlutoGEVulkanRhiTests --vct-publication)
add_test(NAME PlutoGEOpenGLVctPublicationTests COMMAND PlutoGEOpenGLRhiTests --vct-publication)
add_test(NAME PlutoGEVulkanVsmPerformanceTests COMMAND PlutoGEVulkanRhiTests --vsm-performance)
add_test(NAME PlutoGEVctWorldCacheRenderingTests COMMAND PlutoGEVulkanRhiTests --vct-world-cache)
add_test(NAME PlutoGEVctWorldCacheOpenGLTests COMMAND PlutoGEOpenGLRhiTests --vct-world-cache)
add_test(NAME PlutoGEVctRemovedCharacterTests COMMAND PlutoGEVulkanRhiTests --vct-removed-character)
add_test(NAME PlutoGEVctSecondaryBounceOpenGLTests COMMAND PlutoGEOpenGLRhiTests --vct-secondary)

add_executable(PlutoGEVulkanPresentationTests VulkanPresentationTests.cpp)
pluto_link_engine(PlutoGEVulkanPresentationTests)
target_link_libraries(PlutoGEVulkanPresentationTests PRIVATE PlutoGE::Platform)
target_compile_definitions(PlutoGEVulkanPresentationTests PRIVATE
    PLUTO_RHI_TEST_SHADER_DIR="${CMAKE_BINARY_DIR}/engine/render/shaders")
add_test(NAME PlutoGEVulkanPresentationTests COMMAND PlutoGEVulkanPresentationTests)

add_executable(PlutoGELargeMeshOptimizationTests
    LargeMeshOptimizationTests.cpp
)

pluto_link_engine(PlutoGELargeMeshOptimizationTests)

add_test(NAME PlutoGELargeMeshOptimizationTests COMMAND PlutoGELargeMeshOptimizationTests)
add_test(NAME PlutoGEMeshLodGenerationTests COMMAND PlutoGELargeMeshOptimizationTests --lod-only)

# Copy at configure time so nested LINK_GROUP expressions are evaluated normally.
get_target_property(pluto_import_test_libraries PlutoGELargeMeshOptimizationTests LINK_LIBRARIES)

add_executable(PlutoGEInspectMeshImport EXCLUDE_FROM_ALL ../tools/InspectMeshImport.cpp)
add_executable(PlutoGEMaterialReloadTests MaterialReloadTests.cpp)
target_link_libraries(PlutoGEMaterialReloadTests PRIVATE ${pluto_import_test_libraries})
add_test(NAME PlutoGEMaterialReloadTests COMMAND PlutoGEMaterialReloadTests)
target_link_libraries(PlutoGEInspectMeshImport PRIVATE ${pluto_import_test_libraries})
add_executable(PlutoGERecookMesh EXCLUDE_FROM_ALL ../tools/RecookMesh.cpp)
target_link_libraries(PlutoGERecookMesh PRIVATE ${pluto_import_test_libraries})
add_executable(PlutoGEDdsImageTests DdsImageTests.cpp)
target_include_directories(PlutoGEDdsImageTests PRIVATE ../engine/render/include)
target_compile_features(PlutoGEDdsImageTests PRIVATE cxx_std_20)
add_test(NAME PlutoGEDdsImageTests COMMAND PlutoGEDdsImageTests)
add_executable(PlutoGEFbxImportTests FbxImportTests.cpp)
target_link_libraries(PlutoGEFbxImportTests PRIVATE ${pluto_import_test_libraries})
add_test(NAME PlutoGEFbxImportTests COMMAND PlutoGEFbxImportTests)
set_tests_properties(PlutoGEFbxImportTests PROPERTIES ENVIRONMENT "PLUTOGE_DISABLE_MESH_DISK_CACHE=1")

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

pluto_link_engine(PlutoGEPostProcessInitializationTests)

add_test(NAME PlutoGEPostProcessInitializationTests COMMAND PlutoGEPostProcessInitializationTests)
add_test(NAME PlutoGEVctEmissionCoverageTests COMMAND PlutoGEPostProcessInitializationTests --vct-coverage)

add_executable(PlutoGERuntimeUITests
    RuntimeUITests.cpp
)

pluto_link_engine(PlutoGERuntimeUITests)

add_test(NAME PlutoGERuntimeUITests COMMAND PlutoGERuntimeUITests)

add_executable(PlutoGESkeletonAttachmentTests
    SkeletonAttachmentTests.cpp
)

pluto_link_engine(PlutoGESkeletonAttachmentTests)

add_test(NAME PlutoGESkeletonAttachmentTests COMMAND PlutoGESkeletonAttachmentTests)

add_executable(PlutoGEFoliageCollisionTests
    FoliageCollisionTests.cpp
)

pluto_link_engine(PlutoGEFoliageCollisionTests)

add_test(NAME PlutoGEFoliageCollisionTests COMMAND PlutoGEFoliageCollisionTests)

add_executable(PlutoGEPrefabMeshExporterTests
    PrefabMeshExporterTests.cpp
)

pluto_link_engine(PlutoGEPrefabMeshExporterTests)

add_test(NAME PlutoGEPrefabMeshExporterTests COMMAND PlutoGEPrefabMeshExporterTests)

get_target_property(pluto_scene_test_libraries PlutoGEPrefabMeshExporterTests LINK_LIBRARIES)

add_executable(PlutoGEImportedAssetLifetimeTests ImportedAssetLifetimeTests.cpp)
target_link_libraries(PlutoGEImportedAssetLifetimeTests PRIVATE ${pluto_scene_test_libraries})
add_test(NAME PlutoGEImportedAssetLifetimeTests COMMAND PlutoGEImportedAssetLifetimeTests)

add_executable(PlutoGEScenePortraitPoseTests ScenePortraitPoseTests.cpp)
target_link_libraries(PlutoGEScenePortraitPoseTests PRIVATE ${pluto_scene_test_libraries})
add_test(NAME PlutoGEScenePortraitPoseTests COMMAND PlutoGEScenePortraitPoseTests)

add_executable(PlutoGECameraStackTests CameraStackTests.cpp)
target_link_libraries(PlutoGECameraStackTests PRIVATE ${pluto_scene_test_libraries})
add_test(NAME PlutoGECameraStackTests COMMAND PlutoGECameraStackTests)

# Editor data tests run without initializing a window or graphics device.
add_executable(PlutoGEDebugDrawTests DebugDrawTests.cpp ../engine/render/src/DebugDraw.cpp)
target_include_directories(PlutoGEDebugDrawTests PRIVATE ../engine/render/include)
target_link_libraries(PlutoGEDebugDrawTests PRIVATE glm::glm)
add_test(NAME PlutoGEDebugDrawTests COMMAND PlutoGEDebugDrawTests)
find_program(PLUTOGE_TEST_DOTNET dotnet)
if (PLUTOGE_TEST_DOTNET)
    add_test(NAME PlutoGEGraphicsQualitySmokeTests COMMAND ${PLUTOGE_TEST_DOTNET} run
        --project ${CMAKE_CURRENT_SOURCE_DIR}/managed/PlutoGE.GraphicsQualitySmokeTests/PlutoGE.GraphicsQualitySmokeTests.csproj --configuration Release)
    add_test(NAME PlutoGEInputActionMapSmokeTests COMMAND ${PLUTOGE_TEST_DOTNET} run
        --project ${CMAKE_CURRENT_SOURCE_DIR}/managed/PlutoGE.InputActionMapSmokeTests/PlutoGE.InputActionMapSmokeTests.csproj --configuration Release)
endif()
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
pluto_link_engine(PlutoGESceneHistoryTests)
add_test(NAME PlutoGESceneHistoryTests COMMAND PlutoGESceneHistoryTests)

add_executable(PlutoGEAssetReferenceCookingTests AssetReferenceCookingTests.cpp)
pluto_link_engine(PlutoGEAssetReferenceCookingTests)
add_test(NAME PlutoGEAssetReferenceCookingTests COMMAND PlutoGEAssetReferenceCookingTests)

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

add_executable(PlutoGEViewportPickingTests ViewportPickingTests.cpp)
target_include_directories(PlutoGEViewportPickingTests PRIVATE ../editor/ui/include)
target_link_libraries(PlutoGEViewportPickingTests PRIVATE glm::glm)
add_test(NAME PlutoGEViewportPickingTests COMMAND PlutoGEViewportPickingTests)

add_executable(PlutoGEPlayModeChangesTests
    PlayModeChangesTests.cpp
    ../editor/ui/src/PlayModeChanges.cpp)
target_include_directories(PlutoGEPlayModeChangesTests PRIVATE ../editor/ui/include)
pluto_link_engine(PlutoGEPlayModeChangesTests)
add_test(NAME PlutoGEPlayModeChangesTests COMMAND PlutoGEPlayModeChangesTests)
add_executable(PlutoGEGroundPlacementTests
    GroundPlacementTests.cpp
    ../editor/ui/src/GroundPlacement.cpp)
target_include_directories(PlutoGEGroundPlacementTests PRIVATE ../editor/ui/include)
pluto_link_engine(PlutoGEGroundPlacementTests)
add_test(NAME PlutoGEGroundPlacementTests COMMAND PlutoGEGroundPlacementTests)

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
pluto_link_engine(PlutoGEMultiEntityEditTests)
add_test(NAME PlutoGEMultiEntityEditTests COMMAND PlutoGEMultiEntityEditTests)

add_executable(PlutoGECameraRigTests
    CameraRigTests.cpp
)
target_include_directories(PlutoGECameraRigTests PRIVATE ../editor/ui/include)
pluto_link_engine(PlutoGECameraRigTests)
add_test(NAME PlutoGECameraRigTests COMMAND PlutoGECameraRigTests)

if (PLUTOGE_TEST_DOTNET)
    add_test(NAME PlutoGECameraRigManagedTests COMMAND ${PLUTOGE_TEST_DOTNET} run
        --project ${CMAKE_CURRENT_SOURCE_DIR}/managed/PlutoGE.CameraRigSmokeTests/PlutoGE.CameraRigSmokeTests.csproj --configuration Release)
endif()

add_executable(PlutoGESurfaceResponseTests
    SurfaceResponseTests.cpp
)
target_include_directories(PlutoGESurfaceResponseTests PRIVATE ../editor/ui/include)
pluto_link_engine(PlutoGESurfaceResponseTests)
add_test(NAME PlutoGESurfaceResponseTests COMMAND PlutoGESurfaceResponseTests)

if (PLUTOGE_TEST_DOTNET)
    add_test(NAME PlutoGESurfaceResponseManagedTests COMMAND ${PLUTOGE_TEST_DOTNET} run
        --project ${CMAKE_CURRENT_SOURCE_DIR}/managed/PlutoGE.SurfaceResponseSmokeTests/PlutoGE.SurfaceResponseSmokeTests.csproj --configuration Release)
endif()

add_executable(PlutoGEPrefabVariantTests
    PrefabVariantTests.cpp
)
target_include_directories(PlutoGEPrefabVariantTests PRIVATE ../editor/ui/include)
pluto_link_engine(PlutoGEPrefabVariantTests)
add_test(NAME PlutoGEPrefabVariantTests COMMAND PlutoGEPrefabVariantTests)

add_test(NAME PlutoGEVirtualShadowPerformanceTests COMMAND PlutoGEVulkanRhiTests --vsm-performance)

if (TARGET PlutoGEEditorUI)
    add_executable(PlutoGEEditorProfilerTests EditorProfilerTests.cpp)
    target_link_libraries(PlutoGEEditorProfilerTests PRIVATE PlutoGE::EditorUI)
    pluto_link_engine(PlutoGEEditorProfilerTests)
    add_test(NAME PlutoGEEditorProfilerTests COMMAND PlutoGEEditorProfilerTests)
endif()

add_executable(PlutoGECpuTraceTests CpuTraceTests.cpp)
target_include_directories(PlutoGECpuTraceTests PRIVATE ../engine/core/include)
target_compile_features(PlutoGECpuTraceTests PRIVATE cxx_std_20)
add_test(NAME PlutoGECpuTraceTests COMMAND PlutoGECpuTraceTests)

if (TARGET PlutoGEEditorUI)
    add_executable(PlutoGEEditorProfilerPanelTests EditorProfilerPanelTests.cpp)
    target_link_libraries(PlutoGEEditorProfilerPanelTests PRIVATE PlutoGE::EditorUI)
    pluto_link_engine(PlutoGEEditorProfilerPanelTests)
    add_test(NAME PlutoGEEditorProfilerPanelTests COMMAND PlutoGEEditorProfilerPanelTests)
    set_tests_properties(PlutoGEEditorProfilerPanelTests PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(PlutoGERuntimeProfilerTests RuntimeProfilerTests.cpp ../runtime/src/RuntimeProfiler.cpp)
    target_include_directories(PlutoGERuntimeProfilerTests PRIVATE ../runtime/src)
    target_link_libraries(PlutoGERuntimeProfilerTests PRIVATE PlutoGE::EditorUI)
    pluto_link_engine(PlutoGERuntimeProfilerTests)
    add_test(NAME PlutoGERuntimeProfilerTests COMMAND PlutoGERuntimeProfilerTests)
    set_tests_properties(PlutoGERuntimeProfilerTests PROPERTIES SKIP_RETURN_CODE 77)
endif()

add_test(NAME PlutoGEVulkanParticlePointTests COMMAND PlutoGEVulkanRhiTests --particles-points-only)
add_test(NAME PlutoGEOpenGLParticlePointTests COMMAND PlutoGEOpenGLRhiTests --particles-points-only)
add_test(NAME PlutoGEVulkanDecalTests COMMAND PlutoGEVulkanRhiTests --decals-only)
add_test(NAME PlutoGEOpenGLDecalTests COMMAND PlutoGEOpenGLRhiTests --decals-only)

add_test(NAME PlutoGEVulkanTemporalMotionTests COMMAND PlutoGEVulkanRhiTests --temporal-motion)
add_test(NAME PlutoGEOpenGLTemporalMotionTests COMMAND PlutoGEOpenGLRhiTests --temporal-motion)

add_test(NAME PlutoGEVulkanPreparationCacheTests COMMAND PlutoGEVulkanRhiTests --preparation-cache)
add_test(NAME PlutoGEVulkanCameraStackTests COMMAND PlutoGEVulkanRhiTests --camera-stack)
add_test(NAME PlutoGEOpenGLCameraStackTests COMMAND PlutoGEOpenGLRhiTests --camera-stack)
add_test(NAME PlutoGEVulkanRenderTextureTests COMMAND PlutoGEVulkanRhiTests --render-texture)
add_test(NAME PlutoGEVulkanOpaqueBatchingTests COMMAND PlutoGEVulkanRhiTests --opaque-batching)
add_test(NAME PlutoGEOpenGLOpaqueBatchingTests COMMAND PlutoGEOpenGLRhiTests --opaque-batching)

add_executable(PlutoGERmlUiAntialiasingTests RmlUiAntialiasingTests.cpp)
add_executable(PlutoGERmlUiFontRasterTests RmlUiFontRasterTests.cpp)
target_link_libraries(PlutoGERmlUiFontRasterTests PRIVATE RmlUi::Core)
target_include_directories(PlutoGERmlUiFontRasterTests PRIVATE "${CMAKE_SOURCE_DIR}/third_party/rmlui")
target_compile_definitions(PlutoGERmlUiFontRasterTests PRIVATE PLUTO_FONT_TEST_FILE="${CMAKE_SOURCE_DIR}/editor/resources/fonts/MartianMono-StdRg.ttf")
add_test(NAME PlutoGERmlUiFontRasterTests COMMAND PlutoGERmlUiFontRasterTests)
pluto_link_engine(PlutoGERmlUiAntialiasingTests)
target_link_libraries(PlutoGERmlUiAntialiasingTests PRIVATE PlutoGE::Platform)
target_compile_definitions(PlutoGERmlUiAntialiasingTests PRIVATE PLUTO_RHI_TEST_SHADER_DIR="${CMAKE_BINARY_DIR}/engine/render/shaders")
add_test(NAME PlutoGEVulkanUiAntialiasing COMMAND PlutoGERmlUiAntialiasingTests)
add_test(NAME PlutoGEOpenGLUiAntialiasing COMMAND PlutoGERmlUiAntialiasingTests --opengl)
add_test(NAME PlutoGEVulkanUiRenderTexture COMMAND PlutoGERmlUiAntialiasingTests --render-texture)

if(TARGET PlutoGEEditorUI)
    add_executable(PlutoGEEditorLoadingScreenTests EditorLoadingScreenTests.cpp)
    target_link_libraries(PlutoGEEditorLoadingScreenTests PRIVATE PlutoGE::EditorUI)
    pluto_link_engine(PlutoGEEditorLoadingScreenTests)
    add_test(NAME PlutoGEEditorLoadingScreenTests COMMAND PlutoGEEditorLoadingScreenTests)
    add_executable(PlutoGEAssetReferencePickerTests AssetReferencePickerTests.cpp)
    target_link_libraries(PlutoGEAssetReferencePickerTests PRIVATE PlutoGE::EditorUI)
    pluto_link_engine(PlutoGEAssetReferencePickerTests)
    add_test(NAME PlutoGEAssetReferencePickerTests COMMAND PlutoGEAssetReferencePickerTests)
    add_executable(PlutoGEEditorVulkanTextureTests EditorVulkanTextureTests.cpp)
    target_link_libraries(PlutoGEEditorVulkanTextureTests PRIVATE PlutoGE::EditorUI)
    add_test(NAME PlutoGEEditorVulkanTextureTests COMMAND PlutoGEEditorVulkanTextureTests)
endif()

target_compile_definitions(PlutoGERmlUiAntialiasingTests PRIVATE PLUTO_UI_TEST_FONT="${CMAKE_SOURCE_DIR}/editor/resources/fonts/MartianMono-StdRg.ttf")
add_executable(PlutoGETimelineTests TimelineTests.cpp ../engine/scene/src/Timeline.cpp)
target_include_directories(PlutoGETimelineTests PRIVATE ../engine/scene/include)
target_compile_features(PlutoGETimelineTests PRIVATE cxx_std_20)
add_test(NAME PlutoGETimelineTests COMMAND PlutoGETimelineTests)

add_executable(PlutoGESequencerTests SequencerTests.cpp)
add_executable(PlutoGESceneStreamingTests SceneStreamingTests.cpp)
add_executable(PlutoGESceneLoadingTests SceneLoadingTests.cpp)
target_link_libraries(PlutoGESceneLoadingTests PRIVATE PlutoGE::Scene)
pluto_link_engine(PlutoGESceneLoadingTests)
add_test(NAME PlutoGESceneLoadingTests COMMAND PlutoGESceneLoadingTests)
add_executable(PlutoGESplineRoadTests SplineRoadTests.cpp)
target_link_libraries(PlutoGESplineRoadTests PRIVATE PlutoGE::Scene)
pluto_link_engine(PlutoGESplineRoadTests)
add_test(NAME PlutoGESplineRoadTests COMMAND PlutoGESplineRoadTests)
if (PLUTOGE_TEST_DOTNET)
    add_test(NAME PlutoGEReplicationTests COMMAND ${PLUTOGE_TEST_DOTNET} run
        --project ${CMAKE_CURRENT_SOURCE_DIR}/managed/PlutoGE.ReplicationTests/PlutoGE.ReplicationTests.csproj --configuration Release)
endif()
target_link_libraries(PlutoGESceneStreamingTests PRIVATE PlutoGE::Scene)
pluto_link_engine(PlutoGESceneStreamingTests)
add_test(NAME PlutoGESceneStreamingTests COMMAND PlutoGESceneStreamingTests)
target_include_directories(PlutoGESequencerTests PRIVATE ../editor/ui/include)
pluto_link_engine(PlutoGESequencerTests)
add_test(NAME PlutoGESequencerTests COMMAND PlutoGESequencerTests)

add_executable(PlutoGETimelineEditingTests TimelineEditingTests.cpp ../engine/scene/src/Timeline.cpp)
target_include_directories(PlutoGETimelineEditingTests PRIVATE ../engine/scene/include ../editor/ui/include)
target_compile_features(PlutoGETimelineEditingTests PRIVATE cxx_std_20)
add_test(NAME PlutoGETimelineEditingTests COMMAND PlutoGETimelineEditingTests)

add_executable(PlutoGESequencerInteractionTests SequencerInteractionTests.cpp)
target_include_directories(PlutoGESequencerInteractionTests PRIVATE ../editor/ui/include)
target_link_libraries(PlutoGESequencerInteractionTests PRIVATE PlutoGE::Scene PlutoGE::imgui PlutoGE::imguizmo)
add_test(NAME PlutoGESequencerInteractionTests COMMAND PlutoGESequencerInteractionTests)
set_tests_properties(PlutoGESequencerInteractionTests PROPERTIES TIMEOUT 30)

add_executable(PlutoGEGenerateRoadCity EXCLUDE_FROM_ALL ../tools/GenerateRoadCity.cpp)
target_link_libraries(PlutoGEGenerateRoadCity PRIVATE PlutoGE::Scene)

add_executable(PlutoGERenderRoadCity EXCLUDE_FROM_ALL ../tools/RenderRoadCity.cpp)
target_link_libraries(PlutoGERenderRoadCity PRIVATE PlutoGE::Scene PlutoGE::Platform)
target_compile_definitions(PlutoGERenderRoadCity PRIVATE PLUTO_CITY_SHADER_DIR="${CMAKE_BINARY_DIR}/engine/render/shaders")

add_executable(PlutoGERhiOceanTests RhiOceanTests.cpp)
target_link_libraries(PlutoGERhiOceanTests PRIVATE PlutoGE::Scene PlutoGE::Platform)
target_compile_definitions(PlutoGERhiOceanTests PRIVATE PLUTO_OCEAN_SHADER_DIR="${CMAKE_BINARY_DIR}/engine/render/shaders")
add_test(NAME PlutoGEOpenGLOceanTests COMMAND PlutoGERhiOceanTests)
add_test(NAME PlutoGEVulkanOceanTests COMMAND PlutoGERhiOceanTests --vulkan)
set_tests_properties(PlutoGEOpenGLOceanTests PlutoGEVulkanOceanTests PROPERTIES TIMEOUT 60)

add_executable(PlutoGEOceanWaveModelTests OceanWaveModelTests.cpp)
target_link_libraries(PlutoGEOceanWaveModelTests PRIVATE PlutoGE::Scene)
add_test(NAME PlutoGEOceanWaveModelTests COMMAND PlutoGEOceanWaveModelTests)

add_test(NAME PlutoGEOpenGLOutlineTests COMMAND PlutoGEOpenGLRhiTests --outline)
add_test(NAME PlutoGEVulkanOutlineTests COMMAND PlutoGEVulkanRhiTests --outline)

add_test(NAME PlutoGEOpenGLShaderGraphTests COMMAND PlutoGEOpenGLRhiTests --shader-graphs)
add_test(NAME PlutoGEVulkanShaderGraphTests COMMAND PlutoGEVulkanRhiTests --shader-graphs)

# Accepts external document/font/panel arguments for project UI layout regressions.
add_executable(PlutoGERmlUiDocumentLayoutTests RmlUiDocumentLayoutTests.cpp)
target_link_libraries(PlutoGERmlUiDocumentLayoutTests PRIVATE RmlUi::Core)

target_include_directories(PlutoGERmlUiDocumentLayoutTests PRIVATE "${CMAKE_SOURCE_DIR}/engine/render/include")

if (PLUTOGE_TEST_DOTNET)
    add_test(NAME PlutoGEPresentationPreparationManagedTests COMMAND ${PLUTOGE_TEST_DOTNET} run
        --project ${CMAKE_CURRENT_SOURCE_DIR}/managed/PlutoGE.PresentationPreparationTests/PlutoGE.PresentationPreparationTests.csproj --configuration Release)
endif()

add_executable(PlutoGEAudioBatchLifecycleTests AudioBatchLifecycleTests.cpp)
target_link_libraries(PlutoGEAudioBatchLifecycleTests PRIVATE PlutoGE::Audio)
add_test(NAME PlutoGEAudioBatchLifecycleTests COMMAND PlutoGEAudioBatchLifecycleTests)
set_tests_properties(PlutoGEAudioBatchLifecycleTests PROPERTIES ENVIRONMENT "ALSOFT_DRIVERS=null")
add_executable(PlutoGENavigationPathTests NavigationPathTests.cpp)
target_link_libraries(PlutoGENavigationPathTests PRIVATE PlutoGE::Scene)
pluto_link_engine(PlutoGENavigationPathTests)
add_test(NAME PlutoGENavigationPathTests COMMAND PlutoGENavigationPathTests)

add_executable(PlutoGESceneCpuCacheTests SceneCpuCacheTests.cpp)
target_link_libraries(PlutoGESceneCpuCacheTests PRIVATE PlutoGE::Scene)
target_include_directories(PlutoGESceneCpuCacheTests PRIVATE ${CMAKE_SOURCE_DIR}/engine/render/src)
add_test(NAME PlutoGESceneCpuCacheTests COMMAND PlutoGESceneCpuCacheTests)

add_executable(PlutoGESnapshotDamageTests SnapshotDamageTests.cpp)
target_include_directories(PlutoGESnapshotDamageTests PRIVATE ${CMAKE_SOURCE_DIR}/engine/render/include ${CMAKE_SOURCE_DIR}/engine/render/src)
target_compile_features(PlutoGESnapshotDamageTests PRIVATE cxx_std_20)
add_test(NAME PlutoGESnapshotDamageTests COMMAND PlutoGESnapshotDamageTests)

add_executable(PlutoGERetainedRenderSceneTests RetainedRenderSceneTests.cpp)
target_link_libraries(PlutoGERetainedRenderSceneTests PRIVATE PlutoGE::Scene)
target_include_directories(PlutoGERetainedRenderSceneTests PRIVATE ${CMAKE_SOURCE_DIR}/engine/render/src)
add_test(NAME PlutoGERetainedRenderSceneTests COMMAND PlutoGERetainedRenderSceneTests)

# Every test shares one output directory. Copy OpenAL there once instead of
# from a POST_BUILD step per test, which races when tests link in parallel.
if (TARGET OpenAL)
    add_custom_target(PlutoGETestOpenAL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:OpenAL> $<TARGET_FILE_DIR:PlutoGEContentPackTests>/$<TARGET_FILE_NAME:OpenAL>
        VERBATIM)
    get_property(pluto_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(test_target IN LISTS pluto_test_targets)
        get_target_property(test_target_type ${test_target} TYPE)
        if (test_target_type STREQUAL "EXECUTABLE")
            add_dependencies(${test_target} PlutoGETestOpenAL)
        endif()
    endforeach()
endif()

# Source-session regressions need neither an engine singleton nor a GPU.
add_executable(PlutoGERmlDocumentEditSessionTests
    RmlDocumentEditSessionTests.cpp
    ${CMAKE_SOURCE_DIR}/editor/ui/src/RmlDocumentEditSession.cpp)
target_include_directories(PlutoGERmlDocumentEditSessionTests PRIVATE ${CMAKE_SOURCE_DIR}/editor/ui/include)
target_compile_features(PlutoGERmlDocumentEditSessionTests PRIVATE cxx_std_20)
add_test(NAME PlutoGERmlDocumentEditSessionTests COMMAND PlutoGERmlDocumentEditSessionTests)
if(TARGET PlutoGEEditorUI)
    add_executable(PlutoGERmlDocumentPreviewTests RmlDocumentPreviewTests.cpp)
    target_link_libraries(PlutoGERmlDocumentPreviewTests PRIVATE PlutoGE::EditorUI)
    target_compile_definitions(PlutoGERmlDocumentPreviewTests PRIVATE
        PLUTO_RML_TEST_FONT="${CMAKE_SOURCE_DIR}/editor/resources/fonts/MartianMono-StdRg.ttf")
    pluto_link_engine(PlutoGERmlDocumentPreviewTests)
    add_test(NAME PlutoGERmlDocumentPreviewOpenGLTests COMMAND PlutoGERmlDocumentPreviewTests)
    add_test(NAME PlutoGERmlDocumentPreviewVulkanTests COMMAND PlutoGERmlDocumentPreviewTests --vulkan)
endif()

add_executable(PlutoGESourceTextEditorTests SourceTextEditorTests.cpp
    ${CMAKE_SOURCE_DIR}/editor/ui/src/SourceTextEditor.cpp)
target_include_directories(PlutoGESourceTextEditorTests PRIVATE ${CMAKE_SOURCE_DIR}/editor/ui/include)
target_link_libraries(PlutoGESourceTextEditorTests PRIVATE PlutoGE::imgui)
target_compile_features(PlutoGESourceTextEditorTests PRIVATE cxx_std_20)
add_test(NAME PlutoGESourceTextEditorTests COMMAND PlutoGESourceTextEditorTests)

add_executable(PlutoGERmlUiInterfaceScaleTests RmlUiInterfaceScaleTests.cpp)
pluto_link_engine(PlutoGERmlUiInterfaceScaleTests)
target_link_libraries(PlutoGERmlUiInterfaceScaleTests PRIVATE RmlUi::Core)
add_test(NAME PlutoGEVulkanUiInterfaceScale COMMAND PlutoGERmlUiInterfaceScaleTests)
add_test(NAME PlutoGEOpenGLUiInterfaceScale COMMAND PlutoGERmlUiInterfaceScaleTests --opengl)

if (PLUTOGE_TEST_DOTNET)
    add_test(NAME PlutoGESkeletalManagedTests COMMAND ${PLUTOGE_TEST_DOTNET} run
        --project ${CMAKE_CURRENT_SOURCE_DIR}/managed/PlutoGE.SkeletalSmokeTests/PlutoGE.SkeletalSmokeTests.csproj --configuration Release)
endif()
