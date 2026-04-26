add_library(PlutoGEPlatform
    src/Window.cpp
)

add_library(PlutoGE::Platform ALIAS PlutoGEPlatform)


target_link_libraries(PlutoGEPlatform PUBLIC
    PlutoGE::glfw
    PlutoGE::glad
)

target_include_directories(PlutoGEPlatform PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)