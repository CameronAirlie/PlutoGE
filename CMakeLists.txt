cmake_minimum_required(VERSION 3.10.0)
project(PlutoGE VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PLUTO_BUILD_EDITOR "Build the PlutoGE Editor" ON)
option(PLUTO_BUILD_RUNTIME "Build the PlutoGE Runtime" ON)
option(PLUTO_BUILD_SAMPLES "Build sample applications" ON)
option(PLUTO_BUILD_ENGINE_SHARED "Build the engine as a single shared library (PlutoGE.dll on Windows)" OFF)
option(PLUTO_BUILD_ELECTRON_EDITOR_HOST "Build the native host used by the Electron editor" ${PLUTO_BUILD_ENGINE_SHARED})

if (PLUTO_BUILD_ELECTRON_EDITOR_HOST AND NOT PLUTO_BUILD_ENGINE_SHARED)
    message(FATAL_ERROR "PLUTO_BUILD_ELECTRON_EDITOR_HOST requires PLUTO_BUILD_ENGINE_SHARED=ON")
endif()

if (PLUTO_BUILD_ENGINE_SHARED)
    # Static third-party libraries linked into PlutoGE must be suitable for a
    # shared library on platforms that require position-independent code.
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)

    # Keep the engine DLL and its consumers together so editor/runtime targets
    # can be launched directly from the build tree on Windows.
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

add_subdirectory(engine)
add_subdirectory(third_party)

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

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
