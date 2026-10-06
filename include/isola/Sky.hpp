#pragma once
#include <glm/vec3.hpp>

namespace isola {

/// A manually placed sun (used instead of the sun found in an HDR sky).
struct SunSettings {
    float elevation = 40.0f;   ///< degrees above the horizon
    float azimuth = 35.0f;     ///< degrees, compass direction of the sun
    float intensity = 1.0f;
    glm::vec3 tint{1.0f, 0.96f, 0.88f};
};

/// Look of the sky itself.
struct SkySettings {
    float exposure = 1.0f;      ///< brightness of an HDR panorama
    float rotation = 0.0f;      ///< degrees around the vertical axis
    float blur = 0.0f;          ///< 0 sharp .. 6 very soft
    bool clouds = true;         ///< procedural cloud layer (default on only for the procedural sky)
    float cloudSpeed = 0.02f;
    float cloudScale = 1.0f;
    float cloudOpacity = 0.55f;
};

} // namespace isola
