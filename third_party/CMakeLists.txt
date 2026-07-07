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

set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG        v5.4.3
)
FetchContent_MakeAvailable(assimp)
add_library(PlutoGE_assimp INTERFACE)
add_library(PlutoGE::assimp ALIAS PlutoGE_assimp)
target_link_libraries(PlutoGE_assimp INTERFACE assimp)

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

set(ALSOFT_UTILS OFF CACHE BOOL "" FORCE)
set(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ALSOFT_TESTS OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    openal_soft
    GIT_REPOSITORY https://github.com/kcat/openal-soft.git
    GIT_TAG        1.25.2
)
FetchContent_MakeAvailable(openal_soft)
add_library(PlutoGE_openal_soft INTERFACE)
add_library(PlutoGE::openal_soft ALIAS PlutoGE_openal_soft)
if (TARGET OpenAL)
    target_link_libraries(PlutoGE_openal_soft INTERFACE OpenAL)
elseif (TARGET OpenAL::OpenAL)
    target_link_libraries(PlutoGE_openal_soft INTERFACE OpenAL::OpenAL)
endif()
target_include_directories(PlutoGE_openal_soft INTERFACE
    ${openal_soft_SOURCE_DIR}/include
)

set(BUILD_BULLET2_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_BULLET3 OFF CACHE BOOL "" FORCE)
set(BUILD_CPU_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(BUILD_OPENGL3_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(INSTALL_LIBS OFF CACHE BOOL "" FORCE)
set(USE_GRAPHICAL_BENCHMARK OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    bullet3
    GIT_REPOSITORY https://github.com/bulletphysics/bullet3.git
    GIT_TAG        master
)
FetchContent_GetProperties(bullet3)
if (NOT bullet3_POPULATED)
    FetchContent_Populate(bullet3)
endif()
set(BULLET_PHYSICS_SOURCE_DIR ${bullet3_SOURCE_DIR})
set(BULLET_VERSION 3.25)
add_subdirectory(${bullet3_SOURCE_DIR}/src/LinearMath ${bullet3_BINARY_DIR}/src/LinearMath)
add_subdirectory(${bullet3_SOURCE_DIR}/src/BulletCollision ${bullet3_BINARY_DIR}/src/BulletCollision)
add_subdirectory(${bullet3_SOURCE_DIR}/src/BulletDynamics ${bullet3_BINARY_DIR}/src/BulletDynamics)
add_library(PlutoGE_bullet INTERFACE)
add_library(PlutoGE::bullet ALIAS PlutoGE_bullet)
target_link_libraries(PlutoGE_bullet INTERFACE
    BulletDynamics
    BulletCollision
    LinearMath
)
target_include_directories(PlutoGE_bullet INTERFACE
    ${bullet3_SOURCE_DIR}/src
)
