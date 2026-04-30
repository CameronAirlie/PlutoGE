add_library(PlutoGEScene
    src/Entity.cpp
    src/Scene.cpp
    src/components/MeshComponent.cpp
    src/components/CameraComponent.cpp
    src/components/ScriptComponent.cpp
)

add_library(PlutoGE::Scene ALIAS PlutoGEScene)
target_include_directories(PlutoGEScene PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(PlutoGEScene PUBLIC
    PlutoGE::Core
    PlutoGE::Render
    PlutoGE::Assets
    PlutoGE::Scripting
)