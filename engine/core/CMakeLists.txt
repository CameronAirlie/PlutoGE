add_library(PlutoGECore
    src/Engine.cpp
    src/SceneLoading.cpp
    src/LoadingScreenSession.cpp
    src/TaskPool.cpp
    src/FrameWorker.cpp
    src/FramePacer.cpp
    src/PreciseWait.cpp
)

add_library(PlutoGE::Core ALIAS PlutoGECore)

target_include_directories(PlutoGECore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(PlutoGECore PUBLIC
    # Add dependencies here
    PlutoGE::Audio
    PlutoGE::Platform
    PlutoGE::Render
    PlutoGE::Assets
    PlutoGE::Import
    PlutoGE::Scripting
)

if (WIN32)
    # PreciseWait raises the system timer resolution where high-resolution waitable timers are unavailable.
    target_link_libraries(PlutoGECore PRIVATE winmm)
endif()
