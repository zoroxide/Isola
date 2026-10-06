#pragma once

// The CMake project version is read from these lines.
#define ISOLA_VERSION_MAJOR 0
#define ISOLA_VERSION_MINOR 2
#define ISOLA_VERSION_PATCH 2

#define ISOLA_VERSION_STRING "0.2.2"

namespace isola {

struct Version {
    int major, minor, patch;
};

/// Version of the Isola headers being compiled against.
inline constexpr Version kVersion{ISOLA_VERSION_MAJOR, ISOLA_VERSION_MINOR, ISOLA_VERSION_PATCH};

} // namespace isola
