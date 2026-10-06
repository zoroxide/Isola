#pragma once

// The CMake project version is read from these lines.
#define NUT_VERSION_MAJOR 0
#define NUT_VERSION_MINOR 1
#define NUT_VERSION_PATCH 0

#define NUT_VERSION_STRING "0.1.0"

namespace nut {

struct Version {
    int major, minor, patch;
};

/// Version of the Nut headers being compiled against.
inline constexpr Version kVersion{NUT_VERSION_MAJOR, NUT_VERSION_MINOR, NUT_VERSION_PATCH};

} // namespace nut
