add_library(PlutoGE_glad STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/glad/src/glad.c
)
add_library(PlutoGE::glad ALIAS PlutoGE_glad)

target_include_directories(PlutoGE_glad PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/glad/include
)

include(FetchContent)

option(PLUTO_WITH_NVRHI "Fetch and expose NVIDIA NVRHI for the non-OpenGL renderer backends" OFF)

if (PLUTO_WITH_NVRHI)
FetchContent_Declare(
    nvrhi
    GIT_REPOSITORY https://github.com/NVIDIA-RTX/NVRHI.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(nvrhi)
endif()

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

set(PLUTO_IMGUI_BACKEND_SOURCES
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

if (PLUTO_WITH_NVRHI AND WIN32)
    list(APPEND PLUTO_IMGUI_BACKEND_SOURCES
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
    )
endif()

add_library(PlutoGE_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${PLUTO_IMGUI_BACKEND_SOURCES}
)
add_library(PlutoGE::imgui ALIAS PlutoGE_imgui)
target_link_libraries(PlutoGE_imgui PUBLIC
    PlutoGE::glfw
    PlutoGE::glad
)

if (PLUTO_WITH_NVRHI AND WIN32)
    target_link_libraries(PlutoGE_imgui PUBLIC
        d3d12
        dxgi
    )
endif()

target_include_directories(PlutoGE_imgui PUBLIC
    ${imgui_SOURCE_DIR}
)

FetchContent_Declare(
    imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(imguizmo)
target_include_directories(imguizmo PUBLIC
    ${imgui_SOURCE_DIR}
)
add_library(PlutoGE::imguizmo ALIAS imguizmo)

FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG        release
)
FetchContent_MakeAvailable(tinygltf)
add_library(PlutoGE_tinygltf INTERFACE)
add_library(PlutoGE::tinygltf ALIAS PlutoGE_tinygltf)
target_include_directories(PlutoGE_tinygltf INTERFACE
    ${tinygltf_SOURCE_DIR}
)

FetchContent_Declare(
    meshoptimizer
    GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(meshoptimizer)
add_library(PlutoGE_meshoptimizer INTERFACE)
add_library(PlutoGE::meshoptimizer ALIAS PlutoGE_meshoptimizer)
target_link_libraries(PlutoGE_meshoptimizer INTERFACE meshoptimizer)
target_include_directories(PlutoGE_meshoptimizer INTERFACE
    ${meshoptimizer_SOURCE_DIR}/src
)
