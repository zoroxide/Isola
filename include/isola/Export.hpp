#pragma once

/// ISOLA_API marks the symbols of the public interface. Isola builds as a static library by default;
/// for a shared build define ISOLA_SHARED (CMake does this with BUILD_SHARED_LIBS=ON) and
/// ISOLA_BUILDING while compiling the library itself.
#if defined(ISOLA_SHARED)
#  if defined(_WIN32)
#    if defined(ISOLA_BUILDING)
#      define ISOLA_API __declspec(dllexport)
#    else
#      define ISOLA_API __declspec(dllimport)
#    endif
#  else
#    define ISOLA_API __attribute__((visibility("default")))
#  endif
#else
#  define ISOLA_API
#endif
