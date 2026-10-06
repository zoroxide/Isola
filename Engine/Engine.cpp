#include "Engine.h"
#include "Textures.h"
#include <sstream>
#include "PostProcess.h"
#include "gui/gui.h"

#define GLM_ENABLE_EXPERIMENTAL

// GLMs
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/string_cast.hpp>

// STLs
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

// Vertex struct
struct Vertex {
  glm::vec3 pos;
  glm::vec3 normal;
  glm::vec2 uv;
};

// Static instance pointer
Engine *Engine::s_instance_ = nullptr;

Engine::Engine()
    : window_(nullptr), shaderProgram_(0), skyShader_(0),
      cameraPos_(0.0f, 6.0f, 12.0f), yaw_(-90.0f), pitch_(-15.0f),
      mouseSensitivity_(0.12f), moveSpeed_(6.0f), lastX_(0.0), lastY_(0.0),
      firstMouse_(true), lastFrame_(Clock::now()), deltaTime_(0.0f),
      jumping_(false), jumpVel_(0.0f), vsyncEnabled_(true) {
  std::fill(std::begin(keys_), std::end(keys_), false);
  s_instance_ = this;

  // Defaults for configurable constants and paths
  terrainSize_ = 1024;
  terrainScale_ = 1.2f;
  heightScale_ = 100.0f;
  textureTile_ = 22.0f;
  panoramaPath_.clear();
  terrainTexturePath_.clear();
  // Cloud defaults
  cloudEnabled_ = true;
  cloudSpeed_ = 0.02f;
  cloudScale_ = 1.0f;
  cloudOpacity_ = 0.55f;

  // Create GUI manager (will be initialized after window/context creation)
  gui_ = new GUI(this);
}

Engine::~Engine() {
  // Cleanup
  village_.clear();
  sponza_.clear();
  if (shaderProgram_)
    glDeleteProgram(shaderProgram_);
  if (skyShader_)
    glDeleteProgram(skyShader_);
  // Skybox VAO/VBO are managed by Skybox class
  if (window_)
    glfwTerminate();

  if (gui_) {
    delete gui_;
    gui_ = nullptr;
  }
}

bool Engine::init(bool fullscreen) {
  // glfw init
  if (!glfwInit())
    return false;

  // glfw window hints
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  // Create window
  GLFWmonitor *monitor = nullptr;
  int SCR_W = 1280, SCR_H = 720;

  // Fullscreen setup
  if (fullscreen) {
    monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = glfwGetVideoMode(monitor);
    SCR_W = mode->width;
    SCR_H = mode->height;
  }

  // Create window
  window_ = glfwCreateWindow(SCR_W, SCR_H, "Procedural Terrain (Engine)",
                             monitor, nullptr);
  if (!window_) {
    glfwTerminate();
    return false;
  }

  // GLEW + GL context
  glfwMakeContextCurrent(window_);
  glfwSwapInterval(vsyncEnabled_ ? 1 : 0);
  glewExperimental = GL_TRUE;
  GLenum glewErr = glewInit();
#ifdef GLEW_ERROR_NO_GLX_DISPLAY
  // On Wayland, GLEW (built for GLX) reports "no GLX display" but still loads
  // every GL function, so that error is harmless.
  if (glewErr == GLEW_ERROR_NO_GLX_DISPLAY)
    glewErr = GLEW_OK;
#endif
  if (glewErr != GLEW_OK) {
    std::cerr << "glewInit failed: " << glewGetErrorString(glewErr) << "\n";
    return false;
  }

  // Which GPU is this? Pick the quality tier before any shader is compiled: the one the
  // benchmark chose last time for this GPU + driver, or a guess from the GPU family
  // (then benchmarked on the first frame).
  gpu_ = GpuProfile::detect();
  int tier = gpu_.suggestedTier;
  tierFromCache_ = GpuProfile::loadCache("graphics.cfg", gpu_, tier);
  needBenchmark_ = !tierFromCache_;
  benchTier_ = tierFromCache_ ? tier : 3;
  std::cout << "GPU: " << gpu_.renderer << " (" << gpu_.vendorName() << ", "
            << (gpu_.vramMB > 0 ? std::to_string(gpu_.vramMB) + " MB" : std::string("VRAM unknown")) << ", OpenGL "
            << gpu_.version << ")\nQuality tier: " << GraphicsSettings::tierName(tier)
            << (tierFromCache_ ? " (saved benchmark result)" : " (initial guess; benchmarking on start)") << "\n";
  graphics_ = GraphicsSettings::forTier(tier);
  foliageParams_.grassEnabled = graphics_.grass;
  foliageParams_.grassDensity = graphics_.grassDensity;
  foliageParams_.grassRadius = graphics_.grassRadius;
  foliageParams_.treeDetailDistance = graphics_.treeDetailDistance;
  foliageParams_.treeDistance = graphics_.treeDistance;
  shaders_.setDefines("#define QUALITY " + std::to_string(graphics_.shaderQuality()) + "\n");

  // Input
  glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  glfwSetCursorPosCallback(window_, Engine::cursorPosCallbackStatic);
  glfwSetKeyCallback(window_, Engine::keyCallbackStatic);

  // GL settings
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);

  // Resources Loading(shaders, terrain mesh, etc)
  shaderProgram_ = shaders_.loadProgram("terrain", "Engine/shaders/vertex.glsl",
                                        "Engine/shaders/fragment.glsl");
  // Reuse terrain shader initially for simple model rendering.
  // For robust rendering, a separate model shader can be added later.

  // Create sky shader and setup Skybox helper
  skyShader_ = shaders_.loadProgram("sky", "Engine/shaders/sky_vert.glsl",
                                    "Engine/shaders/sky_frag.glsl");
  sky_.setShader(skyShader_);
  waterShader_ = shaders_.loadProgram("water", "Engine/shaders/water_vert.glsl",
                                      "Engine/shaders/water_frag.glsl");
  terrain_.loadMaterials("assets/textures/terrain");
  grassShader_ = shaders_.loadProgram("grass", "Engine/shaders/grass_vert.glsl",
                                      "Engine/shaders/grass_frag.glsl");
  treeShader_ = shaders_.loadProgram("tree", "Engine/shaders/tree_vert.glsl",
                                     "Engine/shaders/tree_frag.glsl");
  treeBakeShader_ = shaders_.loadProgram("treeBake", "Engine/shaders/tree_vert.glsl",
                                        "Engine/shaders/tree_bake_frag.glsl");
  impostorShader_ = shaders_.loadProgram("impostor", "Engine/shaders/impostor_vert.glsl",
                                        "Engine/shaders/impostor_frag.glsl");
  sunShadowShader_ = shaders_.loadProgram("sunShadow", "Engine/shaders/fullscreen_vert.glsl",
                                         "Engine/shaders/sun_shadow_frag.glsl");
  sunShadow_.init(sunShadowShader_, 1024);
  PostProcess::Programs pp;
  pp.bright = shaders_.loadProgram("postBright", "Engine/shaders/fullscreen_vert.glsl", "Engine/shaders/post_bright.glsl");
  pp.blur = shaders_.loadProgram("postBlur", "Engine/shaders/fullscreen_vert.glsl", "Engine/shaders/post_blur.glsl");
  pp.rays = shaders_.loadProgram("postRays", "Engine/shaders/fullscreen_vert.glsl", "Engine/shaders/post_rays.glsl");
  pp.composite = shaders_.loadProgram("postComposite", "Engine/shaders/fullscreen_vert.glsl", "Engine/shaders/post_composite.glsl");
  post_.init(pp);
  FoliagePrograms fp;
  fp.grass = grassShader_;
  fp.tree = treeShader_;
  fp.treeBake = treeBakeShader_;
  fp.impostor = impostorShader_;
  foliage_.init("assets/textures/foliage", fp);
  sky_.initFullscreenTriangle();

  // Renderer programs and scene wiring
  terrainShader_ = shaders_.loadProgram("terrainLod", "Engine/shaders/terrain_vert.glsl",
                                       "Engine/shaders/terrain_frag.glsl");
  villageShader_ = shaders_.loadProgram("village", "Engine/shaders/village_vert.glsl",
                                      "Engine/shaders/village_frag.glsl");
  villageShadowShader_ = shaders_.loadProgram("villageShadow", "Engine/shaders/village_shadow_vert.glsl",
                                            "Engine/shaders/village_shadow_frag.glsl");
  village_.init("assets/textures/village");
  sponzaShader_ = shaders_.loadProgram("sponza", "Engine/shaders/sponza_vert.glsl",
                                       "Engine/shaders/sponza_frag.glsl");
  sponzaShadowShader_ = shaders_.loadProgram("sponzaShadow", "Engine/shaders/sponza_shadow_vert.glsl",
                                             "Engine/shaders/sponza_shadow_frag.glsl");
  noiseTex_ = Textures::createNoise(256);
  waterDetailTex_ = Textures::createWaterDetail(256);
  renderer_.setPrograms(terrainShader_, shaderProgram_, skyShader_);
  // Models are drawn by the engine loop (before the transparent ocean), not the renderer
  renderer_.setScene(&terrain_, &sky_, nullptr);

  // Generate initial procedural terrain via Terrain subsystem
  terrain_.generateProcedural(terrainSize_, terrainScale_, heightScale_,
                              textureTile_, terrainParams_);
  village_.generate(terrain_, terrainParams_.seed);
  replantTrees();
  placePlayerOnLand();

  // Initialize GUI after the OpenGL context is created
  if (gui_)
    gui_->init(window_);

  village_.setShadowResolution(graphics_.villageShadowRes);
  village_.setMaxHouseLights(graphics_.maxHouseLights);
  applyTextureQuality();
  setupSamplerUnits();
  writeGpuReport();

  return true;
}

// ---------------------------------------------------------------------------
// GPU-aware quality
// ---------------------------------------------------------------------------
void Engine::refreshPrograms() {
  shaderProgram_ = shaders_.get("terrain");
  skyShader_ = shaders_.get("sky");
  waterShader_ = shaders_.get("water");
  grassShader_ = shaders_.get("grass");
  treeShader_ = shaders_.get("tree");
  treeBakeShader_ = shaders_.get("treeBake");
  impostorShader_ = shaders_.get("impostor");
  sunShadowShader_ = shaders_.get("sunShadow");
  terrainShader_ = shaders_.get("terrainLod");
  villageShader_ = shaders_.get("village");
  villageShadowShader_ = shaders_.get("villageShadow");
  sponzaShader_ = shaders_.get("sponza");
  sponzaShadowShader_ = shaders_.get("sponzaShadow");
  renderer_.setPrograms(terrainShader_, shaderProgram_, skyShader_);
  sky_.setShader(skyShader_);
  sunShadow_.setProgram(sunShadowShader_);
  FoliagePrograms fp;
  fp.grass = grassShader_; fp.tree = treeShader_; fp.treeBake = treeBakeShader_; fp.impostor = impostorShader_;
  foliage_.setPrograms(fp);
  PostProcess::Programs pp;
  pp.bright = shaders_.get("postBright"); pp.blur = shaders_.get("postBlur");
  pp.rays = shaders_.get("postRays"); pp.composite = shaders_.get("postComposite");
  post_.setPrograms(pp);
}

void Engine::applyTextureQuality() {
  // Anisotropic filtering is cheap on modern GPUs and very expensive on old / low-bandwidth ones
  float a = graphics_.anisotropy;
  Textures::setAnisotropy(terrain_.materialAlbedo(), GL_TEXTURE_2D_ARRAY, a);
  Textures::setAnisotropy(terrain_.materialNormal(), GL_TEXTURE_2D_ARRAY, std::max(1.0f, a * 0.5f));
  Textures::setAnisotropy(village_.albedoTexture(), GL_TEXTURE_2D_ARRAY, a);
  Textures::setAnisotropy(village_.normalTexture(), GL_TEXTURE_2D_ARRAY, std::max(1.0f, a * 0.5f));
  Textures::setAnisotropy(foliage_.textureArray(), GL_TEXTURE_2D_ARRAY, std::min(a, 4.0f));
  if (sponza_.loaded())
    sponza_.setAnisotropy(a);
}

void Engine::applyTier(int tier, const char *reason) {
  tier = glm::clamp(tier, 0, 3);
  int oldQuality = graphics_.shaderQuality();
  GraphicsSettings g = GraphicsSettings::forTier(tier);
  g.autoTier = graphics_.autoTier;
  g.targetFps = graphics_.targetFps;
  graphics_ = g;
  foliageParams_.grassEnabled = g.grass;
  foliageParams_.grassDensity = g.grassDensity;
  foliageParams_.grassRadius = g.grassRadius;
  foliageParams_.treeDetailDistance = g.treeDetailDistance;
  foliageParams_.treeDistance = g.treeDistance;
  if (g.shaderQuality() != oldQuality) {
    // Different shader complexity: recompile everything with the new QUALITY level
    shaders_.setDefines("#define QUALITY " + std::to_string(g.shaderQuality()) + "\n");
    shaders_.reloadAll();
    refreshPrograms();
    setupSamplerUnits();
    sunShadow_.invalidate();
  }
  village_.setShadowResolution(g.villageShadowRes);
  village_.setMaxHouseLights(g.maxHouseLights);
  sponza_.setShadowResolution(g.villageShadowRes);
  applyTextureQuality();
  post_.setScale(glm::clamp(post_.scale(), g.minScale, g.maxScale));
  overBudgetTime_ = underBudgetTime_ = 0.0f;
  if (reason) {
    tierReason_ = std::string(GraphicsSettings::tierName(tier)) + ": " + reason;
    std::cout << "Quality tier -> " << tierReason_ << "\n";
  }
}

void Engine::updateQualityGovernor(float dt) {
  // Dynamic resolution handles small swings. When even the lowest resolution can't hold the
  // target for a few seconds, drop a tier (simpler shaders, fewer effects); when the GPU idles at
  // full resolution for a long time, go back up (never above what the benchmark allowed).
  if (!graphics_.autoTier || !graphics_.autoResolution || needBenchmark_) return;
  float gpu = gpuTimers_.lastTotalMs();
  float budget = 1000.0f / std::max(graphics_.targetFps, 10.0f);
  float s = post_.scale();
  if (s <= graphics_.minScale + 0.01f && gpu > budget * 0.95f) overBudgetTime_ += dt;
  else overBudgetTime_ = std::max(0.0f, overBudgetTime_ - dt * 0.5f);
  if (s >= graphics_.maxScale - 0.01f && gpu < budget * 0.55f) underBudgetTime_ += dt;
  else underBudgetTime_ = 0.0f;
  if (overBudgetTime_ > 4.0f && graphics_.tier > 0)
    applyTier(graphics_.tier - 1, "the GPU couldn't hold the target frame rate");
  else if (underBudgetTime_ > 20.0f && graphics_.tier < benchTier_)
    applyTier(graphics_.tier + 1, "the GPU has plenty of headroom");
}

void Engine::runGpuBenchmark() {
  // Render a demanding view (the village street, or the spawn point) at two resolutions per tier,
  // timing whole frames. Frame time ~ fixed + perPixel * scale^2, so two samples predict the
  // resolution each tier can reach at the target frame rate. Pick the best tier that still runs
  // at a decent resolution, save it in graphics.cfg and write gpu_report.txt.
  needBenchmark_ = false;
  benchSamples_.clear();
  glm::vec3 savePos = cameraPos_;
  float saveYaw = yaw_, savePitch = pitch_;
  bool saveSwim = swimming_, saveAuto = graphics_.autoTier;
  float saveTarget = graphics_.targetFps;
  if (inSponza()) { cameraPos_ = sponza_.spawn(); yaw_ = sponza_.spawnYaw(); }
  else if (village_.active()) { cameraPos_ = village_.spawn(); yaw_ = village_.spawnYaw(); }
  pitch_ = -4.0f;
  swimming_ = underwater_ = false;
  int fbW = 1280, fbH = 720;
  glfwGetFramebufferSize(window_, &fbW, &fbH);

  auto showMessage = [&](const std::string &msg) {
    if (gui_) gui_->renderOverlayMessage(msg);
    glfwSwapBuffers(window_);
    glfwPollEvents();
  };
  auto measure = [&](int tier, float scale) {
    applyTier(tier);
    graphics_.autoResolution = false;
    graphics_.renderScale = scale;
    post_.setScale(scale);
    std::string msg = "Optimizing graphics for " + gpu_.renderer + "...   (testing " + GraphicsSettings::tierName(tier) + ")";
    for (int i = 0; i < 6; ++i) { renderFrame(0, fbW, fbH); showMessage(msg); }
    double total = 0.0;
    const int N = 12;
    for (int i = 0; i < N; ++i) {
      glFinish();
      auto t0 = Clock::now();
      swimTime_ += 1.0f / 60.0f;
      renderFrame(0, fbW, fbH);
      glFinish();
      total += std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
      showMessage(msg);
    }
    float ms = (float)(total / N);
    benchSamples_.push_back({tier, scale, ms});
    return ms;
  };
  const float budget = 1000.0f / std::max(saveTarget, 30.0f) * 0.9f;
  static const float needScale[4] = {0.0f, 0.5f, 0.6f, 0.72f};
  auto reachableScale = [&](int tier) {
    float a = measure(tier, 0.5f), b = measure(tier, 0.85f);
    float perPixel = std::max((b - a) / (0.85f * 0.85f - 0.25f), 0.01f);
    float fixed = a - perPixel * 0.25f;
    float s = (budget - fixed) / perPixel;
    float result = s > 0.0f ? std::sqrt(s) : 0.0f;
    std::cout << "  benchmark " << GraphicsSettings::tierName(tier) << ": " << a << " ms @50%, " << b
              << " ms @85% -> about " << int(result * 100) << "% resolution at " << saveTarget << " fps\n";
    return result;
  };
  auto good = [&](int tier) { return tier == 0 || reachableScale(tier) >= needScale[tier]; };

  int t = gpu_.suggestedTier, chosen;
  if (good(t)) {
    chosen = t;
    while (chosen < 3 && good(chosen + 1)) ++chosen;
  } else {
    chosen = t - 1;
    while (chosen > 0 && !good(chosen)) --chosen;
    chosen = std::max(chosen, 0);
  }
  graphics_.autoTier = saveAuto;
  graphics_.targetFps = saveTarget;
  applyTier(chosen, "chosen by the GPU benchmark");
  graphics_.autoResolution = true;
  benchTier_ = chosen;
  tierFromCache_ = false;
  GpuProfile::saveCache("graphics.cfg", gpu_, chosen);
  cameraPos_ = savePos; yaw_ = saveYaw; pitch_ = savePitch; swimming_ = saveSwim;
  writeGpuReport();
}

void Engine::writeGpuReport() {
  std::ostringstream passes;
  for (const auto &n : gpuTimers_.names()) passes << "  " << n << ": " << gpuTimers_.ms(n) << " ms\n";
  if (!gpuTimers_.names().empty())
    passes << "  render scale: " << int(post_.scale() * 100) << "%\n";
  GpuProfile::writeReport("gpu_report.txt", gpu_, graphics_.tier, tierFromCache_, benchSamples_, passes.str(), shaders_.log());
}

void Engine::vsync(bool enabled) {
  vsyncEnabled_ = enabled;
  if (window_)
    glfwSwapInterval(enabled ? 1 : 0);
}

bool Engine::getVsyncEnabled() const { return vsyncEnabled_; }

/* (Getters / Setters) */

// Terrain accessors
int Engine::getTerrainSize() const { return terrainSize_; }
void Engine::setTerrainSize(int v) { terrainSize_ = v; }
float Engine::getTerrainScale() const { return terrainScale_; }
void Engine::setTerrainScale(float v) { terrainScale_ = v; }
float Engine::getHeightScale() const { return heightScale_; }
void Engine::setHeightScale(float v) { heightScale_ = v; }
float Engine::getTextureTile() const { return textureTile_; }
void Engine::setTextureTile(float v) { textureTile_ = v; }

// Path accessors
const std::string &Engine::getPanoramaPath() const { return panoramaPath_; }
void Engine::setPanoramaPath(const std::string &p) { panoramaPath_ = p; }
const std::string &Engine::getTerrainTexturePath() const {
  return terrainTexturePath_;
}
void Engine::setTerrainTexturePath(const std::string &p) {
  terrainTexturePath_ = p;
}

// Cloud accessors
bool Engine::getCloudEnabled() const { return cloudEnabled_; }
void Engine::setCloudEnabled(bool v) { cloudEnabled_ = v; }
float Engine::getCloudSpeed() const { return cloudSpeed_; }
void Engine::setCloudSpeed(float v) { cloudSpeed_ = v; }
float Engine::getCloudScale() const { return cloudScale_; }
void Engine::setCloudScale(float v) { cloudScale_ = v; }
float Engine::getCloudOpacity() const { return cloudOpacity_; }
void Engine::setCloudOpacity(float v) { cloudOpacity_ = v; }

void Engine::load_terrain_using_texture(const std::string &texturePath,
                                        const std::string &objPath) {
  // Load a procedural terrain texture via Terrain subsystem
  if (!texturePath.empty()) {
    terrain_.loadProceduralTexture(texturePath);
  }

  if (!objPath.empty()) {
    models_.loadOBJ(objPath, glm::vec3(0.0f), glm::vec3(1.0f));
  }
}

void Engine::add_house(const std::string &objPath, const glm::vec3 &position,
                       const glm::vec3 &scale) {
  // Delegate to Models manager
  if (!models_.loadOBJ(objPath, position, scale)) {
    std::cerr << "Error: Failed to load house OBJ via Models: " << objPath
              << "\n";
  }
}

bool Engine::load_flat_terrain(const std::string &texturePath) {
  // Propagate current texture tiling to Terrain so flat UVs are repeated
  terrain_.setTextureTile(textureTile_);
  hasFlat_ = terrain_.buildFlat(texturePath);
  if (!hasFlat_)
    return false;
  village_.clear();
  foliage_.setClearing(glm::vec3(0));
  foliage_.setPlantedTrees({});
  terrain_.setFlatScale(glm::vec3(terrainSize_ * terrainScale_ * 0.5f, 1.0f,
                                  terrainSize_ * terrainScale_ * 0.5f));

  // Also place a house model in front of the player when switching to flat
  // terrain. Use the current yaw to compute forward direction on XZ, and
  // position the house a few meters ahead.
  {
    // Place a properly sized house in front of the player on the flat plane
    glm::vec3 forward = glm::normalize(
        glm::vec3(cos(glm::radians(yaw_)), 0.0f, sin(glm::radians(yaw_))));
    glm::vec3 housePos =
        cameraPos_ +
        forward * 12.0f; // a bit farther ahead to avoid immediate collision
    housePos.y = 0.01f;  // slight lift to avoid z-fighting with the plane
    // Use a larger uniform scale so the user can enter the house comfortably
    glm::vec3 houseScale = glm::vec3(6.0f);
    // Load the house OBJ via Models subsystem (encapsulated loading/drawing)
    models_.loadOBJ("assets/objs/house.obj", housePos, houseScale);
  }
  return true;
}
void Engine::setupSamplerUnits() {
  // Texture units (fixed for the whole run): 0 surface/model texture, 2 height map, 3 sky cube,
  // 4 terrain albedo array, 6 canopy shade, 7 leaf/bark cards, 8 noise, 9 water ripples,
  // 10 sun shadow height map, 11/12 tree billboard atlases
  auto set = [](GLuint p, const char *name, int unit) {
    glUseProgram(p);
    glUniform1i(glGetUniformLocation(p, name), unit);
  };
  for (GLuint p : {shaderProgram_, terrainShader_}) {
    set(p, "texture1", 0);
    set(p, "heightTex", 2);
    set(p, "canopyShade", 6);
  }
  set(waterShader_, "heightTex", 2);
  set(grassShader_, "heightTex", 2);
  set(grassShader_, "matAlbedo", 4);
  set(treeShader_, "foliageTex", 7);
}

void Engine::renderFrame(GLuint outputFbo, int outW, int outH) {
  // Camera
  camera_.setPosition(cameraPos_);
  camera_.setYawPitch(yaw_, pitch_);
  glm::mat4 view = camera_.getView();
  glm::mat4 proj = camera_.getProj(underwater_ ? 70.0f : 60.0f, (float)outW / (float)std::max(outH, 1), 0.2f, 4000.0f);
  glm::mat4 VP = proj * view;
  glm::mat4 invProj = glm::inverse(proj);
  glm::mat4 invView = glm::inverse(view);

  gpuTimers_.newFrame();

  // Light the scene to match the sky (sun direction/colour detected from HDR panoramas)
  // or from the manual sun controls
  sky_.sunOverride = !sunFromSky_;
  if (!sunFromSky_) {
    float el = glm::radians(sunElevation_), az = glm::radians(sunAzimuth_);
    glm::vec3 toSun(std::cos(el) * std::cos(az), std::sin(el), std::cos(el) * std::sin(az));
    // Low sun: warmer and dimmer light through more atmosphere
    float low = 1.0f - glm::smoothstep(0.0f, 0.5f, toSun.y);
    sky_.overrideLightDir = -toSun;
    sky_.overrideLightColor = sunTint_ * sunIntensity_ * glm::mix(glm::vec3(1.0f), glm::vec3(1.0f, 0.62f, 0.38f), low * 0.8f) *
                              (0.35f + 0.65f * glm::smoothstep(-0.05f, 0.25f, toSun.y));
  }
  glm::vec3 lightDir = sky_.lightDirection();
  glm::vec3 lightCol = sky_.lightColor();
  // Colour of the water seen from inside it (dimmer when the sun is weak)
  const TerrainParams &TP = terrain_.params();
  float sunLum = glm::clamp(glm::dot(lightCol, glm::vec3(0.3f, 0.6f, 0.1f)), 0.25f, 1.0f);
  glm::vec3 uwColor = glm::mix(TP.waterDeep, TP.waterShallow, 0.45f) * (0.35f + 0.9f * sunLum);

  post_.setAdaptation(1.0f, false);
  if (inSponza()) {
    renderSponza(view, proj, invView, invProj, VP, lightDir, lightCol, uwColor, outputFbo, outW, outH);
    return;
  }

  // Sun shadows: rebuilt when the sun moves (at most 4x per second while it is being dragged)
  double nowSec = glfwGetTime();
  if (graphics_.shadows)
    village_.buildShadow(villageShadowShader_, -lightDir);
  if (graphics_.shadows && sunShadow_.needsRebuild(-lightDir) && nowSec - lastShadowBuild_ > 0.25) {
    gpuTimers_.begin("shadows");
    sunShadow_.build(terrain_, foliage_.canopyHeightTexture(), -lightDir);
    gpuTimers_.end();
    lastShadowBuild_ = nowSec;
  }
  gatherLights(lightCol);
  for (GLuint p : {shaderProgram_, terrainShader_, waterShader_, grassShader_, treeShader_, impostorShader_, villageShader_})
    setPerFrameUniforms(p, lightDir, lightCol, uwColor);
  // Lamp light on the grass is a per-vertex cost; only worth it once it is getting dark
  glUseProgram(grassShader_);
  glUniform1i(glGetUniformLocation(grassShader_, "grassLights"), daylight_ < 0.5f ? std::min(numOutdoorLights_, 4) : 0);
  glUseProgram(skyShader_);
  glUniform1i(glGetUniformLocation(skyShader_, "underwater"), underwater_ ? 1 : 0);
  glUniform3fv(glGetUniformLocation(skyShader_, "uwColor"), 1, &uwColor.x);
  sky_.fogColor = terrainParams_.fogColor;
  terrain_.setLodDistance(graphics_.terrainLodDistance);

  // --- 3D scene into the (dynamic resolution) scene buffer ---
  post_.beginScene(outW, outH, post_.scale());
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glClearColor(0.53f, 0.8f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Village first: its walls hide the terrain behind them (and everything else) early
  gpuTimers_.begin("village");
  village_.draw(villageShader_, villageShadowShader_, view, proj, foliage_.textureArray(),
                villageLamps_ ? lampIntensity_ : 0.0f, swimTime_);
  gpuTimers_.end();

  gpuTimers_.begin("terrain+sky");
  renderer_.drawFrame(view, proj, glm::mat4(1.0f), invView, invProj, cameraPos_,
                      sky_.hasCubemap(), swimTime_, cloudEnabled_, cloudSpeed_,
                      cloudScale_, cloudOpacity_);
  gpuTimers_.end();

  gpuTimers_.begin("trees+grass");
  foliage_.draw(terrain_, foliageParams_, view, proj, cameraPos_, swimTime_);
  models_.drawAll(shaderProgram_, view, proj);
  gpuTimers_.end();
  gpuTimers_.begin("village glass");
  village_.drawGlass(villageShader_, view, proj);
  gpuTimers_.end();

  // Ocean last: it is transparent and must blend over everything below it
  gpuTimers_.begin("water");
  terrain_.drawWater(waterShader_, view, proj, cameraPos_, swimTime_);
  gpuTimers_.end();

  // --- Post-processing and upscale to the output ---
  gpuTimers_.begin("post");
  post_.endScene(graphics_, outputFbo, VP, cameraPos_, -lightDir, lightCol, underwater_, swimTime_);
  gpuTimers_.end();

  // Dynamic resolution from the measured GPU time of the whole frame
  post_.updateScale(graphics_, gpuTimers_.lastTotalMs());
}

// The Sponza map: no terrain, water, grass or village; the building is lit by the sun (shadow map)
// and its baked sky / bounce light, then the sky fills the courtyard opening.
void Engine::renderSponza(const glm::mat4 &view, const glm::mat4 &proj, const glm::mat4 &invView,
                          const glm::mat4 &invProj, const glm::mat4 &VP, const glm::vec3 &lightDir,
                          const glm::vec3 &lightCol, const glm::vec3 &uwColor, GLuint outputFbo, int outW,
                          int outH) {
  if (graphics_.shadows) {
    gpuTimers_.begin("shadows");
    sponza_.buildShadow(sponzaShadowShader_, -lightDir);
    gpuTimers_.end();
  }
  sponza_.updateIndirect(lightDir);
  gatherLights(lightCol);
  setPerFrameUniforms(sponzaShader_, lightDir, lightCol, uwColor);
  glUseProgram(skyShader_);
  glUniform1i(glGetUniformLocation(skyShader_, "underwater"), 0);
  sky_.fogColor = terrainParams_.fogColor;

  post_.beginScene(outW, outH, post_.scale());
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glClearColor(0.53f, 0.8f, 1.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  gpuTimers_.begin("sponza");
  sponza_.draw(sponzaShader_, view, proj, sponzaIndirect_);
  gpuTimers_.end();
  gpuTimers_.begin("sky");
  sky_.setShader(skyShader_);
  sky_.draw(invView, invProj, sky_.hasCubemap(), swimTime_, cloudEnabled_, cloudSpeed_, cloudScale_, cloudOpacity_);
  gpuTimers_.end();

  // Eye adaptation: expose for the (baked) light around the camera, so the shaded galleries read
  // like they do to an adapted eye while the sunlit courtyard rolls off softly instead of clipping
  float target = 1.0f;
  if (sponzaAutoExposure_) {
    glm::vec4 L = sponza_.lightAt(cameraPos_);
    float sunLum = glm::dot(lightCol, glm::vec3(0.3f, 0.59f, 0.11f));
    float level = std::max(L.x, 0.012f) * 0.55f +
                  glm::dot(glm::vec3(L.y, L.z, L.w), glm::vec3(0.3f, 0.59f, 0.11f)) * sunLum * sponzaIndirect_;
    target = glm::clamp(0.12f / level, 1.0f, 16.0f);
  }
  if (snapExposure_)
    sponzaExposure_ = target;
  else
    sponzaExposure_ += (target - sponzaExposure_) * (1.0f - std::exp(-deltaTime_ * 1.5f));
  snapExposure_ = false;
  post_.setAdaptation(sponzaExposure_, true);

  gpuTimers_.begin("post");
  post_.endScene(graphics_, outputFbo, VP, cameraPos_, -lightDir, lightCol, false, swimTime_);
  gpuTimers_.end();
  post_.updateScale(graphics_, gpuTimers_.lastTotalMs());
}

void Engine::mainloop() {
  if (!window_)
    return;
  setupSamplerUnits();
  if (needBenchmark_)
    runGpuBenchmark();
  double reportAt = glfwGetTime() + 20.0;   // refresh gpu_report.txt with real per-pass timings

  lastFrame_ = Clock::now();
  while (!glfwWindowShouldClose(window_)) {
    auto now = Clock::now();
    deltaTime_ = std::chrono::duration<float>(now - lastFrame_).count();
    lastFrame_ = now;
    if (pendingMap_ >= 0) {
      int map = pendingMap_;
      pendingMap_ = -1;
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
      glClear(GL_COLOR_BUFFER_BIT);
      if (gui_) gui_->renderOverlayMessage(std::string("Loading ") + mapName(map) + "...", "");
      glfwSwapBuffers(window_);
      if (!selectMap(map))
        std::cerr << "Could not load the map " << mapName(map) << "\n";
      lastFrame_ = Clock::now();   // don't count the loading time as a frame
      continue;
    }
    terrain_.setLiveParams(terrainParams_);
    updateMovement(deltaTime_);

    // Render at the framebuffer size (differs from the window size on HiDPI screens)
    int fbW, fbH;
    glfwGetFramebufferSize(window_, &fbW, &fbH);
    if (needBenchmark_)
      runGpuBenchmark();   // requested from the settings panel
    if (fbW > 0 && fbH > 0) {
      renderFrame(0, fbW, fbH);
      gui_->render();
    }
    updateQualityGovernor(deltaTime_);
    if (glfwGetTime() > reportAt) {
      writeGpuReport();
      reportAt = 1e30;
    }
    glfwSwapBuffers(window_);
    glfwPollEvents();
  }
}

// ---------------- Utility / helpers ----------------

bool Engine::panorama(const std::string &path) {
  if (!sky_.loadFromPath(path))
    return false;
  panoramaPath_ = path;
  // Photographic skies already contain clouds; the procedural layer is for the
  // built-in sky (it can still be re-enabled from the GUI)
  cloudEnabled_ = path.empty();
  return true;
}

// Input callbacks
void Engine::cursorPosCallbackStatic(GLFWwindow *, double xpos, double ypos) {
  if (s_instance_)
    s_instance_->cursorPosCallback(xpos, ypos);
}
void Engine::keyCallbackStatic(GLFWwindow *window, int key, int scancode,
                               int action, int mods) {
  if (s_instance_)
    s_instance_->keyCallback(key, scancode, action, mods);
}

void Engine::cursorPosCallback(double xpos, double ypos) {
  // Free cursor (settings panel open / ENTER): the mouse drives the GUI, not the camera
  if (glfwGetInputMode(window_, GLFW_CURSOR) != GLFW_CURSOR_DISABLED) {
    firstMouse_ = true;
    return;
  }
  if (firstMouse_) {
    lastX_ = xpos;
    lastY_ = ypos;
    firstMouse_ = false;
  }
  double xoff = xpos - lastX_;
  double yoff = lastY_ - ypos;
  lastX_ = xpos;
  lastY_ = ypos;
  xoff *= mouseSensitivity_;
  yoff *= mouseSensitivity_;
  yaw_ += (float)xoff;
  pitch_ += (float)yoff;
  pitch_ = glm::clamp(pitch_, -89.0f, 89.0f);
}

void Engine::setGuiVisible(bool v) {
  guiVisible_ = v;
  glfwSetInputMode(window_, GLFW_CURSOR, v ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
  firstMouse_ = true; // no camera jump when the cursor is captured again
}

void Engine::keyCallback(int key, int, int action, int) {
  // While typing into a GUI text field, keys belong to ImGui (releases always pass
  // through so movement keys can't get stuck)
  bool typing = guiVisible_ && ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput;
  if (typing && action != GLFW_RELEASE)
    return;

  if (key >= 0 && key < 1024)
    keys_[key] = (action == GLFW_PRESS || action == GLFW_REPEAT); // key states

  // TAB shows / hides the settings panel (and frees the mouse to use it)
  if (key == GLFW_KEY_TAB && action == GLFW_PRESS)
    setGuiVisible(!guiVisible_);

  // ESC closes the window
  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    glfwSetWindowShouldClose(window_, true);

  // SPACE for jumping
  if (key == GLFW_KEY_SPACE && action == GLFW_PRESS && !jumping_ && !swimming_) {
    jumping_ = true;
    jumpVel_ = JUMP_VELOCITY; // ideal 7 for normal jump
  }

  // ENTER toggles mouse visibility
  if (key == GLFW_KEY_ENTER && action == GLFW_PRESS) {
    bool captured = glfwGetInputMode(window_, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
    glfwSetInputMode(window_, GLFW_CURSOR, captured ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
    firstMouse_ = true;
  }
}

void Engine::setPerFrameUniforms(GLuint prog, const glm::vec3 &lightDir,
                                 const glm::vec3 &lightCol,
                                 const glm::vec3 &uwColor) {
  glUseProgram(prog);
  glUniform3fv(glGetUniformLocation(prog, "lightDir"), 1, &lightDir.x);
  glUniform3fv(glGetUniformLocation(prog, "lightColor"), 1, &lightCol.x);
  glUniform1i(glGetUniformLocation(prog, "fogFromSky"), fogFromSky_ ? 1 : 0);
  glUniform1i(glGetUniformLocation(prog, "underwater"), underwater_ ? 1 : 0);
  glUniform3fv(glGetUniformLocation(prog, "uwColor"), 1, &uwColor.x);
  glUniform1f(glGetUniformLocation(prog, "time"), swimTime_);
  sky_.bindForLighting(prog, 3);
  // Atmosphere (shared by every scene shader through common.glsl)
  const TerrainParams &P = terrainParams_;
  glUniform3fv(glGetUniformLocation(prog, "fogColor"), 1, &P.fogColor.x);
  glUniform1f(glGetUniformLocation(prog, "fogDensity"), P.fogDensity);
  glUniform1f(glGetUniformLocation(prog, "fogHeightDensity"), graphics_.fogHeightDensity);
  glUniform1f(glGetUniformLocation(prog, "fogHeightFalloff"), graphics_.fogHeightFalloff);
  glUniform1f(glGetUniformLocation(prog, "fogBaseY"), inSponza() ? -1000.0f : terrain_.getWaterY());
  glUniform1f(glGetUniformLocation(prog, "sunGlow"), graphics_.sunGlow);
  glUniform3fv(glGetUniformLocation(prog, "viewPos"), 1, &cameraPos_.x);
  // Procedural helper textures: 8 noise, 9 water ripples
  glUniform1i(glGetUniformLocation(prog, "noiseTex"), 8);
  glUniform1i(glGetUniformLocation(prog, "waterDetail"), 9);
  glActiveTexture(GL_TEXTURE8);
  glBindTexture(GL_TEXTURE_2D, noiseTex_);
  glActiveTexture(GL_TEXTURE9);
  glBindTexture(GL_TEXTURE_2D, waterDetailTex_);
  glActiveTexture(GL_TEXTURE0);
  // Sun shadow height map on unit 10, canopy shade on unit 6
  // On the Sponza map its own shadow map takes the village's place (same uniforms)
  sunShadow_.bind(prog, 10, terrain_.getHalfExtent(), graphics_.shadowStrength, graphics_.shadows && !inSponza());
  if (inSponza())
    sponza_.bindShadow(prog, 13, graphics_.shadowStrength, graphics_.shadows);
  else
    village_.bindShadow(prog, 13, graphics_.shadowStrength, graphics_.shadows);
  village_.bindMask(prog, 14);
  if (inSponza())
    glUniform1i(glGetUniformLocation(prog, "hasVillageMask"), 0);
  glUniform1i(glGetUniformLocation(prog, "numPointLights"), numPointLights_);
  glUniform1i(glGetUniformLocation(prog, "numOutdoorLights"), numOutdoorLights_);
  glUniform4fv(glGetUniformLocation(prog, "pointLightPos"), 16, &pointLightPos_[0].x);
  glUniform4fv(glGetUniformLocation(prog, "pointLightColor"), 16, &pointLightColor_[0].x);
  foliage_.bindCanopyShade(prog, 6, terrain_.getHalfExtent());
}

void Engine::updateMovement(float dt) {
  // Walking: WASD + SPACE to jump, SHIFT to sprint.
  // Swimming (deep water): the player floats at the surface and rides the waves.
  //   W swims where you look (look down to dive), SPACE swims up, CTRL / C dives,
  //   SHIFT swims faster. Oxygen drains while the head is under water.
  dt = std::min(dt, 0.1f); // avoid tunnelling after a stall
  swimTime_ += dt;
  if (inSponza()) {
    updateSponzaMovement(dt);
    return;
  }

  const float eyeHeight = 1.7f;
  const float floatEye = 0.35f;   // eye height above the surface while floating
  const float swimDepth = 1.35f;  // water deeper than this and you start swimming

  glm::vec3 flatFront = glm::normalize(
      glm::vec3(cos(glm::radians(yaw_)), 0.0f, sin(glm::radians(yaw_))));
  glm::vec3 right = glm::normalize(glm::cross(flatFront, glm::vec3(0, 1, 0)));
  glm::vec3 lookFront = camera_.forward();

  auto input = [&](const glm::vec3 &fwd) {
    glm::vec3 m(0.0f);
    if (keys_[GLFW_KEY_W]) m += fwd;
    if (keys_[GLFW_KEY_S]) m -= fwd;
    if (keys_[GLFW_KEY_A]) m -= right;
    if (keys_[GLFW_KEY_D]) m += right;
    return glm::length(m) > 0.0f ? glm::normalize(m) : m;
  };

  float ground = terrain_.getHeightAt(cameraPos_.x, cameraPos_.z);
  float surface = terrain_.getWaterSurfaceAt(cameraPos_.x, cameraPos_.z, swimTime_);
  float depth = surface - ground;

  if (!swimming_) {
    // Wading slows you down as the water gets deeper
    float wade = glm::clamp(depth / swimDepth, 0.0f, 1.0f);
    float sp = moveSpeed_ * (keys_[GLFW_KEY_LEFT_SHIFT] ? SPRINT_MULTIPLIER : 1.0f) *
               (1.0f - 0.55f * wade);
    glm::vec3 walk = input(flatFront) * sp * dt;
    int steps = std::max(1, int(std::ceil(glm::length(walk) / 0.15f)));
    for (int step = 0; step < steps; ++step) {
      cameraPos_ += walk / float(steps);
      village_.collide(cameraPos_);
    }
    foliage_.resolveCollision(cameraPos_);
    // Ground under the feet: terrain, or a village floor / stair / step
    ground = groundHeight(cameraPos_.x, cameraPos_.z, cameraPos_.y - eyeHeight);
    surface = terrain_.getWaterSurfaceAt(cameraPos_.x, cameraPos_.z, swimTime_);
    depth = surface - terrain_.getHeightAt(cameraPos_.x, cameraPos_.z);

    // Walked off an edge (balcony, stairwell): fall instead of snapping down
    if (!jumping_ && cameraPos_.y - eyeHeight > ground + 0.6f) {
      jumping_ = true;
      jumpVel_ = 0.0f;
    }
    if (jumping_) {
      cameraPos_.y += jumpVel_ * dt;
      jumpVel_ -= 18.0f * dt;
      if (cameraPos_.y <= ground + eyeHeight) {
        cameraPos_.y = ground + eyeHeight;
        jumping_ = false;
        jumpVel_ = 0.0f;
      }
    } else {
      cameraPos_.y = ground + eyeHeight;
    }
    // Deep enough to float: start swimming (also when jumping/falling into the sea)
    if (depth > swimDepth && cameraPos_.y < surface + eyeHeight) {
      swimming_ = true;
      jumping_ = false;
      jumpVel_ = std::min(jumpVel_, 0.0f) * 0.3f; // splash: keep a little downward momentum
    }
  } else {
    bool fast = keys_[GLFW_KEY_LEFT_SHIFT];
    float sp = (fast ? 4.8f : 2.8f);
    bool headUnder = cameraPos_.y < surface - 0.15f;
    // At the surface W swims flat unless you look well down (to dive); under water it follows the view
    glm::vec3 fwd = (headUnder || lookFront.y < -0.45f) ? lookFront : flatFront;
    glm::vec3 move = input(fwd) * sp;

    // Vertical: SPACE up, CTRL/C down, otherwise buoyancy pulls you to the surface
    float vy = move.y;
    bool up = keys_[GLFW_KEY_SPACE] || oxygen_ <= 0.0f;
    bool down = (keys_[GLFW_KEY_LEFT_CONTROL] || keys_[GLFW_KEY_C]) && oxygen_ > 0.0f;
    if (up) vy += oxygen_ <= 0.0f ? 4.0f : 2.5f;
    if (down) vy -= 2.5f;
    float target = surface + floatEye;
    if (!up && !down && std::fabs(move.y) < 0.2f) {
      if (cameraPos_.y > target - 1.2f)
        vy += (target - cameraPos_.y) * 7.0f;   // settle on the surface and ride the waves
      else
        vy += 0.9f;                               // buoyancy from deeper down
    }
    // Leftover fall speed from a jump/dive into the water, damped by drag
    jumpVel_ *= std::exp(-3.0f * dt);
    vy += jumpVel_;

    cameraPos_ += glm::vec3(move.x, 0.0f, move.z) * dt;
    foliage_.resolveCollision(cameraPos_);
    cameraPos_.y += vy * dt;

    ground = terrain_.getHeightAt(cameraPos_.x, cameraPos_.z);
    surface = terrain_.getWaterSurfaceAt(cameraPos_.x, cameraPos_.z, swimTime_);
    depth = surface - ground;
    cameraPos_.y = std::min(cameraPos_.y, surface + floatEye);  // can't fly out of the water
    cameraPos_.y = std::max(cameraPos_.y, ground + 0.45f);       // don't sink into the sea floor

    // Standing depth reached (e.g. swam onto a beach): walk again
    if (depth < swimDepth - 0.1f && ground + eyeHeight >= surface + floatEye - 0.2f) {
      swimming_ = false;
      jumpVel_ = 0.0f;
      cameraPos_.y = ground + eyeHeight;
    }
  }

  // Head under water?
  float surfaceHere = terrain_.getWaterSurfaceAt(cameraPos_.x, cameraPos_.z, swimTime_);
  underwater_ = cameraPos_.y < surfaceHere - 0.05f;

  // Oxygen: ~20 s of breath, refills quickly at the surface. When empty you are pushed up.
  if (underwater_)
    oxygen_ = std::max(0.0f, oxygen_ - dt / 20.0f);
  else
    oxygen_ = std::min(1.0f, oxygen_ + dt / 3.0f);
}

// Walking around the Sponza palace: floors from the building's geometry, walls and columns block.
void Engine::updateSponzaMovement(float dt) {
  const float eyeHeight = 1.7f;
  swimming_ = underwater_ = false;
  oxygen_ = 1.0f;
  glm::vec3 flatFront = glm::normalize(glm::vec3(cos(glm::radians(yaw_)), 0.0f, sin(glm::radians(yaw_))));
  glm::vec3 right = glm::normalize(glm::cross(flatFront, glm::vec3(0, 1, 0)));
  glm::vec3 m(0.0f);
  if (keys_[GLFW_KEY_W]) m += flatFront;
  if (keys_[GLFW_KEY_S]) m -= flatFront;
  if (keys_[GLFW_KEY_A]) m -= right;
  if (keys_[GLFW_KEY_D]) m += right;
  if (glm::length(m) > 0.0f) m = glm::normalize(m);
  // A palace, not an island: a slower walk
  glm::vec3 walk = m * moveSpeed_ * 0.6f * (keys_[GLFW_KEY_LEFT_SHIFT] ? SPRINT_MULTIPLIER : 1.0f) * dt;
  int steps = std::max(1, int(std::ceil(glm::length(walk) / 0.1f)));
  for (int step = 0; step < steps; ++step) {
    glm::vec3 before = cameraPos_;
    cameraPos_ += walk / float(steps);
    sponza_.collide(cameraPos_, eyeHeight);
    // No floor there (a hole, or a step too high to climb): stay put
    if (sponza_.groundAt(cameraPos_.x, cameraPos_.z, cameraPos_.y - eyeHeight) < -1e8f)
      cameraPos_ = before;
  }
  float ground = sponza_.groundAt(cameraPos_.x, cameraPos_.z, cameraPos_.y - eyeHeight);
  if (!jumping_ && cameraPos_.y - eyeHeight > ground + 0.5f) {   // walked off a ledge
    jumping_ = true;
    jumpVel_ = 0.0f;
  }
  if (jumping_) {
    if (jumpVel_ > 0.0f && sponza_.headroom(cameraPos_) < 0.15f)
      jumpVel_ = 0.0f;   // bumped the head
    cameraPos_.y += jumpVel_ * dt;
    jumpVel_ -= 18.0f * dt;
    if (cameraPos_.y <= ground + eyeHeight) {
      cameraPos_.y = ground + eyeHeight;
      jumping_ = false;
      jumpVel_ = 0.0f;
    }
  } else {
    // Ease up steps instead of popping
    cameraPos_.y += (ground + eyeHeight - cameraPos_.y) * std::min(1.0f, dt * 18.0f);
  }
  if (cameraPos_.y < sponza_.boundsMin().y - 20.0f)
    teleportToSpawn();   // fell out of the world
}

// ----------------- Maps -----------------
int Engine::mapCount() {
  int presets = 0;
  TerrainParams::presetNames(presets);
  return presets + 1;
}

const char *Engine::mapName(int map) {
  int presets = 0;
  const char *const *names = TerrainParams::presetNames(presets);
  if (map >= 0 && map < presets)
    return names[map];
  return map == presets ? "Sponza Palace" : "?";
}

bool Engine::inSponza() const { return map_ == mapCount() - 1; }

bool Engine::selectMap(int map) {
  if (map < 0 || map >= mapCount())
    return false;
  if (map == mapCount() - 1) {
    // Low tiers keep the textures smaller (the full set is ~70 maps of 1024 px)
    int texSize = graphics_.tier <= 1 ? 512 : 1024;
    if (!sponza_.load("assets/maps/sponza/Sponza.gltf", texSize, graphics_.anisotropy))
      return false;
    sponza_.setShadowResolution(graphics_.villageShadowRes);
    map_ = map;
    teleportToSpawn();
    return true;
  }
  bool wasSponza = inSponza();
  int seed = terrainParams_.seed;
  terrainParams_ = TerrainParams::preset(map);
  terrainParams_.seed = seed;
  map_ = map;
  regenerateTerrain();
  if (wasSponza)
    sunShadow_.invalidate();
  return true;
}

void Engine::teleportToSpawn() {
  if (inSponza()) {
    cameraPos_ = sponza_.spawn();
    yaw_ = sponza_.spawnYaw();
    pitch_ = 4.0f;
    snapExposure_ = true;
    swimming_ = underwater_ = jumping_ = false;
    jumpVel_ = 0.0f;
    oxygen_ = 1.0f;
  } else {
    placePlayerOnLand();
  }
}

// ----------------- Runtime config API -----------------
float Engine::groundHeight(float x, float z, float feetY) const {
  return std::max(terrain_.getHeightAt(x, z), village_.groundAt(x, z, feetY));
}

void Engine::gatherLights(const glm::vec3 &sunColor) {
  // Pick the 16 lights nearest to the camera. Outdoor lamps are dimmed in bright daylight
  // (they still glow), interior lamps and fires always matter because interiors are shaded.
  numPointLights_ = numOutdoorLights_ = 0;
  daylight_ = glm::smoothstep(0.3f, 0.9f, glm::dot(sunColor, glm::vec3(0.3f, 0.6f, 0.1f)));
  // In full daylight a lamp's pool of light is invisible: skip the per-pixel light loops then
  // (lantern glass still glows; interior lamps and fires are applied per house regardless)
  if (inSponza() || !villageLamps_ || !village_.active() || daylight_ > 0.8f) return;
  const auto &all = village_.lights();
  std::vector<std::pair<float, int>> order;
  for (int i = 0; i < (int)all.size(); ++i) {
    if (!all[i].outdoor) continue;   // interior lights are applied per house by the village
    float d = glm::length(all[i].pos - cameraPos_);
    if (d < 160.0f) order.push_back({d, i});
  }
  std::sort(order.begin(), order.end());
  if ((int)order.size() > graphics_.maxOutdoorLights) order.resize(std::max(0, graphics_.maxOutdoorLights));
  float daylight = glm::smoothstep(0.3f, 0.9f, glm::dot(sunColor, glm::vec3(0.3f, 0.6f, 0.1f)));
  daylight_ = daylight;
  float t = swimTime_;
  for (const auto &o : order) {
    const VillageLight &L = all[o.second];
    float f = 1.0f;
    if (L.flicker > 0.0f)
      f += L.flicker * (0.5f * std::sin(t * 13.0f + o.second * 1.7f) + 0.3f * std::sin(t * 7.3f + o.second * 2.9f) +
                        0.2f * std::sin(t * 23.0f + o.second));
    float scale = lampIntensity_ * f * (L.outdoor ? glm::mix(1.0f, 0.3f, daylight) : 1.0f);
    pointLightPos_[numPointLights_] = glm::vec4(L.pos, L.radius);
    pointLightColor_[numPointLights_] = glm::vec4(L.color * scale, 0.0f);
    ++numPointLights_;
    if (L.outdoor) ++numOutdoorLights_;
  }
}

void Engine::regenerateVillage() {
  // A new village needs fresh terrain (the old terraces are carved into it)
  regenerateTerrain();
}

void Engine::teleportToVillage() {
  if (!village_.active()) return;
  cameraPos_ = village_.spawn();
  yaw_ = village_.spawnYaw();
  pitch_ = -4.0f;
  swimming_ = underwater_ = jumping_ = false;
  jumpVel_ = 0.0f;
}

void Engine::regenerateTerrain() {
  terrain_.generateProcedural(terrainSize_, terrainScale_, heightScale_,
                              textureTile_, terrainParams_);
  village_.generate(terrain_, terrainParams_.seed);
  replantTrees();
  placePlayerOnLand();
}

void Engine::replantTrees() {
  foliage_.setClearing(village_.clearing());
  foliage_.setPlantedTrees(village_.plantedTrees());
  foliage_.generate(terrain_, foliageParams_, terrainParams_.seed);
  terrain_.buildMinimap(512, &foliage_.treeDots());
  sunShadow_.invalidate(); // trees and terrain changed: shadows are rebuilt next frame
}

void Engine::placePlayerOnLand() {
  if (village_.active()) {
    cameraPos_ = village_.spawn(); yaw_ = village_.spawnYaw(); pitch_ = -4.0f;
    swimming_ = underwater_ = jumping_ = false;
    jumpVel_ = 0; oxygen_ = 1;
    return;
  }
  // Start on a beach looking out to sea. Walk outward from the centre in 16 directions
  // to the coast, step back inland a little, and keep the lowest spot (a beach, not a cliff).
  float waterY = terrain_.getWaterY();
  float half = terrain_.getHalfExtent();
  swimming_ = underwater_ = jumping_ = false;
  oxygen_ = 1.0f;
  float bestH = 1e9f;
  cameraPos_ = glm::vec3(0.0f, terrain_.getHeightAt(0, 0) + 1.7f, 0.0f);
  for (int k = 0; k < 16; ++k) {
    float a = 0.8f + k * 0.3927f;
    glm::vec2 dir(cos(a), sin(a));
    float lastLand = -1.0f;
    for (float r = 0.0f; r < half * 0.95f; r += 2.0f) {
      float h = terrain_.getHeightAt(dir.x * r, dir.y * r);
      if (h > waterY + 0.8f) lastLand = r;
      else if (lastLand >= 0.0f && r - lastLand > 30.0f) break; // reached open water
    }
    if (lastLand < 0.0f) continue;
    float r = std::max(0.0f, lastLand - 12.0f);
    float x = dir.x * r, z = dir.y * r;
    float h = terrain_.getHeightAt(x, z);
    if (h > waterY + 1.0f && h < bestH) {
      bestH = h;
      cameraPos_ = glm::vec3(x, h + 1.7f, z);
      yaw_ = glm::degrees(a);
      pitch_ = -6.0f;
    }
  }
  foliage_.resolveCollision(cameraPos_); // don't start inside a tree trunk
}


