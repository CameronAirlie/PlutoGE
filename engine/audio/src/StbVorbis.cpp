// The Ogg Vorbis decoder used by AudioSystem (stb_vorbis, public domain; the pinned stb checkout also provides
// stb_image). Decoding is from memory: clips are read through the content system so packed builds work.
#define STB_VORBIS_NO_STDIO
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif
#include <stb_vorbis.c>
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
