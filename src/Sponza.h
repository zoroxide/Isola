#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "TriangleBvh.h"

namespace nut::detail {

// The Sponza palace map (Crytek Sponza, glTF from the Khronos sample assets in assets/maps/sponza).
// Loaded on first use. Renders the PBR materials (base colour, normal, metal/roughness, alpha-masked
// plants and chains) with a sun shadow map and baked indirect light: a 3D grid over the building
// stores, as L1 spherical harmonics, how much sky each point sees and how much sunlight bounces
// off the lit walls and floor towards it. The bounce is re-baked on a worker thread when the sun moves.
class Sponza {
public:
    ~Sponza() { clear(); }
    // Free the GPU resources (call while the GL context still exists)
    void clear();
    // maxTextureSize: textures are box-filtered down to this size (memory on low-end GPUs)
    bool load(const std::string& gltfPath, int maxTextureSize, float anisotropy);
    bool loaded() const { return vao_ != 0; }

    // --- Rendering ---
    // Opaque and alpha-tested geometry. indirect: strength of the baked sun bounce light
    void draw(GLuint program, const glm::mat4& view, const glm::mat4& proj, float indirect) const;
    // Sun depth map fitted around the building (rebuilt only when the sun moves)
    void buildShadow(GLuint program, const glm::vec3& sunDir);
    // Binds the shadow map as the "village" shadow of common.glsl (same uniforms, same filtering)
    void bindShadow(GLuint program, int unit, float strength, bool enabled) const;
    void setShadowResolution(int res);
    // Start a bounce-light bake when the sun has moved, and upload a finished one (call every frame).
    // The very first bake runs synchronously so the map never shows without indirect light.
    void updateIndirect(const glm::vec3& sunDir);
    bool bakingIndirect() const { return baking_; }
    // Baked light reaching point p, averaged over all directions: (sky visibility, bounce colour).
    // Cheap CPU lookup, used for the camera's eye adaptation.
    glm::vec4 lightAt(const glm::vec3& p) const;
    void setAnisotropy(float amount);

    // --- Gameplay ---
    // Highest walkable surface under x/z that is at most a step above the feet; -1e9 when there is none
    float groundAt(float x, float z, float feetY) const;
    // Push the player (eye position, eyeHeight above the feet) out of walls, columns and props
    void collide(glm::vec3& eye, float eyeHeight) const;
    // Distance from the eye up to a ceiling (for jumps), large when there is none
    float headroom(const glm::vec3& eye) const;
    glm::vec3 spawn() const { return spawn_; }
    float spawnYaw() const { return spawnYaw_; }
    glm::vec3 boundsMin() const { return bmin_; }
    glm::vec3 boundsMax() const { return bmax_; }

    // --- Info ---
    int triangleCount() const { return bvh_.triangleCount(); }
    int materialCount() const { return (int)materials_.size(); }
    int lastDrawCalls() const { return drawCalls_; }

private:
    struct Material {
        GLuint albedo = 0, normal = 0, metalRough = 0;
        glm::vec4 baseColor{1.0f};
        float metallic = 1.0f, roughness = 1.0f;
        bool mask = false;
        float cutoff = 0.5f;
        glm::vec3 average{0.5f};   // mean albedo, for the light bake
    };
    struct Part { GLint firstIndex; GLsizei count; int material; glm::vec3 center; float radius; };

    // Light volume (CPU result of a bake): per cell L1 SH as (b.xyz, a) for sky visibility and bounce r, g, b
    struct Bake {
        glm::vec3 sun{0.0f};
        std::vector<glm::vec4> sky, r, g, b;
    };
    // One light bounce more than `prev` (nullptr: sky + direct sun bounce only)
    void bake(const glm::vec3& sunDir, const Bake* prev, Bake& out, int threads) const;
    glm::vec4 sample(const std::vector<glm::vec4>& vol, const glm::vec3& p) const;   // trilinear
    void uploadBake(const Bake& b);
    GLuint loadTexture(const std::string& path, int kind, int maxSize, float anisotropy, glm::vec3* average);

    std::vector<Material> materials_;
    std::vector<Part> parts_;   // sorted: opaque materials first, then alpha-tested
    std::vector<std::pair<std::string, GLuint>> textureCache_;
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0;
    GLuint white_ = 0, flatNormal_ = 0;
    mutable int drawCalls_ = 0;

    TriangleBvh bvh_;
    glm::vec3 bmin_{0}, bmax_{0};
    glm::vec3 spawn_{0};
    float spawnYaw_ = 180.0f;

    // Shadow map
    GLuint shadowFbo_ = 0, shadowTex_ = 0;
    int shadowRes_ = 2048;
    glm::mat4 shadowMatrix_{1.0f};
    glm::vec3 shadowSun_{0.0f};
    bool shadowBuilt_ = false;

    // Indirect light volume
    glm::ivec3 volRes_{0};
    glm::vec3 volMin_{0}, volCell_{0.5f};
    GLuint volTex_[4] = {};
    bool volReady_ = false;
    glm::vec3 bakedSun_{0.0f};
    std::thread worker_;
    std::atomic<bool> baking_{false}, bakeDone_{false};
    std::unique_ptr<Bake> pending_;
    int settleBakes_ = 0;   // extra bakes after the sun stops (each adds a bounce)
    Bake current_;   // CPU copy of the uploaded volume (next bounce, eye adaptation)
};

} // namespace nut::detail
