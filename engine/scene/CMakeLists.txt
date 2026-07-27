add_library(PlutoGEScene
    src/Entity.cpp
    src/Prefab.cpp
    src/Scene.cpp
    src/SceneBaker.cpp
    src/SceneSerializer.cpp
    src/NavigationSystem.cpp
    src/components/AnimationComponent.cpp
    src/components/ClothComponent.cpp
    src/components/MeshComponent.cpp
    src/components/NavAgentComponent.cpp
    src/components/NavigationMeshComponent.cpp
    src/components/OceanComponent.cpp
    src/components/SkeletonAttachmentComponent.cpp
    src/components/CameraComponent.cpp
    src/components/ColliderComponent.cpp
    src/components/FoliageComponent.cpp
    src/components/IblCaptureComponent.cpp
    src/components/LightComponent.cpp
    src/components/PhysicalSkyComponent.cpp
    src/components/ParticleSystemComponent.cpp
    src/components/RigidbodyComponent.cpp
    src/components/ScriptComponent.cpp
    src/components/SoundEmitterComponent.cpp
    src/components/SoundListenerComponent.cpp
    src/components/SplineComponent.cpp
    src/components/TerrainComponent.cpp
    src/components/UIComponent.cpp
    src/components/VolumetricCloudComponent.cpp
)

# Scene baking performs millions of BVH, spatial-hash, and image-filter
# operations. Keeping this translation unit unoptimized makes editor Debug
# bakes orders of magnitude slower, particularly generated-UV seam stitching.
# Optimize only the baker while retaining Debug symbols and Debug behavior for
# the rest of the scene module.
if(MSVC)
    set_property(SOURCE src/SceneBaker.cpp APPEND PROPERTY
        COMPILE_FLAGS "$<$<CONFIG:Debug>:/O2 /Ob2>")
    set_property(SOURCE src/SceneBaker.cpp APPEND PROPERTY
        VS_SETTINGS "$<$<CONFIG:Debug>:BasicRuntimeChecks=Default>")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set_property(SOURCE src/SceneBaker.cpp APPEND PROPERTY
        COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O3>")
endif()

add_library(PlutoGE::Scene ALIAS PlutoGEScene)
target_include_directories(PlutoGEScene PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../import/include
)

target_link_libraries(PlutoGEScene PUBLIC
    PlutoGE::Core
    PlutoGE::Render
    PlutoGE::Assets
    PlutoGE::Scripting
    PlutoGE::bullet
    PlutoGE::stb_image
)
