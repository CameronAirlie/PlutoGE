add_library(PlutoGE_glad STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/glad/src/glad.c
)
add_library(PlutoGE::glad ALIAS PlutoGE_glad)

target_include_directories(PlutoGE_glad PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/glad/include
)

include(FetchContent)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(glfw)
add_library(PlutoGE_glfw INTERFACE)
add_library(PlutoGE::glfw ALIAS PlutoGE_glfw)
target_link_libraries(PlutoGE_glfw INTERFACE glfw)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(glm)
add_library(PlutoGE_glm INTERFACE)
add_library(PlutoGE::glm ALIAS PlutoGE_glm)
target_include_directories(PlutoGE_glm INTERFACE
    ${glm_SOURCE_DIR}
)

FetchContent_Declare(
    stb_image
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(stb_image)
add_library(PlutoGE_stb_image INTERFACE)
add_library(PlutoGE::stb_image ALIAS PlutoGE_stb_image)
target_include_directories(PlutoGE_stb_image INTERFACE
    ${stb_image_SOURCE_DIR}
)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
)
FetchContent_MakeAvailable(imgui)
add_library(PlutoGE_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
add_library(PlutoGE::imgui ALIAS PlutoGE_imgui)
target_link_libraries(PlutoGE_imgui PUBLIC
    PlutoGE::glad
    PlutoGE::glfw
)
target_include_directories(PlutoGE_imgui PUBLIC
    ${imgui_SOURCE_DIR}
)