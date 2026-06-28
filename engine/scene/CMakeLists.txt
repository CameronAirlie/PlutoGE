add_library(PlutoGEScene
    src/Entity.cpp
    src/Prefab.cpp
    src/Scene.cpp
    src/SceneBaker.cpp
    src/SceneSerializer.cpp
    src/components/AnimationComponent.cpp
    src/components/MeshComponent.cpp
    src/components/SkeletonAttachmentComponent.cpp
    src/components/CameraComponent.cpp
    src/components/ColliderComponent.cpp
    src/components/FoliageComponent.cpp
    src/components/IblCaptureComponent.cpp
    src/components/LightComponent.cpp
    src/components/PhysicalSkyComponent.cpp
    src/components/RigidbodyComponent.cpp
    src/components/ScriptComponent.cpp
    src/components/TerrainComponent.cpp
    src/components/UIComponent.cpp
    src/components/VolumetricCloudComponent.cpp
)

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
