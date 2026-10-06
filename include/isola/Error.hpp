#pragma once
#include <isola/Export.hpp>

#include <stdexcept>
#include <string>

namespace isola {

/// Thrown when the engine cannot do what was asked: no OpenGL 3.3 context, a missing resource
/// directory, a sky or map that fails to load.
class ISOLA_API Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

} // namespace isola
