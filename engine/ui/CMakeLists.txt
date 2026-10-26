# Native UI core: layout, text and backend-neutral drawing. Depends only on
# glm and FreeType so it can be unit tested without the scene or the renderer.
add_library(PlutoGEUI
    src/layout/Layout.cpp
    src/render/DrawList.cpp
    src/render/Painter.cpp
    src/text/FontFace.cpp
    src/text/FontLibrary.cpp
    src/text/GlyphAtlas.cpp
    src/text/RichText.cpp
    src/text/TextLayout.cpp
    src/text/TextShaper.cpp
    src/text/Utf8.cpp
)
add_library(PlutoGE::UI ALIAS PlutoGEUI)

target_include_directories(PlutoGEUI PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(PlutoGEUI
    PUBLIC PlutoGE::glm
    PRIVATE Freetype::Freetype
)
