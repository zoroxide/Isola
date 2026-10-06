#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

class Terrain;

// A light placed by the village: lamps, lanterns, fireplaces
struct VillageLight {
    glm::vec3 pos, color;
    float radius;
    bool outdoor;    // street lamps / lanterns also light the terrain, grass and trees
    float flicker;   // 0 steady; > 0 fire-like flicker amount
    int house = -1;  // interior lights belong to a house (only that house is lit by them)
};

// A procedurally planned village: streets winding out from a plaza, houses with furnished interiors,
// a fountain, market stalls and street lamps. Everything is built in code from the textures in
// assets/textures/village, combined into a few static meshes (one draw per visible house).
class Village {
public:
    ~Village();
    // Load the material textures (call once, after the GL context exists)
    bool init(const std::string& textureDir);
    // Pick a site, shape the terrain (plaza, streets, house terraces) and build all geometry
    void generate(Terrain& terrain, int seed);
    void clear();
    bool active() const { return !houses_.empty(); }

    // --- Rendering ---
    // lightScale/time: intensity and flicker of the interior lights
    // depthProgram: depth-only pre-pass (so the expensive shading runs once per pixel)
    void draw(GLuint program, GLuint depthProgram, const glm::mat4& view, const glm::mat4& proj, GLuint foliageTex,
              float lightScale, float time) const;
    void drawGlass(GLuint program, const glm::mat4& view, const glm::mat4& proj) const;   // after opaque
    // Directional depth map of the village for the sun (rebuilt only when the sun moves)
    void buildShadow(GLuint program, const glm::vec3& sunDir);
    void bindShadow(GLuint program, int unit, float strength, bool enabled) const;
    // Ground mask (paving / no grass) for the terrain and grass shaders
    void bindMask(GLuint program, int unit) const;

    // --- Gameplay ---
    // Highest walkable village surface (floors, stairs, steps) at x/z that is no more than a step
    // above the feet; -1e9 when there is none
    float groundAt(float x, float z, float feetY) const;
    void collide(glm::vec3& eye) const;   // push the player (eye position) out of walls and furniture

    // --- Info for other systems ---
    const std::vector<VillageLight>& lights() const { return lights_; }
    glm::vec3 center() const { return center_; }
    float radius() const { return radius_; }
    glm::vec3 spawn() const { return spawn_; }          // eye position at the end of the main street
    float spawnYaw() const { return spawnYaw_; }        // looking up the street towards the plaza
    glm::vec3 clearing() const { return active() ? glm::vec3(center_.x, center_.z, radius_ + 6.0f) : glm::vec3(0); }
    const std::vector<glm::vec4>& plantedTrees() const { return planted_; }   // x, y, z, species
    struct MapHouse { glm::vec2 center, half, axisX; glm::vec3 roof; };
    const std::vector<MapHouse>& mapHouses() const { return mapHouses_; }
    const std::vector<std::vector<glm::vec2>>& streets() const { return streets_; }
    float plazaRadius() const { return plazaR_; }
    int houseCount() const { return (int)houses_.size(); }
    int lastDrawnHouses() const { return drawnHouses_; }
    // Quality settings
    void setShadowResolution(int res);
    void setMaxHouseLights(int n) { maxHouseLights_ = n; }
    GLuint albedoTexture() const { return albedo_; }
    GLuint normalTexture() const { return normal_; }
    // For tests: a point standing in an open doorway, and one inside a wall
    glm::vec3 testDoorway() const { return testDoor_; }
    glm::vec3 testWall() const { return testWall_; }

private:
    struct House {
        glm::vec2 c, ex, ez;        // centre and local axes on the ground (ez points away from the street)
        float w, d, padY;           // width (local x), depth (local z), terrace height
        int storeys;
        float doorX;                // door centre along the front wall
        glm::vec3 wall, accent, roof, trim, interior, fabric;
        bool fireplace, balcony, canopy;
        unsigned seed;
    };
    struct Collider { glm::vec2 c, ax, half; float y0, y1, radius; };   // radius > 0: vertical cylinder
    struct Walkable { glm::vec2 c, ax, half; float h0, h1; };          // ramp from h0 (local -x) to h1 (+x)
    struct Range { GLint first; GLsizei count; glm::vec3 center; float radius; int house; };
    struct Plaza { float y; };

    // Planning
    bool chooseSite(const Terrain& t);
    void planStreets(const Terrain& t);
    void planHouses(const Terrain& t);
    void shapeTerrain(Terrain& t);
    void buildMask();
    // Geometry
    void buildAll(const Terrain& t);
    void upload();

    friend class VillageBuilder;

    // Plan
    unsigned seed_ = 1;
    glm::vec3 center_{0};
    float radius_ = 60.0f, plazaR_ = 11.0f, streetHalf_ = 2.1f;
    std::vector<std::vector<glm::vec2>> streets_;
    std::vector<std::vector<float>> streetY_;
    std::vector<House> houses_;
    std::vector<MapHouse> mapHouses_;
    std::vector<glm::vec4> planted_;
    glm::vec3 spawn_{0};
    float spawnYaw_ = 0.0f;

    // Gameplay data (bucketed for fast queries)
    std::vector<Collider> colliders_;
    std::vector<Walkable> walkables_;
    std::vector<VillageLight> lights_;
    float gridMinX_ = 0, gridMinZ_ = 0, gridCell_ = 6.0f;
    int gridW_ = 0, gridH_ = 0;
    std::vector<std::vector<int>> colGrid_, walkGrid_;
    void buildGrids();
    glm::vec3 testDoor_{0}, testWall_{0};

    // GPU
    GLuint albedo_ = 0, normal_ = 0;
    // 0 opaque, 1 alpha-tested cards (flowers, fire), 2 glass. Indexed, packed 36-byte vertices;
    // the opaque mesh also has a position-only copy for the depth pre-pass and the shadow map.
    GLuint vao_[3] = {}, vbo_[3] = {}, ebo_[3] = {};
    GLuint posVao_ = 0, posVbo_ = 0;
    GLsizei counts_[3] = {};               // index counts
    std::vector<Range> ranges_;            // per house / prop group: index ranges in the opaque mesh
    GLuint maskTex_ = 0;
    glm::vec4 maskRect_{0};
    GLuint shadowFbo_ = 0, shadowTex_ = 0;
    glm::mat4 shadowMatrix_{1.0f};
    glm::vec3 shadowSun_{0.0f};
    bool shadowBuilt_ = false;
    int shadowRes_ = 2048;
    int maxHouseLights_ = 4;
    mutable int drawnHouses_ = 0;
};
