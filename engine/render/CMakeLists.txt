add_library(PlutoGERender
    src/Renderer.cpp
    src/TextureManager.cpp
    src/Shader.cpp
    src/Texture.cpp
    src/Graphics.cpp
)

add_library(PlutoGE::Render ALIAS PlutoGERender)

target_include_directories(PlutoGERender PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(PlutoGERender PUBLIC
    # Add dependencies here
    PlutoGE::Platform
    PlutoGE::Assets
    PlutoGE::Core
    PlutoGE::glad
    PlutoGE::stb_image
)