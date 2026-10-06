#pragma once

/// NUT_API marks the symbols of the public interface. Nut builds as a static library by default;
/// for a shared build define NUT_SHARED (CMake does this with BUILD_SHARED_LIBS=ON) and
/// NUT_BUILDING while compiling the library itself.
#if defined(NUT_SHARED)
#  if defined(_WIN32)
#    if defined(NUT_BUILDING)
#      define NUT_API __declspec(dllexport)
#    else
#      define NUT_API __declspec(dllimport)
#    endif
#  else
#    define NUT_API __attribute__((visibility("default")))
#  endif
#else
#  define NUT_API
#endif
