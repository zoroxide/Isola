#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

class Terrain;

// User-tweakable vegetation settings
struct FoliageParams {
    // Grass (generated on the GPU around the player every frame)
    bool  grassEnabled = true;
    float grassDensity = 1.0f;     // multiplier on the number of blades
    float grassRadius = 45.0f;     // metres; blades thin out towards this distance
    float grassHeight = 0.5f;      // metres (average blade)
    bool  flowers = true;

    // Trees (placed once per terrain generation)
    bool  treesEnabled = true;
    float treeDensity = 0.55f;     // 0..1, how much of the grassland becomes forest
    float treeDistance = 1200.0f;  // draw distance in metres
    float treeDetailDistance = 90.0f; // beyond this, trees are drawn as billboards (impostors)

    // Wind (direction follows the ocean wind)
    float windStrength = 0.6f;
};

struct FoliagePrograms {
    GLuint grass = 0, tree = 0, treeBake = 0, impostor = 0;
};

// Grass blades + instanced procedural trees (fir, broadleaf, acacia) that sway in the wind.
// Distant trees are drawn as pre-rendered billboards (impostors).
class Foliage {
public:
    Foliage() = default;
    ~Foliage();

    // Create meshes, load the bark / leaf-card textures (dir: assets/textures/foliage) and bake impostors
    bool init(const std::string& textureDir, const FoliagePrograms& programs);

    // Scatter trees over the current terrain (call after every terrain generation)
    void generate(const Terrain& terrain, const FoliageParams& params, int seed);
    // Village: no wild trees inside the circle (x, z, radius); planted trees (x, y, z, species) instead
    void setClearing(glm::vec3 area) { clearing_ = area; }
    void setPlantedTrees(const std::vector<glm::vec4>& trees) { planted_ = trees; }
    GLuint textureArray() const { return texArray_; }
    void setPrograms(const FoliagePrograms& p) { prog_ = p; }   // after a shader reload

    // Trees and grass; call after the terrain, before transparent objects
    void draw(const Terrain& terrain, const FoliageParams& params, const glm::mat4& view,
              const glm::mat4& proj, const glm::vec3& cameraPos, float time);

    // Keep the player out of tree trunks (pos = eye position; only x/z are changed)
    void resolveCollision(glm::vec3& pos, float playerRadius = 0.35f) const;

    // Soft shade on the ground under tree canopies (terrain / grass shaders)
    void bindCanopyShade(GLuint prog, int unit, float terrainHalf) const;
    // Tree crown top heights (same layout as the terrain height map) for sun shadows
    GLuint canopyHeightTexture() const { return trees_.empty() ? 0 : canopyHeightTex_; }

    // x, z, canopy radius of every tree (for the minimap)
    const std::vector<glm::vec3>& treeDots() const { return dots_; }
    int treeCount() const { return (int)trees_.size(); }
    int lastDrawnMeshTrees() const { return drawnMesh_; }
    int lastDrawnImpostors() const { return drawnImpostors_; }

private:
    glm::vec3 clearing_{0}; // center X/Z and radius
    std::vector<glm::vec4> planted_;
    struct TreeVertex { glm::vec3 p, n; glm::vec2 uv; float layer, bend, flutter, ao; };
    struct Mesh { GLuint vao = 0, vbo = 0, ebo = 0; GLsizei count = 0; float height = 10.0f, radius = 3.0f, tile = 10.0f; };
    struct Tree { glm::vec3 pos; float yaw, scale, phase, tint; int mesh; };

    void buildMesh(Mesh& m, const std::vector<TreeVertex>& v, const std::vector<unsigned>& idx);
    void bakeImpostors();
    void drawTrees(const Terrain& terrain, const FoliageParams& params, const glm::mat4& viewProj,
                   const glm::vec3& cameraPos, float time);
    void drawGrass(const Terrain& terrain, const FoliageParams& params, const glm::mat4& viewProj,
                   const glm::vec3& cameraPos, float time);

    static const int kSpecies = 3, kVariants = 3, kMeshes = kSpecies * kVariants;
    static const int kViews = 8, kTile = 256;          // impostor views per tree, pixels per view
    static const int kBladesPerClump = 6, kBladeVerts = 7;
    Mesh meshes_[kMeshes];
    std::vector<Tree> trees_;
    std::vector<glm::vec3> dots_;
    float trunkRadius_[kSpecies] = { 0.3f, 0.32f, 0.24f };
    int drawnMesh_ = 0, drawnImpostors_ = 0;

    // Trunk collision grid
    float cellSize_ = 8.0f, gridHalf_ = 0.0f;
    int gridN_ = 0;
    std::vector<std::vector<int>> grid_;

    FoliagePrograms prog_;
    GLuint texArray_ = 0;       // 0 bark, 1 conifer branch, 2 broadleaf cluster, 3 acacia cluster
    GLuint instanceVBO_ = 0;
    GLuint canopyTex_ = 0, canopyHeightTex_ = 0;
    // Impostors: atlas of kViews columns x kMeshes rows (albedo+alpha, and tree-space normal)
    GLuint impAlbedo_ = 0, impNormal_ = 0, impVAO_ = 0, impQuadVBO_ = 0, impInstVBO_ = 0;
    // Grass: attribute-less clumps drawn per visible tile
    GLuint grassVAO_ = 0, grassEBO_ = 0, grassFarVAO_ = 0, grassFarEBO_ = 0;
};
