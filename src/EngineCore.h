#pragma once

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <chrono>
#include <exception>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <nut/Config.hpp>
#include <nut/Graphics.hpp>

#include "Camera.h"
#include "Foliage.h"
#include "GpuProfile.h"
#include "PostProcess.h"
#include "Renderer.h"
#include "Shaders.h"
#include "Skybox.h"
#include "Sponza.h"
#include "SunShadow.h"
#include "Terrain.h"
#include "Village.h"

namespace nut::detail {

class GUI;
using Clock = std::chrono::high_resolution_clock;

// The engine's implementation: window, subsystems, player and main loop. nut::Engine is the
// public facade over it; the settings panel (GUI) uses this class directly.
class EngineCore {
public:
    EngineCore();
    ~EngineCore();
    EngineCore(const EngineCore&) = delete;
    EngineCore& operator=(const EngineCore&) = delete;

    // Creates the window and GL context, compiles the shaders and builds the start map.
    // On failure returns false with a message in `error`.
    bool init(const EngineConfig& config, std::string& error);

    // --- Main loop ---
    void run();
    void requestQuit() { quit_ = true; }
    std::function<void(float dt, double time, std::uint64_t frame)> updateHook;
    std::function<void(int key, int action)> keyHook;
    bool isKeyDown(int key) const { return key >= 0 && key < 1024 && keys_[key]; }
    // Render one frame of the 3D world (with post-processing) into outputFbo (0 = window)
    void renderFrame(GLuint outputFbo, int outW, int outH);
    float fps() const { return fps_; }

    // --- Paths ---
    std::string shaderPath(const char* file) const { return (paths_.shaders / file).string(); }
    std::filesystem::path assetPath(const std::filesystem::path& rel) const { return paths_.assets / rel; }
    std::filesystem::path dataPath(const char* file) const { return paths_.data / file; }

    // --- Sky / sun ---
    // Equirect image or cube-face folder; empty path: procedural sky. False if it fails to load.
    bool panorama(const std::string& path);
    const std::string& getPanoramaPath() const { return panoramaPath_; }
    Skybox& sky() { return sky_; }
    bool& sunFromSky() { return sunFromSky_; }
    bool& fogFromSky() { return fogFromSky_; }
    // Manual sun (when "sun from sky" is off): elevation / azimuth in degrees, intensity
    float& sunElevation() { return sunElevation_; }
    float& sunAzimuth() { return sunAzimuth_; }
    float& sunIntensity() { return sunIntensity_; }
    glm::vec3& sunTint() { return sunTint_; }
    // Procedural cloud layer
    bool& cloudEnabled() { return cloudEnabled_; }
    float& cloudSpeed() { return cloudSpeed_; }
    float& cloudScale() { return cloudScale_; }
    float& cloudOpacity() { return cloudOpacity_; }

    // --- Island ---
    TerrainParams& terrainParams() { return terrainParams_; }
    void regenerateTerrain();
    void placePlayerOnLand();
    // Mesh size: vertices per side, metres per vertex, height range, texture tiling
    int& terrainSize() { return terrainSize_; }
    float& terrainScale() { return terrainScale_; }
    float& heightScale() { return heightScale_; }
    float& textureTile() { return textureTile_; }
    FoliageParams& foliageParams() { return foliageParams_; }
    void replantTrees();
    int getTreeCount() const { return foliage_.treeCount(); }
    const Terrain& terrain() const { return terrain_; }
    const Village& village() const { return village_; }
    bool& villageLamps() { return villageLamps_; }
    float& lampIntensity() { return lampIntensity_; }
    void regenerateVillage();
    void teleportToVillage();

    // --- Maps: every island preset, then the Sponza palace ---
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

    // --- Player ---
    void teleportToSpawn();
    void teleport(const glm::vec3& eye, float yaw, float pitch);
    float groundHeight(float x, float z, float feetY) const;
    const glm::vec3& getPlayerPos() const { return cameraPos_; }
    float getYaw() const { return yaw_; }
    float getPitch() const { return pitch_; }
    bool isSwimming() const { return swimming_; }
    bool isUnderwater() const { return underwater_; }
    float getOxygen() const { return oxygen_; }

    // --- Graphics ---
    GraphicsSettings& graphics() { return graphics_; }
    void vsync(bool enabled);
    bool getVsyncEnabled() const { return vsyncEnabled_; }
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

    // --- UI ---
    bool isGuiVisible() const { return guiVisible_; }
    void setGuiVisible(bool v);
    bool hudEnabled() const { return hud_; }

private:
    static constexpr float kJumpVelocity = 7.0f;
    static constexpr float kSprintMultiplier = 1.9f;

    // Input
    static void cursorPosCallbackStatic(GLFWwindow*, double xpos, double ypos);
    static void keyCallbackStatic(GLFWwindow*, int key, int scancode, int action, int mods);
    void cursorPosCallback(double xpos, double ypos);
    void keyCallback(int key, int scancode, int action, int mods);
    void updateMovement(float dt);
    void updateSponzaMovement(float dt);
    static EngineCore* s_instance_;   // for the GLFW callbacks

    // Rendering
    void setupSamplerUnits();
    void setPerFrameUniforms(GLuint prog, const glm::vec3& lightDir, const glm::vec3& lightCol,
                             const glm::vec3& uwColor);
    void gatherLights(const glm::vec3& sunColor);
    void renderSponza(const glm::mat4& view, const glm::mat4& proj, const glm::mat4& invView, const glm::mat4& invProj,
                      const glm::mat4& VP, const glm::vec3& lightDir, const glm::vec3& lightCol, const glm::vec3& uwColor,
                      GLuint outputFbo, int outW, int outH);

    // GPU profile / auto quality
    void runGpuBenchmark();
    void refreshPrograms();
    void applyTextureQuality();
    void updateQualityGovernor(float dt);
    void writeGpuReport();

    // Configuration
    ResourcePaths paths_;
    bool quitOnEscape_ = true, settingsPanel_ = true, hud_ = true;

    // Window / loop
    GLFWwindow* window_ = nullptr;
    bool vsyncEnabled_ = true;
    bool quit_ = false;
    std::exception_ptr callbackError_;   // thrown by a key callback, rethrown by run()
    bool cursorEnabled_ = false;
    Clock::time_point lastFrame_;
    float deltaTime_ = 0.0f;
    float fps_ = 0.0f;
    GUI* gui_ = nullptr;

    // Programs
    ShaderManager shaders_;
    GLuint shaderProgram_ = 0;   // flat terrain
    GLuint skyShader_ = 0, waterShader_ = 0, grassShader_ = 0, treeShader_ = 0;
    GLuint terrainShader_ = 0;   // chunked LOD terrain
    GLuint impostorShader_ = 0, treeBakeShader_ = 0, sunShadowShader_ = 0;
    GLuint villageShader_ = 0, villageShadowShader_ = 0;
    GLuint sponzaShader_ = 0, sponzaShadowShader_ = 0;

    // Subsystems
    Skybox sky_;
    SunShadow sunShadow_;
    PostProcess post_;
    GpuTimers gpuTimers_;
    Renderer renderer_;
    Terrain terrain_;
    Foliage foliage_;
    Village village_;
    Sponza sponza_;
    GLuint noiseTex_ = 0, waterDetailTex_ = 0;
    double lastShadowBuild_ = -1.0;
    GraphicsSettings graphics_;

    // Camera / player
    Camera camera_;
    glm::vec3 cameraPos_{0.0f, 6.0f, 12.0f};
    float yaw_ = -90.0f, pitch_ = -15.0f;
    float mouseSensitivity_ = 0.12f;
    float moveSpeed_ = 6.0f;
    double lastX_ = 0.0, lastY_ = 0.0;
    bool firstMouse_ = true;
    bool keys_[1024] = {};
    bool jumping_ = false;
    float jumpVel_ = 0.0f;
    bool swimming_ = false;
    bool underwater_ = false;
    float oxygen_ = 1.0f;        // 0..1, drains while the head is under water
    float swimTime_ = 0.0f;      // running time used for the waves

    // Island
    int terrainSize_ = 1024;
    float terrainScale_ = 1.2f;
    float heightScale_ = 100.0f;
    float textureTile_ = 22.0f;
    TerrainParams terrainParams_;
    FoliageParams foliageParams_;
    bool villageLamps_ = true;
    float lampIntensity_ = 1.0f;

    // Maps
    int map_ = 0, pendingMap_ = -1;
    float sponzaIndirect_ = 1.0f;
    bool sponzaAutoExposure_ = true;
    float sponzaExposure_ = 1.0f;
    bool snapExposure_ = true;   // jump straight to the target exposure (after teleporting)

    // Sky / sun / clouds
    std::string panoramaPath_;
    bool sunFromSky_ = true, fogFromSky_ = true;
    float sunElevation_ = 40.0f, sunAzimuth_ = 35.0f, sunIntensity_ = 1.0f;
    glm::vec3 sunTint_{1.0f, 0.96f, 0.88f};
    bool cloudEnabled_ = true;
    float cloudSpeed_ = 0.02f, cloudScale_ = 1.0f, cloudOpacity_ = 0.55f;

    // GPU profile / auto quality
    GpuInfo gpu_;
    bool needBenchmark_ = false, tierFromCache_ = false;
    int benchTier_ = 3;                     // highest tier the benchmark allowed
    float overBudgetTime_ = 0.0f, underBudgetTime_ = 0.0f;
    std::string tierReason_;
    std::vector<BenchmarkSample> benchSamples_;

    // Point lights this frame (outdoor ones first), uploaded to every scene shader
    int numPointLights_ = 0, numOutdoorLights_ = 0;
    float daylight_ = 1.0f;
    glm::vec4 pointLightPos_[16], pointLightColor_[16];

    // GUI state
    bool guiVisible_ = false;
};

} // namespace nut::detail
