#include "Gui.h"
#include "../EngineCore.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <vector>

namespace nut::detail {

GUI::GUI(EngineCore* engine) : engine_(engine), window_(nullptr), initialized_(false) {}

GUI::~GUI() {
    if (initialized_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
}

bool GUI::init(GLFWwindow* window) {
    if (initialized_) return true;
    window_ = window;

    // Create ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    // Style
    ImGui::StyleColorsDark();

    // Backends
    if (!ImGui_ImplGlfw_InitForOpenGL(window_, true)) {
        std::cerr << "Failed to init ImGui GLFW backend" << std::endl; return false;
    }

    if (!ImGui_ImplOpenGL3_Init("#version 330")) {
        std::cerr << "Failed to init ImGui OpenGL3 backend" << std::endl; return false;
    }

    initialized_ = true;
    return true;
}

// Lists loadable skies: equirect images in <assets>/panoramas + <assets>/skybox, and cube-face folders
static std::vector<std::string> scanSkies(const EngineCore& engine) {
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& dir : {engine.assetPath("panoramas"), engine.assetPath("skybox")}) {
        for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            const fs::path& p = it->path();
            std::string ext = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (it->is_directory(ec)) {
                if (fs::exists(p / "right.png", ec) || fs::exists(p / "right.bmp", ec) || fs::exists(p / "right.jpg", ec))
                    out.push_back(p.string());
            } else if (ext == ".hdr" || ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") {
                // Skip helper images that are not panoramas
                std::string name = p.filename().string();
                if (name.find("UV") != std::string::npos || name == "pz.png") continue;
                out.push_back(p.string());
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

void GUI::drawSkyPanel() {
    if (!ImGui::CollapsingHeader("Sky / Panorama", ImGuiTreeNodeFlags_DefaultOpen)) return;

    static std::vector<std::string> skies = scanSkies(*engine_);
    static int selected = -1;
    const std::string& current = engine_->getPanoramaPath();
    if (selected < 0) {
        for (size_t i = 0; i < skies.size(); ++i) if (skies[i] == current) selected = (int)i;
    }

    auto label = [](const std::string& p) { return std::filesystem::path(p).filename().string(); };
    std::string preview = selected >= 0 && selected < (int)skies.size() ? label(skies[selected])
                        : (current.empty() ? std::string("Procedural sky") : label(current));
    if (ImGui::BeginCombo("Sky", preview.c_str())) {
        if (ImGui::Selectable("Procedural sky", current.empty())) { engine_->panorama(""); selected = -1; }
        for (size_t i = 0; i < skies.size(); ++i) {
            bool isSel = (int)i == selected;
            if (ImGui::Selectable(label(skies[i]).c_str(), isSel)) {
                if (engine_->panorama(skies[i])) selected = (int)i;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", skies[i].c_str());
            if (isSel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Rescan")) { skies = scanSkies(*engine_); selected = -1; }

    // Manual path (folder with right/left/top/bottom/front/back, or a single equirect image)
    static char pbuf[512] = "";
    ImGui::InputTextWithHint("##skypath", "custom path (.hdr/.png/.jpg or cube-face folder)", pbuf, sizeof(pbuf));
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        if (engine_->panorama(pbuf)) selected = -1;
    }

    Skybox& sky = engine_->sky();
    if (sky.hasCubemap()) {
        ImGui::TextDisabled("%s panorama%s", sky.isHDR() ? "HDR" : "LDR",
                            sky.analysis().hasSun ? " - sun detected" : "");
        ImGui::SliderFloat("Sky Exposure", &sky.exposure, 0.02f, 8.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
        ImGui::SameLine();
        if (ImGui::SmallButton("Auto")) sky.exposure = sky.analysis().autoExposure;
        ImGui::SliderFloat("Rotation", &sky.rotationDeg, 0.0f, 360.0f, "%.0f deg");
        ImGui::SliderFloat("Blur", &sky.blur, 0.0f, 6.0f);
        ImGui::Checkbox("Sun & light from sky", &engine_->sunFromSky());
        if (!engine_->sunFromSky()) {
            ImGui::SliderFloat("Sun Elevation", &engine_->sunElevation(), -5.0f, 90.0f, "%.0f deg");
            ImGui::SliderFloat("Sun Direction", &engine_->sunAzimuth(), 0.0f, 360.0f, "%.0f deg");
            ImGui::SliderFloat("Sun Intensity", &engine_->sunIntensity(), 0.1f, 2.0f);
            ImGui::ColorEdit3("Sun Colour", &engine_->sunTint().x);
        }
        ImGui::SameLine();
        ImGui::Checkbox("Fog from sky", &engine_->fogFromSky());
    }
    ImGui::Checkbox("Fill below horizon", &sky.horizonFill);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hide the panorama's floor behind the horizon haze");

    ImGui::Checkbox("Procedural Clouds", &engine_->cloudEnabled());
    if (engine_->cloudEnabled()) {
        ImGui::SliderFloat("Cloud Speed", &engine_->cloudSpeed(), 0.0f, 0.5f);
        ImGui::SliderFloat("Cloud Scale", &engine_->cloudScale(), 0.2f, 4.0f);
        ImGui::SliderFloat("Cloud Opacity", &engine_->cloudOpacity(), 0.0f, 1.0f);
    }
}

void GUI::renderOverlayMessage(const std::string& text, const std::string& detail) {
    if (!initialized_) return;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.75f);
    ImGui::Begin("##overlay", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
    ImGui::TextUnformatted(text.c_str());
    if (!detail.empty()) ImGui::TextDisabled("%s", detail.c_str());
    ImGui::End();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void GUI::drawGraphicsPanel() {
    if (!ImGui::CollapsingHeader("Graphics & Performance", ImGuiTreeNodeFlags_DefaultOpen)) return;
    GraphicsSettings& G = engine_->graphics();
    const GpuInfo& gpu = engine_->gpu();
    ImGui::TextWrapped("GPU: %s", gpu.renderer.c_str());
    ImGui::TextDisabled("%s, %s, OpenGL %s", gpu.vendorName().c_str(),
                        gpu.vramMB > 0 ? (std::to_string(gpu.vramMB) + " MB").c_str() : "VRAM unknown", gpu.version.c_str());
    int tier = G.tier;
    const char* tiers[] = { "Potato", "Low", "Medium", "High" };
    if (ImGui::Combo("Quality", &tier, tiers, 4)) engine_->applyTier(tier, "chosen in the settings");
    ImGui::Checkbox("Adapt quality to the GPU automatically", &G.autoTier);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drops to a lower tier when even the lowest resolution can't hold the target FPS");
    if (!engine_->lastQualityChange().empty()) ImGui::TextDisabled("Last change: %s", engine_->lastQualityChange().c_str());
    if (ImGui::Button("Re-run GPU benchmark")) engine_->requestBenchmark();
    ImGui::SameLine();
    ImGui::TextDisabled("(details in gpu_report.txt)");

    // Live numbers: resolution scale and GPU time per pass
    const GpuTimers& T = engine_->gpuTimers();
    ImGui::Text("Render scale %.0f%%   GPU %.1f ms", engine_->renderScale() * 100.0f, T.totalMs());
    if (ImGui::TreeNode("GPU time per pass")) {
        for (const auto& n : T.names()) ImGui::Text("%-12s %5.2f ms", n.c_str(), T.ms(n));
        ImGui::Text("terrain triangles %d, trees %d full + %d billboards", engine_->terrainTriangles(),
                    engine_->treesDrawn(), engine_->impostorsDrawn());
        ImGui::TreePop();
    }

    ImGui::Checkbox("Auto Resolution", &G.autoResolution);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lowers the 3D resolution when needed to hold the target FPS");
    if (G.autoResolution) {
        ImGui::SliderFloat("Target FPS", &G.targetFps, 30.0f, 144.0f, "%.0f");
        ImGui::SliderFloat("Min Scale", &G.minScale, 0.4f, 1.0f, "%.2f");
    } else {
        ImGui::SliderFloat("Render Scale", &G.renderScale, 0.4f, 1.0f, "%.2f");
    }
    ImGui::SliderFloat("Terrain Detail (m)", &G.terrainLodDistance, 40.0f, 300.0f, "%.0f");

    if (ImGui::TreeNode("Lighting & Atmosphere")) {
        ImGui::Checkbox("Sun Shadows", &G.shadows);
        if (G.shadows) ImGui::SliderFloat("Shadow Strength", &G.shadowStrength, 0.0f, 1.0f);
        ImGui::SliderFloat("Valley Fog", &G.fogHeightDensity, 0.0f, 0.02f, "%.4f");
        ImGui::SliderFloat("Fog Height Falloff", &G.fogHeightFalloff, 0.005f, 0.2f, "%.3f");
        ImGui::SliderFloat("Sun Glow in Haze", &G.sunGlow, 0.0f, 1.0f);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Post Effects")) {
        ImGui::Checkbox("Bloom", &G.bloom);
        if (G.bloom) {
            ImGui::SliderFloat("Bloom Intensity", &G.bloomIntensity, 0.0f, 1.0f);
            ImGui::SliderFloat("Bloom Threshold", &G.bloomThreshold, 0.5f, 1.2f);
        }
        ImGui::Checkbox("Sun Rays", &G.godRays);
        if (G.godRays) ImGui::SliderFloat("Sun Ray Intensity", &G.godRayIntensity, 0.0f, 1.5f);
        ImGui::Checkbox("FXAA (anti-aliasing)", &G.fxaa);
        ImGui::SliderFloat("Sharpen", &G.sharpen, 0.0f, 1.0f);
        ImGui::Checkbox("Vignette", &G.vignette);
        ImGui::Checkbox("Underwater Wobble", &G.underwaterWobble);
        ImGui::SliderFloat("Exposure", &G.exposure, 0.5f, 1.8f);
        ImGui::SliderFloat("Contrast", &G.contrast, 0.8f, 1.4f);
        ImGui::SliderFloat("Saturation", &G.saturation, 0.5f, 1.6f);
        ImGui::SliderFloat("Warmth", &G.warmth, -0.2f, 0.2f);
        ImGui::TreePop();
    }
}

void GUI::drawMapPanel() {
    if (!ImGui::CollapsingHeader("Map", ImGuiTreeNodeFlags_DefaultOpen)) return;
    int current = engine_->currentMap();
    if (ImGui::BeginCombo("Current Map", EngineCore::mapName(current))) {
        for (int i = 0; i < EngineCore::mapCount(); ++i) {
            if (i == EngineCore::mapCount() - 1) ImGui::Separator();   // the island presets, then Sponza
            if (ImGui::Selectable(EngineCore::mapName(i), i == current)) engine_->requestMap(i);
            if (i == current) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (!engine_->inSponza()) {
        ImGui::TextDisabled("Procedural island (a new one per seed)");
        return;
    }
    const Sponza& s = engine_->sponza();
    ImGui::TextDisabled("Crytek Sponza: %d triangles, %d materials, %d draws", s.triangleCount(), s.materialCount(),
                        s.lastDrawCalls());
    ImGui::SliderFloat("Bounce Light", &engine_->sponzaIndirect(), 0.0f, 3.0f);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sunlight reflected off the walls and floor (baked; re-baked when the sun moves)");
    if (s.bakingIndirect()) { ImGui::SameLine(); ImGui::TextDisabled("baking..."); }
    ImGui::Checkbox("Eye adaptation", &engine_->sponzaAutoExposure());
    ImGui::SameLine();
    ImGui::TextDisabled("exposure x%.2f", engine_->sponzaExposure());
    if (ImGui::Button("Back to the entrance")) engine_->teleportToSpawn();
    ImGui::TextDisabled("Tip: Sky / Panorama -> turn off 'Sun & light from sky' to move the sun");
}

void GUI::drawVillagePanel() {
    if (!ImGui::CollapsingHeader("Village", ImGuiTreeNodeFlags_DefaultOpen)) return;
    const Village& v = engine_->village();
    if (!v.active()) {
        ImGui::TextDisabled("No village on this terrain (needs a dry, gentle site)");
    } else {
        ImGui::Text("%d houses, %d streets, %d lights", v.houseCount(), (int)v.streets().size(), (int)v.lights().size());
        if (ImGui::Button("Go to the village")) engine_->teleportToVillage();
    }
    ImGui::Checkbox("Lamps, lanterns & fires", &engine_->villageLamps());
    ImGui::SliderFloat("Light Intensity", &engine_->lampIntensity(), 0.0f, 3.0f);
    if (ImGui::Button("New village (new seed)")) {
        engine_->terrainParams().seed += 1;
        engine_->regenerateVillage();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Regenerates the island with the next seed (the village is planned into the terrain)");
}

void GUI::drawFoliagePanel() {
    if (!ImGui::CollapsingHeader("Grass & Trees", ImGuiTreeNodeFlags_DefaultOpen)) return;
    FoliageParams& F = engine_->foliageParams();
    ImGui::Checkbox("Grass", &F.grassEnabled);
    if (F.grassEnabled) {
        ImGui::SliderFloat("Grass Density", &F.grassDensity, 0.3f, 2.5f, "%.1fx");
        ImGui::SliderFloat("Grass Distance (m)", &F.grassRadius, 12.0f, 90.0f, "%.0f");
        ImGui::SliderFloat("Grass Height (m)", &F.grassHeight, 0.15f, 1.5f);
        ImGui::Checkbox("Wildflowers", &F.flowers);
    }
    ImGui::SliderFloat("Wind Strength", &F.windStrength, 0.0f, 2.0f);
    ImGui::Separator();
    bool replant = ImGui::Checkbox("Trees", &F.treesEnabled);
    if (F.treesEnabled) {
        replant |= ImGui::SliderFloat("Forest Density", &F.treeDensity, 0.0f, 1.0f) && !ImGui::IsMouseDown(0);
        if (ImGui::IsItemDeactivatedAfterEdit()) replant = true;
        ImGui::SliderFloat("Tree Draw Distance (m)", &F.treeDistance, 100.0f, 2000.0f, "%.0f");
        ImGui::SliderFloat("Full Detail Trees (m)", &F.treeDetailDistance, 20.0f, 400.0f, "%.0f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Beyond this distance trees are drawn as lit billboards (much faster)");
        ImGui::Text("%d trees", engine_->getTreeCount());
        ImGui::SameLine();
        if (ImGui::SmallButton("Replant")) replant = true;
    }
    if (replant) engine_->replantTrees();
}

void GUI::drawTerrainPanel() {
    TerrainParams& P = engine_->terrainParams();
    static bool autoRegen = false;
    static int preset = 0;
    bool changed = false, force = false;

    if (!ImGui::CollapsingHeader("Procedural Terrain", ImGuiTreeNodeFlags_DefaultOpen)) return;

    int nPresets = 0;
    const char* const* names = islandPresetNames(nPresets);
    ImGui::Combo("Preset", &preset, names, nPresets);
    ImGui::SameLine();
    if (ImGui::Button("Apply")) engine_->requestMap(preset);

    ImGui::InputInt("Seed", &P.seed);
    ImGui::SameLine();
    if (ImGui::Button("Random")) { P.seed = (int)(ImGui::GetTime() * 1000.0) ^ (rand() & 0xffff); force = true; }

    if (ImGui::TreeNodeEx("Shape", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat("Frequency", &P.frequency, 0.001f, 0.02f, "%.4f", ImGuiSliderFlags_Logarithmic);
        changed |= ImGui::SliderInt("Octaves", &P.octaves, 1, 10);
        changed |= ImGui::SliderFloat("Persistence", &P.persistence, 0.2f, 0.8f);
        changed |= ImGui::SliderFloat("Lacunarity", &P.lacunarity, 1.5f, 3.5f);
        changed |= ImGui::SliderFloat("Ridged Mountains", &P.ridgeAmount, 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Domain Warp", &P.warpStrength, 0.0f, 200.0f);
        changed |= ImGui::SliderFloat("Height Curve", &P.heightPower, 0.5f, 3.5f);
        changed |= ImGui::SliderFloat("Terracing", &P.terraceStrength, 0.0f, 1.0f);
        changed |= ImGui::SliderInt("Terrace Steps", &P.terraceSteps, 2, 20);
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Island", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::SliderFloat("Island Strength", &P.islandStrength, 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Island Size", &P.islandRadius, 0.1f, 0.6f);
        changed |= ImGui::SliderFloat("Coastline Roughness", &P.coastNoise, 0.0f, 1.5f);
        changed |= ImGui::SliderFloat("Ocean Depth", &P.seaDepth, 0.05f, 0.5f);
        changed |= ImGui::Checkbox("No Lakes (fill inland basins)", &P.fillLakes);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Size")) {
        if (ImGui::InputInt("Terrain Size", &engine_->terrainSize())) engine_->terrainSize() = std::max(engine_->terrainSize(), 2);
        ImGui::InputFloat("Terrain Scale", &engine_->terrainScale());
        ImGui::InputFloat("Height Scale", &engine_->heightScale());
        ImGui::InputFloat("Texture Tile", &engine_->textureTile());
        ImGui::TextDisabled("Applied on Regenerate Terrain");
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Erosion")) {
        changed |= ImGui::SliderInt("Hydraulic Droplets", &P.erosionIterations, 0, 300000);
        changed |= ImGui::SliderFloat("Erode Strength", &P.erosionStrength, 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Deposit Strength", &P.depositStrength, 0.0f, 1.0f);
        changed |= ImGui::SliderInt("Thermal Passes", &P.thermalIterations, 0, 30);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Ocean")) {
        changed |= ImGui::Checkbox("Enable Ocean", &P.waterEnabled);
        changed |= ImGui::SliderFloat("Sea Level", &P.waterLevel, 0.0f, 0.9f);
        // Waves update live (no regeneration needed)
        ImGui::SliderFloat("Wave Height (m)", &P.waveHeight, 0.0f, 3.0f);
        ImGui::SliderFloat("Wave Length (m)", &P.waveLength, 8.0f, 120.0f);
        ImGui::SliderFloat("Choppiness", &P.choppiness, 0.0f, 1.0f);
        ImGui::SliderFloat("Wind Direction", &P.windAngle, 0.0f, 360.0f, "%.0f deg");
        ImGui::SliderFloat("Wave Speed", &P.waveSpeed, 0.0f, 3.0f);
        ImGui::SliderFloat("Water Opacity", &P.waterOpacity, 0.2f, 1.0f);
        ImGui::ColorEdit3("Shallow Color", &P.waterShallow.x);
        ImGui::ColorEdit3("Deep Color", &P.waterDeep.x);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Materials")) {
        ImGui::SliderFloat("Beach Width", &P.beachWidth, 0.0f, 0.15f);
        ImGui::SliderFloat("Rock Slope", &P.rockSlope, 0.05f, 0.9f);
        ImGui::SliderFloat("Snow Line", &P.snowLine, 0.2f, 1.2f);
        ImGui::SliderFloat("Snow Blend", &P.snowBlend, 0.01f, 0.4f);
        ImGui::SliderFloat("Texture Scale", &P.textureScale, 0.3f, 4.0f);
        ImGui::ColorEdit3("Grass Tint", &P.grassTint.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        ImGui::ColorEdit3("Sand Tint", &P.sandTint.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        ImGui::ColorEdit3("Rock Tint", &P.rockTint.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        ImGui::ColorEdit3("Snow Tint", &P.snowTint.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Atmosphere")) {
        ImGui::SliderFloat("Fog Density", &P.fogDensity, 0.0f, 0.01f, "%.4f");
        ImGui::ColorEdit3("Fog Color", &P.fogColor.x);
        ImGui::TreePop();
    }

    ImGui::Checkbox("Auto Regenerate (slow with erosion)", &autoRegen);
    // While dragging a slider just remember that something changed; rebuild on release
    static bool pending = false;
    if (changed && autoRegen) pending = true;
    if (!autoRegen) pending = false;
    bool rebuild = force || (pending && !ImGui::IsMouseDown(0));
    if (ImGui::Button("Regenerate Terrain")) rebuild = true;
    if (rebuild) { pending = false; engine_->regenerateTerrain(); }
}

// Minimap: bottom-left, north up, the whole island with the player's position and view direction.
// Press M to switch between small and large.
void GUI::drawMinimap() {
    if (engine_->inSponza()) {   // no island map indoors: just the footer
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(14.0f, io.DisplaySize.y - 14.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.45f);
        ImGui::Begin("##mapfooter", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::TextDisabled("Sponza Palace   Tab menu   %.0f fps", io.Framerate);
        ImGui::End();
        return;
    }
    const Terrain& terrain = engine_->terrain();
    GLuint tex = terrain.minimapTexture();
    if (!tex) return;

    static bool large = false;
    if (ImGui::IsKeyPressed(ImGuiKey_M, false) && !ImGui::GetIO().WantTextInput) large = !large;

    ImGuiIO& io = ImGui::GetIO();
    float size = std::floor(std::min(io.DisplaySize.x, io.DisplaySize.y) * (large ? 0.6f : 0.26f));
    const float margin = 14.0f, pad = 6.0f, footer = 18.0f;
    ImVec2 winPos(margin, io.DisplaySize.y - margin - size - 2 * pad - footer);
    ImGui::SetNextWindowPos(winPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(size + 2 * pad, size + 2 * pad + footer), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.45f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    ImGui::Begin("##minimap", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1(p0.x + size, p0.y + size);
    dl->AddImageRounded(ImTextureRef((ImTextureID)tex), p0, p1, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 6.0f);
    dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 90), 6.0f, 0, 1.5f);

    // World -> map. Texture u follows +X and v follows +Z, so north (-Z) is up.
    float half = terrain.getHalfExtent();
    const glm::vec3& pos = engine_->getPlayerPos();
    auto toMap = [&](float wx, float wz) {
        return ImVec2(p0.x + (wx / (2.0f * half) + 0.5f) * size, p0.y + (wz / (2.0f * half) + 0.5f) * size);
    };
    ImVec2 c = toMap(pos.x, pos.z);
    // The village: cobbled streets, the plaza and every house (roof colour), true to scale
    const Village& vil = engine_->village();
    if (vil.active()) {
        dl->PushClipRect(p0, p1, true);
        float px = size / (2.0f * half);
        float streetW = std::max(1.5f, 4.2f * px);
        for (const auto& st : vil.streets()) {
            std::vector<ImVec2> pts;
            for (const auto& q : st) pts.push_back(toMap(q.x, q.y));
            dl->AddPolyline(pts.data(), (int)pts.size(), IM_COL32(205, 195, 175, 230), 0, streetW);
        }
        glm::vec3 vc = vil.center();
        dl->AddCircleFilled(toMap(vc.x, vc.z), std::max(2.5f, vil.plazaRadius() * px), IM_COL32(210, 200, 180, 235));
        for (const auto& hs : vil.mapHouses()) {
            glm::vec2 az(-hs.axisX.y, hs.axisX.x);
            glm::vec2 hx = hs.axisX * hs.half.x, hz = az * hs.half.y;
            ImVec2 q[4] = { toMap(hs.center.x - hx.x - hz.x, hs.center.y - hx.y - hz.y), toMap(hs.center.x + hx.x - hz.x, hs.center.y + hx.y - hz.y),
                            toMap(hs.center.x + hx.x + hz.x, hs.center.y + hx.y + hz.y), toMap(hs.center.x - hx.x + hz.x, hs.center.y - hx.y + hz.y) };
            ImU32 roof = IM_COL32((int)(glm::clamp(hs.roof.r, 0.0f, 1.0f) * 255), (int)(glm::clamp(hs.roof.g, 0.0f, 1.0f) * 255),
                                  (int)(glm::clamp(hs.roof.b, 0.0f, 1.0f) * 255), 255);
            dl->AddQuadFilled(q[0], q[1], q[2], q[3], roof);
            if (large) dl->AddQuad(q[0], q[1], q[2], q[3], IM_COL32(60, 30, 20, 160), 1.0f);
        }
        if (!large) dl->AddText(ImVec2(toMap(vc.x, vc.z).x + 6, toMap(vc.x, vc.z).y - 16), IM_COL32(255, 235, 190, 230), "Village");
        dl->PopClipRect();
    }
    c.x = std::clamp(c.x, p0.x + 4.0f, p1.x - 4.0f);   // stay on the edge when out at sea
    c.y = std::clamp(c.y, p0.y + 4.0f, p1.y - 4.0f);

    // Facing direction (same convention as the camera: forward = (cos yaw, sin yaw) on XZ)
    float yaw = glm::radians(engine_->getYaw());
    ImVec2 f(std::cos(yaw), std::sin(yaw));
    ImVec2 r(-f.y, f.x);

    // View cone
    float fov = glm::radians(30.0f), reach = size * 0.16f;
    ImVec2 cl(c.x + (f.x * std::cos(fov) - f.y * std::sin(fov)) * reach, c.y + (f.y * std::cos(fov) + f.x * std::sin(fov)) * reach);
    ImVec2 cr(c.x + (f.x * std::cos(fov) + f.y * std::sin(fov)) * reach, c.y + (f.y * std::cos(fov) - f.x * std::sin(fov)) * reach);
    dl->PushClipRect(p0, p1, true);
    dl->AddTriangleFilled(c, cl, cr, IM_COL32(255, 255, 255, 45));

    // Player arrow with a dark outline so it reads on sand, grass and water
    float a = large ? 11.0f : 8.0f;
    ImVec2 tip(c.x + f.x * a, c.y + f.y * a);
    ImVec2 left(c.x - f.x * a * 0.7f + r.x * a * 0.65f, c.y - f.y * a * 0.7f + r.y * a * 0.65f);
    ImVec2 right(c.x - f.x * a * 0.7f - r.x * a * 0.65f, c.y - f.y * a * 0.7f - r.y * a * 0.65f);
    ImVec2 notch(c.x - f.x * a * 0.3f, c.y - f.y * a * 0.3f);
    dl->AddQuadFilled(tip, right, notch, left, engine_->isSwimming() ? IM_COL32(80, 200, 255, 255) : IM_COL32(255, 70, 50, 255));
    dl->AddQuad(tip, right, notch, left, IM_COL32(20, 20, 20, 220), 1.5f);
    dl->PopClipRect();

    // North marker
    ImVec2 n(p0.x + size * 0.5f, p0.y + 3.0f);
    dl->AddTriangleFilled(ImVec2(n.x, n.y), ImVec2(n.x - 5.0f, n.y + 9.0f), ImVec2(n.x + 5.0f, n.y + 9.0f), IM_COL32(255, 255, 255, 220));
    dl->AddText(ImVec2(n.x - 4.0f, n.y + 10.0f), IM_COL32(255, 255, 255, 230), "N");

    // Footer: altitude above sea level + key hints
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y + 3.0f));
    float alt = pos.y - 1.7f - terrain.getWaterY();
    ImGui::TextColored(ImVec4(1, 1, 1, 0.85f), "%+.0f m", alt);
    ImGui::SameLine();
    ImGui::TextDisabled(large ? " Tab: settings   M: smaller map" : " Tab menu  M map");
    ImGui::SameLine();
    ImGui::TextDisabled(" %.0f fps", ImGui::GetIO().Framerate);
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void GUI::render() {
    if (!initialized_) return;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // HUD: swimming status and oxygen (only shown in the water)
    if (engine_->hudEnabled() && (engine_->isSwimming() || engine_->getOxygen() < 0.999f)) {
        ImGui::SetNextWindowPos(ImVec2(10,10), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.35f);
        ImGui::Begin("HUD", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
        if (engine_->isSwimming()) {
            ImGui::TextColored(ImVec4(0.5f, 0.85f, 1.0f, 1.0f), engine_->isUnderwater() ? "Diving" : "Swimming");
            ImGui::TextDisabled("C/Ctrl dive - Space up - look down + W to dive");
        }
        float o2 = engine_->getOxygen();
        if (o2 < 0.999f || engine_->isUnderwater()) {
            ImVec4 col = o2 > 0.3f ? ImVec4(0.35f, 0.75f, 1.0f, 1.0f) : ImVec4(1.0f, 0.35f, 0.3f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, col);
            ImGui::ProgressBar(o2, ImVec2(160, 0), "Oxygen");
            ImGui::PopStyleColor();
        }
        ImGui::End();
    }

    if (engine_->hudEnabled()) drawMinimap();

    // Settings panel: hidden until TAB is pressed
    if (engine_->isGuiVisible()) {
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 470.0f, 10.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(460.0f, ImGui::GetIO().DisplaySize.y - 20.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Engine Controls  (Tab to hide)");

    // Performance
    {
        ImGuiIO& io = ImGui::GetIO();
        float fps = io.Framerate;
        float ms = fps > 0.0f ? 1000.0f / fps : 0.0f;
        ImGui::Text("FPS: %.1f (%.3f ms)", fps, ms);

        int vsMode = engine_->getVsyncEnabled() ? 1 : 0;
        int prev = vsMode;
        if (ImGui::RadioButton("VSync On", vsMode == 1)) vsMode = 1;
        ImGui::SameLine();
        if (ImGui::RadioButton("VSync Off", vsMode == 0)) vsMode = 0;
        if (vsMode != prev) {
            engine_->vsync(vsMode == 1);
        }
    }

    drawMapPanel();

    ImGui::Separator();

    drawGraphicsPanel();

    ImGui::Separator();

    drawSkyPanel();

    ImGui::Separator();

    // Everything below shapes the island
    if (!engine_->inSponza()) {

    drawTerrainPanel();
    drawFoliagePanel();
    drawVillagePanel();
    }

    ImGui::End();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

} // namespace nut::detail
