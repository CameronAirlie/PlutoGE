cmake_minimum_required(VERSION 3.10.0)
project(PlutoGE VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


option(PLUTO_BUILD_EDITOR "Build the PlutoGE Editor" ON)
option(PLUTO_BUILD_RUNTIME "Build the PlutoGE Runtime" ON)
option(PLUTO_BUILD_SAMPLES "Build sample applications" ON)
option(PLUTOGE_USE_NVRHI "Legacy alias for PLUTO_WITH_NVRHI" OFF)
option(PLUTO_WITH_NVRHI "Enable NVIDIA NVRHI renderer backends" ${PLUTOGE_USE_NVRHI})

add_subdirectory(third_party)
add_subdirectory(engine)

if (PLUTO_BUILD_EDITOR)
    add_subdirectory(editor)
endif()

if (PLUTO_BUILD_RUNTIME)
    add_subdirectory(runtime)
endif()

# if (PLUTO_BUILD_SAMPLES)
#     add_subdirectory(samples)
# endif()

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
