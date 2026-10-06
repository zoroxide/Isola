#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

// What we learn about a panorama when it is loaded (used to light the scene to match the sky)
struct SkyAnalysis {
    bool hasSun = false;
    glm::vec3 sunDir{0.0f, 1.0f, 0.0f}; // direction *towards* the sun, in panorama space
    glm::vec3 sunColor{1.0f};           // normalized tint (max component = 1)
    float sunIntensity = 1.0f;          // 0..1, lower for dim suns / moonlight
    float autoExposure = 1.0f;          // exposure that brings the sky to a pleasant brightness
};

class Skybox {
public:
    Skybox() = default;
    ~Skybox();

    bool setCubemap(GLuint texID);
    void setShader(GLuint prog);
    void initFullscreenTriangle();
    void draw(const glm::mat4& invView, const glm::mat4& invProj, bool hasSkybox, float time, bool cloudEnabled, float cloudSpeed, float cloudScale, float cloudOpacity);

    // High-level loading. 'path' is either:
    //  - a directory containing right/left/top/bottom/front/back images (.png/.jpg/.bmp/.hdr), or
    //  - a single equirectangular panorama (.hdr for real HDR lighting, or .png/.jpg/.bmp)
    // Returns true on success. An empty path unloads the panorama (procedural sky is used).
    bool loadFromPath(const std::string& path);
    bool hasCubemap() const { return cubemap_ != 0; }
    GLuint cubemap() const { return cubemap_; }

    // Bind the sky to a texture unit and set the matching uniforms on another program,
    // so it can use the sky for fog colour, ambient light and reflections.
    void bindForLighting(GLuint prog, int unit) const;

    // Light that matches the sky (world space). Falls back to a default sun.
    glm::vec3 lightDirection() const;  // direction light travels (towards the ground)
    glm::vec3 lightColor() const;

    // --- Runtime settings ---
    // Manual sun (used instead of the one detected in the panorama when enabled)
    bool sunOverride = false;
    glm::vec3 overrideLightDir{0.0f, -1.0f, 0.0f};   // direction light travels
    glm::vec3 overrideLightColor{1.0f};

    float exposure = 1.0f;
    float rotationDeg = 0.0f;  // spins the panorama around the vertical axis
    float blur = 0.0f;         // mip bias for a softer, out-of-focus sky
    bool horizonFill = true;   // replace the panorama's floor with the horizon/fog colour
    glm::vec3 fogColor{0.66f, 0.78f, 0.90f}; // horizon colour of the procedural sky
    const SkyAnalysis& analysis() const { return analysis_; }
    bool isHDR() const { return isHDR_; }
    glm::mat3 rotation() const; // world direction -> panorama direction

    // --- CPU helpers (no GL needed; exposed for tools/tests) ---
    // Convert a linear RGB float equirectangular image into 6 cube faces (+X,-X,+Y,-Y,+Z,-Z).
    static void equirectToFaces(const float* img, int w, int h, int faceSize, std::vector<float> faces[6]);
    static SkyAnalysis analyzeEquirect(const float* img, int w, int h, bool hdr);

private:
    GLuint skyShader_ = 0;
    GLuint skyVAO_ = 0;
    GLuint skyVBO_ = 0;
    GLuint cubemap_ = 0;
    int mipLevels_ = 1;
    bool isHDR_ = false;
    SkyAnalysis analysis_;
};
