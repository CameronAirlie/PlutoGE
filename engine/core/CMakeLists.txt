add_library(PlutoGECore
    src/Engine.cpp
)

add_library(PlutoGE::Core ALIAS PlutoGECore)

target_include_directories(PlutoGECore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(PlutoGECore PUBLIC
    # Add dependencies here
    PlutoGE::Platform
    PlutoGE::Render
    PlutoGE::Assets
)