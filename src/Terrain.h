#pragma once
#include <glm/glm.hpp>
#include <GL/glew.h>
#include <string>
#include <vector>
#include <functional>

// All user-tweakable settings for procedural terrain generation and shading.
struct TerrainParams {
    // --- Shape ---
    int   seed = 1337;
    float frequency = 0.0035f;   // base noise frequency (per vertex)
    int   octaves = 7;
    float persistence = 0.5f;    // amplitude falloff per octave
    float lacunarity = 2.0f;     // frequency gain per octave
    float ridgeAmount = 0.65f;   // 0 = smooth rolling hills, 1 = sharp mountain ridges
    float warpStrength = 70.0f;  // domain warping (organic, twisting shapes)
    float heightPower = 1.5f;    // >1 flattens valleys and sharpens peaks
    float terraceStrength = 0.0f;// 0..1 blend towards stepped (mesa) terrain
    int   terraceSteps = 8;

    // --- Island ---
    float islandStrength = 1.0f; // 0 = land runs off the map edges, 1 = one island surrounded by ocean
    float islandRadius = 0.46f;  // size of the island (0..1 of the map); the coast is a bit further out
    float coastNoise = 0.8f;     // how irregular the coastline is (bays, peninsulas)
    float seaDepth = 0.22f;      // ocean floor depth below sea level (fraction of heightScale)
    bool  fillLakes = true;      // raise any inland basin below sea level so all water is ocean

    // --- Erosion ---
    int   erosionIterations = 150000; // hydraulic erosion droplets (0 = off)
    float erosionStrength = 0.3f;
    float depositStrength = 0.3f;
    int   thermalIterations = 6;     // talus smoothing passes (0 = off)

    // --- Ocean ---
    bool  waterEnabled = true;
    float waterLevel = 0.20f;    // fraction of heightScale
    float waterOpacity = 0.85f;
    glm::vec3 waterShallow{0.10f, 0.55f, 0.60f};
    glm::vec3 waterDeep{0.02f, 0.13f, 0.28f};
    float waveHeight = 0.9f;     // metres, crest to trough of the biggest swell
    float waveLength = 38.0f;    // metres, of the biggest swell
    float choppiness = 0.7f;     // 0 = round swells, 1 = sharp crests
    float windAngle = 35.0f;     // degrees, direction the waves travel
    float waveSpeed = 1.0f;

    // --- Surface materials (heights are fractions of heightScale) ---
    float beachWidth = 0.02f;
    float rockSlope = 0.38f;     // slope (1 - normal.y) where rock takes over
    float snowLine = 0.78f;
    float snowBlend = 0.08f;
    float textureScale = 1.0f;   // world-space size multiplier of the ground textures
    glm::vec3 grassTint{0.78f, 1.0f, 0.62f};   // the meadow texture is olive; pull it towards green
    glm::vec3 sandTint{1.0f, 1.0f, 1.0f};
    glm::vec3 rockTint{1.0f, 1.0f, 1.0f};
    glm::vec3 snowTint{1.0f, 1.0f, 1.0f};

    // --- Atmosphere ---
    float fogDensity = 0.0012f;  // 0 disables fog
    glm::vec3 fogColor{0.66f, 0.78f, 0.90f};

    // Preset ids: see presetNames()
    static TerrainParams preset(int id);
    static const char* const* presetNames(int& count);
};

// Simple Terrain facade to encapsulate procedural and flat terrain
class Terrain {
public:
    Terrain() = default;
    ~Terrain();

    // Build a procedural mesh using provided buffers (interleaved pos/normal/uv)
    // Ownership of VAO/VBO/EBO stays in Terrain.
    void buildProcedural(const std::vector<float>& interleaved, const std::vector<unsigned int>& indices);

    // Build a flat quad terrain and load its texture
    bool buildFlat(const std::string& texturePath);

    // Load a 2D texture to use for procedural terrain
    bool loadProceduralTexture(const std::string& texturePath);

    // High-level: generate a procedural terrain mesh using internal noise
    void generateProcedural(int size, float scale, float heightScale, float textureTile);
    void generateProcedural(int size, float scale, float heightScale, float textureTile, const TerrainParams& params);

    // Load the ground material set (grass/grass2/rock/sand/snow _albedo/_normal .jpg|.png)
    bool loadMaterials(const std::string& dir);

    // Modify heights inside a world-space rectangle: fn(x, z, oldHeight) -> newHeight.
    // Updates the height texture and LOD chunk bounds (call buildMinimap afterwards).
    void editHeights(glm::vec2 wmin, glm::vec2 wmax, const std::function<float(float, float, float)>& fn);

    // Query ground height at world x,z (sea floor included)
    float getHeightAt(float wx, float wz) const;

    // World-space Y of the calm water surface (or -1e9 when water is disabled)
    float getWaterY() const { return params_.waterEnabled ? waterY_ : -1e9f; }
    // Height of the ocean surface including waves at time t (matches the water shader)
    float getWaterSurfaceAt(float wx, float wz, float t) const;
    // Half the terrain width in world units
    float getHalfExtent() const { return (size_ - 1) * 0.5f * scale_; }
    float getHeightScale() const { return heightScale_; }
    GLuint heightTexture() const { return heightTex_; }

    // (Re)build the minimap; optional tree dots are (x, z, canopy radius) in world units
    void buildMinimap(int res, const std::vector<glm::vec3>* trees = nullptr);
    const TerrainParams& params() const { return params_; }
    // Apply settings that don't need regeneration (waves, materials, fog). Shape settings
    // (incl. sea level) only take effect on the next generateProcedural().
    void setLiveParams(const TerrainParams& p) { params_ = p; }

    // Shaded relief map of the island for the HUD minimap (0 when there is none).
    // Texture u runs along world +X, v along world +Z, covering the whole terrain.
    GLuint minimapTexture() const { return isFlat_ ? 0 : minimapTex_; }
    // Ground material texture arrays (for quality settings such as anisotropic filtering)
    GLuint materialAlbedo() const { return matAlbedo_; }
    GLuint materialNormal() const { return matNormal_; }

    // Ocean: drawn after all opaque objects, with its own shader (water_vert/water_frag)
    void drawWater(GLuint waterProgram, const glm::mat4& view, const glm::mat4& proj, const glm::vec3& cameraPos, float time);

    // Chunked LOD settings: distance (metres) at which chunks drop to half resolution
    void setLodDistance(float d) { lodDistance_ = d; }
    int lastDrawnTriangles() const { return drawnTriangles_; }

    // Draw currently configured terrain using given shader and matrices.
    // Procedural terrain needs the terrain_vert/terrain_frag program; the flat plane uses the plain one.
    void draw(GLuint shaderProgram, const glm::mat4& model, const glm::mat4& view, const glm::mat4& proj, const glm::vec3& cameraPos);

    // Configure tiling and scale for flat draw
    void setFlatScale(const glm::vec3& s) { flatScale_ = s; }
         void setTextureTile(float t) { tile_ = t; }
         float getTextureTile() const { return tile_; }

    // State
    bool isFlat() const { return isFlat_; }

private:
    // Procedural
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0;
    unsigned int indexCount_ = 0;
    GLuint procTexture_ = 0; // optional

    // Flat
    GLuint flatVAO_ = 0, flatVBO_ = 0, flatEBO_ = 0;
    GLuint flatTex_ = 0;
    glm::vec3 flatScale_{1.0f,1.0f,1.0f};
    bool isFlat_ = false;

    // Height map (world-space Y per vertex) kept for queries and water shading
    std::vector<float> heights_;
    TerrainParams params_;
    float waterY_ = 0.0f;
    GLuint heightTex_ = 0;
    GLuint waterVAO_ = 0, waterVBO_ = 0, waterEBO_ = 0;
    GLsizei waterIndexCount_ = 0;
    void buildWaterGrid();

    // Ground materials (texture arrays: 0 grass, 1 grass2, 2 rock, 3 sand, 4 snow)
    GLuint matAlbedo_ = 0, matNormal_ = 0;

    GLuint minimapTex_ = 0;

    // Chunked LOD rendering: a small patch mesh per LOD level, instanced per chunk.
    // Heights come from heightTex in the vertex shader, normals are computed per pixel.
    static const int kChunkCells = 64, kLods = 5;
    struct Patch { GLuint vao = 0, vbo = 0, ebo = 0; GLsizei count = 0; };
    struct Chunk { int gx, gz; float minY, maxY; };
    Patch patches_[kLods];
    std::vector<Chunk> chunks_;
    GLuint chunkInstVBO_ = 0;
    float lodDistance_ = 110.0f;
    int drawnTriangles_ = 0;
    void buildPatches();
    void buildChunks();
    void drawChunks(GLuint prog, const glm::mat4& view, const glm::mat4& proj, const glm::vec3& cameraPos);

    // Config for procedural generation
    int size_ = 512;
    float scale_ = 1.0f;
    float heightScale_ = 6.0f;
    float tile_ = 22.0f;
};
