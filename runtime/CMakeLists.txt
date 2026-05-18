add_executable(PlutoGERuntime
    src/main.cpp
)

target_include_directories(PlutoGERuntime
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(PlutoGERuntime
    PRIVATE
        PlutoGE::Platform
        PlutoGE::Render
        PlutoGE::Assets
        PlutoGE::Core
        PlutoGE::Scene
        PlutoGE::glm
)

add_dependencies(PlutoGERuntime PlutoGE::Platform PlutoGE::Render PlutoGE::Assets PlutoGE::Core PlutoGE::Scene)