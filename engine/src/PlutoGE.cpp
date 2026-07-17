// The shared engine target is composed from the sources of each engine module.
// This translation unit gives the target a stable root source for older CMake
// versions that require one when add_library() is called.
namespace
{
    constexpr bool sharedLibraryAnchor = true;
    static_assert(sharedLibraryAnchor);
}
