add_executable(PlutoGELargeMeshOptimizationTests
    LargeMeshOptimizationTests.cpp
)

if (CMAKE_CXX_LINK_GROUP_USING_RESCAN_SUPPORTED)
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

if (CMAKE_CXX_LINK_GROUP_USING_RESCAN_SUPPORTED)
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

if (CMAKE_CXX_LINK_GROUP_USING_RESCAN_SUPPORTED)
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

if (CMAKE_CXX_LINK_GROUP_USING_RESCAN_SUPPORTED)
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

if (CMAKE_CXX_LINK_GROUP_USING_RESCAN_SUPPORTED)
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

if (CMAKE_CXX_LINK_GROUP_USING_RESCAN_SUPPORTED)
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
