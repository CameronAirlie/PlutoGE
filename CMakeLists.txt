cmake_minimum_required(VERSION 3.10.0)
project(PlutoGE VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/EHsc>")
endif()

option(PLUTO_BUILD_EDITOR "Build the PlutoGE Editor" ON)
option(PLUTO_BUILD_RUNTIME "Build the PlutoGE Runtime" ON)
option(PLUTO_BUILD_SAMPLES "Build sample applications" ON)

add_subdirectory(engine)

# PlutoGE ships a curated runtime layout. Dependency projects must not append
# their developer headers, libraries, samples, or data to the end-user package.
set(CMAKE_SKIP_INSTALL_RULES ON)
add_subdirectory(third_party)
set(CMAKE_SKIP_INSTALL_RULES OFF)

if (PLUTO_BUILD_EDITOR)
    add_subdirectory(editor)
endif()

if (PLUTO_BUILD_RUNTIME)
    add_subdirectory(runtime)
endif()

if (BUILD_TESTING)
    add_subdirectory(tests)
endif()

# if (PLUTO_BUILD_SAMPLES)
#     add_subdirectory(samples)
# endif()

if (WIN32)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT Runtime)
    include(InstallRequiredSystemLibraries)
endif()

if (TARGET OpenAL)
    install(FILES "$<TARGET_FILE:OpenAL>"
        DESTINATION .
        COMPONENT Runtime
    )
endif()

find_program(PLUTO_DOTNET_EXECUTABLE dotnet)
set(PLUTO_SCRIPT_CORE_PROJECT
    "${CMAKE_SOURCE_DIR}/engine/scripting/managed/PlutoGE.ScriptCore/PlutoGE.ScriptCore.csproj")
set(PLUTO_SCRIPT_CORE_OUTPUT
    "${CMAKE_SOURCE_DIR}/engine/scripting/managed/PlutoGE.ScriptCore/bin/Release/net8.0")

if (PLUTO_DOTNET_EXECUTABLE)
    add_custom_target(PlutoGEScriptCore ALL
        COMMAND "${PLUTO_DOTNET_EXECUTABLE}" build "${PLUTO_SCRIPT_CORE_PROJECT}"
                --configuration Release --framework net8.0
        BYPRODUCTS
            "${PLUTO_SCRIPT_CORE_OUTPUT}/PlutoGE.ScriptCore.dll"
            "${PLUTO_SCRIPT_CORE_OUTPUT}/PlutoGE.ScriptCore.deps.json"
            "${PLUTO_SCRIPT_CORE_OUTPUT}/PlutoGE.ScriptCore.runtimeconfig.json"
        COMMENT "Building the PlutoGE managed scripting SDK"
        VERBATIM
    )
    if (TARGET PlutoGEEditor)
        add_dependencies(PlutoGEEditor PlutoGEScriptCore)
    endif()
else()
    message(WARNING "dotnet was not found; the installable ScriptCore SDK cannot be built")
endif()

install(DIRECTORY
    "${CMAKE_SOURCE_DIR}/engine/scripting/managed/PlutoGE.ScriptCore/"
    DESTINATION "SDK/PlutoGE.ScriptCore"
    COMPONENT SDK
    PATTERN "bin" EXCLUDE
    PATTERN "obj" EXCLUDE
    PATTERN "*.lscache" EXCLUDE
)
if (PLUTO_DOTNET_EXECUTABLE)
    install(DIRECTORY "${PLUTO_SCRIPT_CORE_OUTPUT}/"
        DESTINATION "SDK/PlutoGE.ScriptCore/bin/Release/net8.0"
        COMPONENT SDK
    )
endif()

install(FILES
    "${CMAKE_SOURCE_DIR}/README.md"
    "${CMAKE_SOURCE_DIR}/EXPORTING.md"
    "${CMAKE_SOURCE_DIR}/DISTRIBUTING.md"
    DESTINATION "Documentation"
    COMPONENT Documentation
)
install(FILES "${CMAKE_SOURCE_DIR}/LICENSE"
    DESTINATION .
    COMPONENT Documentation
)
install(FILES
    "${CMAKE_SOURCE_DIR}/editor/resources/fonts/MartianMono-OFL.txt"
    DESTINATION "Licenses"
    RENAME "MartianMono-OFL-1.1.txt"
    COMPONENT Documentation
)

function(pluto_install_dependency_license source_path output_name)
    if (EXISTS "${source_path}")
        install(FILES "${source_path}"
            DESTINATION "Licenses/ThirdParty"
            RENAME "${output_name}"
            COMPONENT Documentation
        )
    endif()
endfunction()

include(FetchContent)
FetchContent_GetProperties(glfw)
FetchContent_GetProperties(glm)
FetchContent_GetProperties(stb_image)
FetchContent_GetProperties(imgui)
FetchContent_GetProperties(imguizmo)
FetchContent_GetProperties(tinygltf)
FetchContent_GetProperties(assimp)
FetchContent_GetProperties(meshoptimizer)
FetchContent_GetProperties(openal_soft)
FetchContent_GetProperties(bullet3)

pluto_install_dependency_license("${glfw_SOURCE_DIR}/LICENSE.md" "GLFW-LICENSE.md")
pluto_install_dependency_license("${glm_SOURCE_DIR}/copying.txt" "GLM-LICENSE.txt")
pluto_install_dependency_license("${stb_image_SOURCE_DIR}/LICENSE" "stb-LICENSE.txt")
pluto_install_dependency_license("${imgui_SOURCE_DIR}/LICENSE.txt" "Dear-ImGui-LICENSE.txt")
pluto_install_dependency_license("${imguizmo_SOURCE_DIR}/LICENSE" "ImGuizmo-LICENSE.txt")
pluto_install_dependency_license("${tinygltf_SOURCE_DIR}/LICENSE" "tinygltf-LICENSE.txt")
pluto_install_dependency_license("${assimp_SOURCE_DIR}/LICENSE" "Assimp-LICENSE.txt")
pluto_install_dependency_license("${meshoptimizer_SOURCE_DIR}/LICENSE.md" "meshoptimizer-LICENSE.md")
pluto_install_dependency_license("${openal_soft_SOURCE_DIR}/COPYING" "OpenAL-Soft-COPYING.txt")
pluto_install_dependency_license("${bullet3_SOURCE_DIR}/LICENSE.txt" "Bullet-LICENSE.txt")

set(CPACK_PACKAGE_NAME "PlutoGE")
set(CPACK_PACKAGE_VENDOR "PlutoGE")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "PlutoGE 3D game engine and editor")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "PlutoGE")
set(CPACK_PACKAGE_EXECUTABLES "PlutoGEEditor;PlutoGE Editor")
set(CPACK_CREATE_DESKTOP_LINKS "PlutoGEEditor")
set(CPACK_NSIS_DISPLAY_NAME "PlutoGE")
set(CPACK_NSIS_PACKAGE_NAME "PlutoGE")
set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/packaging/branding/PlutoGE.ico")
set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/packaging/branding/PlutoGE.ico")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_MODIFY_PATH OFF)
set(CPACK_NSIS_MUI_FINISHPAGE_RUN "PlutoGEEditor.exe")
set(CPACK_COMPONENTS_ALL Runtime SDK Documentation)
set(CPACK_INSTALL_CMAKE_PROJECTS
    "${CMAKE_BINARY_DIR};${PROJECT_NAME};Runtime;/"
    "${CMAKE_BINARY_DIR};${PROJECT_NAME};SDK;/"
    "${CMAKE_BINARY_DIR};${PROJECT_NAME};Documentation;/"
)
set(CPACK_MONOLITHIC_INSTALL ON)
set(CPACK_GENERATOR "ZIP;NSIS64")
set(CPACK_SOURCE_GENERATOR "")
include(CPack)
