#pragma once

#ifndef _WIN32

#include <algorithm>
#include <cstddef>
#include <cstring>

#ifndef _TRUNCATE
#define _TRUNCATE static_cast<std::size_t>(-1)
#endif

inline int strncpy_s(char *destination, std::size_t destinationSize,
                     const char *source, std::size_t count)
{
    if (!destination || destinationSize == 0)
    {
        return 22;
    }

    if (!source)
    {
        destination[0] = '\0';
        return 22;
    }

    const std::size_t sourceLength = std::strlen(source);
    const std::size_t requestedLength = count == _TRUNCATE ? sourceLength : count;
    const std::size_t copyLength = std::min({sourceLength, requestedLength, destinationSize - 1});
    std::memcpy(destination, source, copyLength);
    destination[copyLength] = '\0';
    return copyLength < sourceLength && count != _TRUNCATE ? 34 : 0;
}

template <std::size_t Size>
inline int strncpy_s(char (&destination)[Size], const char *source, std::size_t count)
{
    return strncpy_s(destination, Size, source, count);
}

#endif
