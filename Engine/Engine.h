#pragma once

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <string>
#include <chrono>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <vector>

#include "gui/gui.h"
#include "Camera.h"
#include "Shaders.h"
#include "libs/imgui/imgui.h"
#include "Terrain.h"
#include "Foliage.h"
#include "Graphics.h"
#include "SunShadow.h"
#include "GpuProfile.h"
#include "PostProcess.h"
#include "Skybox.h"
#include "Models.h"
#include "Village.h"
#include "Sponza.h"
#include "Renderer.h"

using Clock = std::chrono::high_resolution_clock;

class Engine {
public:
    Engine();
    ~Engine();

    // Initialize the engine and create a window. Returns true on success.
    // If fullscreen is true, a fullscreen window is created.
    bool init(bool fullscreen = true);

    // Load terrain texture and optional OBJ model from paths.
    void load_terrain_using_texture(const std::string &texturePath, const std::string &objPath = "");
    // Load and prepare a simple flat terrain (textured quad). Returns true on success.
    bool load_flat_terrain(const std::string &texturePath);

    // Enable or disable VSync (must be called after init or will be applied on next init)
    void vsync(bool enabled);
    bool getVsyncEnabled() const;

    // Enter the main loop and run until window close.
    void mainloop();

private:
    // Internal state (opaque to users)
    GLFWwindow* window_;
    GLuint shaderProgram_;
    GLuint waterShader_ = 0;
    GLuint grassShader_ = 0, treeShader_ = 0;
    GLuint terrainShader_ = 0;           // chunked LOD terrain
    GLuint impostorShader_ = 0, treeBakeShader_ = 0, sunShadowShader_ = 0;
    SunShadow sunShadow_;
    PostProcess post_;
    GpuTimers gpuTimers_;
    void setupSamplerUnits();
    double lastShadowBuild_ = -1.0;
    GLuint noiseTex_ = 0, waterDetailTex_ = 0;
    GraphicsSettings graphics_;

    // Sky renderer
    GLuint skyShader_;
    Skybox sky_;

    // Camera / movement
    Camera camera_;
    glm::vec3 cameraPos_;
    float yaw_, pitch_;
    float mouseSensitivity_;
    float moveSpeed_;

    // Mouse
    double lastX_, lastY_;
    bool firstMouse_;

    // Timing
    Clock::time_point lastFrame_;
    float deltaTime_;

    // constants
    #define TERRAIN_SIZE 512
    #define TERRAIN_SCALE 1.0f
    #define HEIGHT_SCALE 6.0f
    #define TEXTURE_TILE 22.0f

    // #define NOISE_SCALE 0.1f
    // #define NOISE_OCTAVES 6
    // #define NOISE_PERSISTENCE 0.5f
    // #define NOISE_LACUNARITY 2.0f

    #define JUMP_VELOCITY 7.0f

    // #define GRAVITY 18.0f

    #define SPRINT_MULTIPLIER 1.9f

    // Input
    bool keys_[1024];
    bool jumping_;
    float jumpVel_;

    // VSync state
    bool vsyncEnabled_;

    // for locking/unlocking cursor
    bool cursorEnabled_ = false;


    // Instance pointer for static callbacks
    static Engine* s_instance_;

    // GUI manager
    GUI* gui_;
    ShaderManager shaders_;

    // Subsystems
    Terrain terrain_;
    Foliage foliage_;
    FoliageParams foliageParams_;
    Models models_;
    Village village_;
    GLuint villageShader_ = 0;
    GLuint villageShadowShader_ = 0;
    bool villageLamps_ = true;
    // Maps: the island presets, then Sponza (loaded on first use)
    int map_ = 0, pendingMap_ = -1;
    Sponza sponza_;
    GLuint sponzaShader_ = 0, sponzaShadowShader_ = 0;
    float sponzaIndirect_ = 1.0f;
    bool sponzaAutoExposure_ = true;
    float sponzaExposure_ = 1.0f;
    bool snapExposure_ = true;   // jump straight to the target exposure (after teleporting)
    void updateSponzaMovement(float dt);
    void renderSponza(const glm::mat4& view, const glm::mat4& proj, const glm::mat4& invView, const glm::mat4& invProj,
                      const glm::mat4& VP, const glm::vec3& lightDir, const glm::vec3& lightCol, const glm::vec3& uwColor,
                      GLuint outputFbo, int outW, int outH);
    // GPU profile / auto quality
    GpuInfo gpu_;
    bool needBenchmark_ = false, tierFromCache_ = false;
    int benchTier_ = 3;                     // highest tier the benchmark allowed
    float overBudgetTime_ = 0.0f, underBudgetTime_ = 0.0f;
    std::string tierReason_;
    std::vector<BenchmarkSample> benchSamples_;
    void runGpuBenchmark();
    void refreshPrograms();
    void applyTextureQuality();
    void updateQualityGovernor(float dt);
    void writeGpuReport();
    float lampIntensity_ = 1.0f;
    // Nearest point lights this frame (outdoor ones first), uploaded to every scene shader
    int numPointLights_ = 0, numOutdoorLights_ = 0;
    float daylight_ = 1.0f;
    glm::vec4 pointLightPos_[16], pointLightColor_[16];
    void gatherLights(const glm::vec3& sunColor);
    Renderer renderer_;

    // Configurable constants (moved from macros to members so we can change them at runtime)
    int terrainSize_;
    float terrainScale_;
    float heightScale_;
    float textureTile_;
    TerrainParams terrainParams_;

    // Last-used file paths (for UI / serialization)
    std::string panoramaPath_;
    std::string terrainTexturePath_;
    
    // Cloud layer settings
    bool cloudEnabled_;
    float cloudSpeed_;
    float cloudScale_;
    float cloudOpacity_;
    bool sunFromSky_ = true;
    bool guiVisible_ = false;
    float sunElevation_ = 40.0f, sunAzimuth_ = 35.0f, sunIntensity_ = 1.0f;
    glm::vec3 sunTint_{1.0f, 0.96f, 0.88f};

    // Swimming state
    bool swimming_ = false;
    bool underwater_ = false;
    float oxygen_ = 1.0f;        // 0..1, drains while the head is under water
    float swimTime_ = 0.0f;      // running time used for the waves
    void setPerFrameUniforms(GLuint prog, const glm::vec3& lightDir, const glm::vec3& lightCol,
                             const glm::vec3& uwColor);
    bool fogFromSky_ = true;


public: // Public API
    // Load a skybox cubemap. 'path' is either:
    //  - a directory containing right/left/top/bottom/front/back images (.png or .bmp), or
    //  - a single equirectangular image (.png or .bmp)
    // Returns true on success; if path is empty, disables skybox (fallback gradient).
    bool panorama(const std::string &path);

    // Regenerate terrain mesh with current constants
    void regenerateTerrain();
    void placePlayerOnLand();

    // Getters / setters for configurable constants and file paths
    int getTerrainSize() const;
    void setTerrainSize(int v);
    float getTerrainScale() const;
    void setTerrainScale(float v);
    float getHeightScale() const;
    void setHeightScale(float v);
    float getTextureTile() const;
    void setTextureTile(float v);

    // Full procedural-generation settings (edited live by the GUI)
    TerrainParams& terrainParams() { return terrainParams_; }
    float getWaterY() const { return terrain_.getWaterY(); }

    // Sky / panorama settings (exposure, rotation, blur live on the Skybox)
    Skybox& sky() { return sky_; }
    bool& sunFromSky() { return sunFromSky_; }
    bool& fogFromSky() { return fogFromSky_; }
    // Manual sun (when "sun from sky" is off): elevation / azimuth in degrees, intensity
    float& sunElevation() { return sunElevation_; }
    float& sunAzimuth() { return sunAzimuth_; }
    float& sunIntensity() { return sunIntensity_; }
    glm::vec3& sunTint() { return sunTint_; }

    // Rendering quality / post-processing
    GraphicsSettings& graphics() { return graphics_; }
    // GPU-aware quality: tier per GPU (benchmarked once, cached in graphics.cfg), adjusted at runtime
    const GpuInfo& gpu() const { return gpu_; }
    void applyTier(int tier, const char* reason = nullptr);
    void requestBenchmark() { needBenchmark_ = true; }
    const std::string& lastQualityChange() const { return tierReason_; }
    int benchmarkTier() const { return benchTier_; }
    const GpuTimers& gpuTimers() const { return gpuTimers_; }
    float renderScale() const { return post_.scale(); }
    int terrainTriangles() const { return terrain_.lastDrawnTriangles(); }
    int treesDrawn() const { return foliage_.lastDrawnMeshTrees(); }
    int impostorsDrawn() const { return foliage_.lastDrawnImpostors(); }

    // Render one frame of the 3D world (with post-processing) into outputFbo (0 = window)
    void renderFrame(GLuint outputFbo, int outW, int outH);

    // Grass & trees
    FoliageParams& foliageParams() { return foliageParams_; }
    void replantTrees();
    int getTreeCount() const { return foliage_.treeCount(); }

    // Settings panel (Tab) visibility; the HUD and minimap are always drawn
    bool isGuiVisible() const { return guiVisible_; }
    void setGuiVisible(bool v);

    // Maps: every island terrain preset, then the Sponza palace
    static int mapCount();
    static const char* mapName(int map);
    int currentMap() const { return map_; }
    bool inSponza() const;
    // Switch map (an island preset regenerates the island; Sponza loads on first use). False on failure.
    bool selectMap(int map);
    // Switch on the next frame, with a loading message (for the GUI)
    void requestMap(int map) { pendingMap_ = map; }
    const Sponza& sponza() const { return sponza_; }
    float& sponzaIndirect() { return sponzaIndirect_; }
    bool& sponzaAutoExposure() { return sponzaAutoExposure_; }
    float sponzaExposure() const { return sponzaExposure_; }
    void teleportToSpawn();

    // Player / minimap
    const Terrain& terrain() const { return terrain_; }
    const Village& village() const { return village_; }
    // Village lights (lamps, lanterns, fires) and the player's standing height (terrain or floors)
    bool& villageLamps() { return villageLamps_; }
    float& lampIntensity() { return lampIntensity_; }
    void regenerateVillage();
    void teleportToVillage();
    float groundHeight(float x, float z, float feetY) const;
    const glm::vec3& getPlayerPos() const { return cameraPos_; }
    float getYaw() const { return yaw_; }

    // Player / swimming status for the HUD
    bool isSwimming() const { return swimming_; }
    bool isUnderwater() const { return underwater_; }
    float getOxygen() const { return oxygen_; }

    // File path accessors
    const std::string& getPanoramaPath() const;
    void setPanoramaPath(const std::string &p);
    const std::string& getTerrainTexturePath() const;
    void setTerrainTexturePath(const std::string &p);

    // Cloud accessors
    bool getCloudEnabled() const;
    void setCloudEnabled(bool v);
    float getCloudSpeed() const;
    void setCloudSpeed(float v);
    float getCloudScale() const;
    void setCloudScale(float v);
    float getCloudOpacity() const;
    void setCloudOpacity(float v);

    // Internal helpers: none (delegated to subsystems)

    // Input helpers
    static void cursorPosCallbackStatic(GLFWwindow* , double xpos, double ypos);
    static void keyCallbackStatic(GLFWwindow* , int key, int scancode, int action, int mods);
    void cursorPosCallback(double xpos, double ypos);
    void keyCallback(int key, int scancode, int action, int mods);
    void updateMovement(float dt);

    // Legacy model data removed; models handled by Models module
    bool insideHouse_ = false;

    // API to add a house model
public:
    void add_house(const std::string& objPath, const glm::vec3& position, const glm::vec3& scale = glm::vec3(1.0f));

private:
    // Flat terrain state handled by Terrain subsystem
    bool hasFlat_ = false;
};
