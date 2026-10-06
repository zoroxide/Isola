#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>

class Terrain;

// Island-wide sun shadows. A GPU pass marches from every texel towards the sun over the terrain
// height map (plus tree crowns) and stores the height above which that spot sees the sun.
// Shaders then get soft shadows with one texture fetch (see sunShadow() in common.glsl).
// Rebuilt only when the sun moves or the world changes.
class SunShadow {
public:
    ~SunShadow();
    bool init(GLuint program, int resolution = 1024);
    // sunDir: direction towards the sun. canopyHeightTex: optional tree-crown heights (same layout as the terrain height map)
    void build(const Terrain& terrain, GLuint canopyHeightTex, const glm::vec3& sunDir);
    // True when the sun direction changed enough since the last build
    bool needsRebuild(const glm::vec3& sunDir) const;
    void bind(GLuint prog, int unit, float halfExtent, float strength, bool enabled) const;
    void invalidate() { built_ = false; }
    void setProgram(GLuint p) { prog_ = p; }

private:
    GLuint prog_ = 0, fbo_ = 0, tex_ = 0, vao_ = 0;
    int res_ = 1024;
    bool built_ = false;
    glm::vec3 lastSun_{0.0f};
};
