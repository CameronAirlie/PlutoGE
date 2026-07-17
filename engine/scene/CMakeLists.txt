pluto_add_engine_module(PlutoGEScene PlutoGE::Scene PLUTO_SCENE_TARGET
    src/Entity.cpp
    src/Prefab.cpp
    src/Scene.cpp
    src/SceneBaker.cpp
    src/SceneSerializer.cpp
    src/NavigationSystem.cpp
    src/components/Component.cpp
    src/components/ComponentFactory.cpp
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

target_include_directories(${PLUTO_SCENE_TARGET} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../import/include
)

if (NOT PLUTO_BUILD_ENGINE_SHARED)
    target_link_libraries(${PLUTO_SCENE_TARGET} PUBLIC
        PlutoGE::Core
        PlutoGE::Render
        PlutoGE::Assets
        PlutoGE::Scripting
        PlutoGE::bullet
        PlutoGE::stb_image
    )
endif()
