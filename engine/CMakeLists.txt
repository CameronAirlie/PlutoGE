add_subdirectory(core)
add_subdirectory(platform)
add_subdirectory(render)
add_subdirectory(assets)
add_subdirectory(scene)
    
add_library(PlutoGE INTERFACE)
add_library(PlutoGE::engine ALIAS PlutoGE)

target_link_libraries(PlutoGE
    INTERFACE 
        PlutoGE::Core
        PlutoGE::Platform
        PlutoGE::Render
        PlutoGE::Assets
        PlutoGE::Scene
)