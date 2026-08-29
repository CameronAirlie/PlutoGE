add_library(PlutoGE_glad STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/glad/src/glad.c
)
add_library(PlutoGE::glad ALIAS PlutoGE_glad)

target_include_directories(PlutoGE_glad PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/glad/include
)

include(FetchContent)

# Vulkan is loaded dynamically through Volk, so development builds do not
# require a machine-wide Vulkan SDK or a link-time dependency on vulkan-1.
set(VULKAN_HEADERS_ENABLE_MODULE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG        vulkan-sdk-1.3.296.0
)
FetchContent_MakeAvailable(vulkan_headers)

set(VOLK_PULL_IN_VULKAN OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG        vulkan-sdk-1.3.296.0
)
FetchContent_MakeAvailable(volk)
# Volk's own CMake project is populated from a different source directory, so
# attaching Vulkan::Headers here would require the broad CMP0079 policy. Give
# the loader its one concrete compile requirement directly instead.
target_include_directories(volk PUBLIC
    ${vulkan_headers_SOURCE_DIR}/include
)

FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.1.0
)
FetchContent_MakeAvailable(vma)

add_library(PlutoGE_vulkan INTERFACE)
add_library(PlutoGE::vulkan ALIAS PlutoGE_vulkan)
target_link_libraries(PlutoGE_vulkan INTERFACE
    volk
    Vulkan::Headers
    GPUOpen::VulkanMemoryAllocator
)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        463cf73610d911e8eff95ae345143137cf610be4
)
FetchContent_MakeAvailable(glfw)
add_library(PlutoGE_glfw INTERFACE)
add_library(PlutoGE::glfw ALIAS PlutoGE_glfw)
target_link_libraries(PlutoGE_glfw INTERFACE glfw)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        6f14f4792a0cde5d0cf2c910506724d61cb95834
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
    GIT_TAG        31c1ad37456438565541f4919958214b6e762fb4
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
    GIT_TAG        b334d19b667958ed970000073644d911fae17e57
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
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
add_library(PlutoGE::imgui ALIAS PlutoGE_imgui)
target_link_libraries(PlutoGE_imgui PUBLIC
    PlutoGE::glad
    PlutoGE::glfw
    volk
)
target_compile_definitions(PlutoGE_imgui PRIVATE IMGUI_IMPL_VULKAN_USE_VOLK)
target_include_directories(PlutoGE_imgui PUBLIC
    ${imgui_SOURCE_DIR}
)

# RmlUi powers document-driven runtime/game interfaces. Keep its optional
# samples, tests, Lua bindings, and bundled font engine out of PlutoGE builds;
# PlutoGE provides the OpenGL renderer, input, and asset integration.
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG        VER-2-13-3
)
FetchContent_MakeAvailable(freetype)
if (TARGET freetype AND NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()

set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_TESTS OFF CACHE BOOL "" FORCE)
set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
set(RMLUI_FONT_ENGINE "freetype" CACHE STRING "" FORCE)
FetchContent_Declare(
    rmlui
    GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
    GIT_TAG        6.1
)
FetchContent_MakeAvailable(rmlui)
if (MSVC)
    # VS 2026's dependency scanner and shared PDB writer are unstable when the
    # RmlUi core translation units are compiled concurrently.
    set_property(TARGET rmlui_core PROPERTY CXX_SCAN_FOR_MODULES OFF)
    set_property(TARGET rmlui_core PROPERTY VS_GLOBAL_MultiProcessorCompilation "false")
    target_compile_options(rmlui_core PRIVATE /FS)
endif()
add_library(PlutoGE_rmlui STATIC
    "${rmlui_SOURCE_DIR}/Backends/RmlUi_Renderer_GL3.cpp"
    "${rmlui_SOURCE_DIR}/Backends/RmlUi_Platform_GLFW.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/rmlui/PlutoGE_RmlUi_Target.cpp"
)
add_library(PlutoGE::rmlui ALIAS PlutoGE_rmlui)
target_compile_definitions(PlutoGE_rmlui PRIVATE
    RMLUI_GL3_CUSTOM_LOADER="PlutoGE_RmlUi_GL3.h"
    RMLUI_NUM_MSAA_SAMPLES=0
)
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # RmlUi 6.1's bundled robin_hood header relies on fixed-width integer
    # types being transitively included, which GCC 16 no longer guarantees.
    target_compile_options(PlutoGE_rmlui PRIVATE -include cstdint)
endif()
target_link_libraries(PlutoGE_rmlui PUBLIC RmlUi::Core PlutoGE::glfw PlutoGE::glad)
target_include_directories(PlutoGE_rmlui PUBLIC
    "${rmlui_SOURCE_DIR}/Backends"
    "${CMAKE_CURRENT_SOURCE_DIR}/rmlui"
)

FetchContent_Declare(
    imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG        dc25afb98bc3ebe00dfc9a23ba7235fead2ccb1d
)
FetchContent_MakeAvailable(imguizmo)
target_include_directories(imguizmo PUBLIC
    ${imgui_SOURCE_DIR}
)
add_library(PlutoGE::imguizmo ALIAS imguizmo)

FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG        v2.9.6
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
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
# Avoid Arch's system minizip pkg-config layout (`minizip/unzip.h`), which is
# incompatible with Assimp's expected flat include. Use Assimp's pinned copy.
set(ZLIB_FOUND OFF CACHE BOOL "" FORCE)
set(UNZIP_FOUND OFF CACHE BOOL "" FORCE)
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
    GIT_TAG        a6ecc73c094bdd5d09644f9286bbf25134853679
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
    GIT_TAG        63c4d67e337017f9d8b298c900e9aabdb69296e7
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
