// nut::Engine: the public facade over detail::EngineCore.
#include <nut/Engine.hpp>
#include <nut/Error.hpp>

#include "EngineCore.h"

#include <GLFW/glfw3.h>

#include <string>
#include <utility>

namespace nut {

// nut::Key / KeyAction values are GLFW's, so input passes through without a lookup table
static_assert(static_cast<int>(Key::Space) == GLFW_KEY_SPACE);
static_assert(static_cast<int>(Key::Num0) == GLFW_KEY_0 && static_cast<int>(Key::Num9) == GLFW_KEY_9);
static_assert(static_cast<int>(Key::A) == GLFW_KEY_A && static_cast<int>(Key::Z) == GLFW_KEY_Z);
static_assert(static_cast<int>(Key::Escape) == GLFW_KEY_ESCAPE && static_cast<int>(Key::Delete) == GLFW_KEY_DELETE);
static_assert(static_cast<int>(Key::Right) == GLFW_KEY_RIGHT && static_cast<int>(Key::End) == GLFW_KEY_END);
static_assert(static_cast<int>(Key::CapsLock) == GLFW_KEY_CAPS_LOCK && static_cast<int>(Key::Pause) == GLFW_KEY_PAUSE);
static_assert(static_cast<int>(Key::F1) == GLFW_KEY_F1 && static_cast<int>(Key::F12) == GLFW_KEY_F12);
static_assert(static_cast<int>(Key::LeftShift) == GLFW_KEY_LEFT_SHIFT &&
              static_cast<int>(Key::RightSuper) == GLFW_KEY_RIGHT_SUPER);
static_assert(static_cast<int>(KeyAction::Press) == GLFW_PRESS && static_cast<int>(KeyAction::Release) == GLFW_RELEASE &&
              static_cast<int>(KeyAction::Repeat) == GLFW_REPEAT);

Engine::Engine(const EngineConfig& config) : core_(std::make_unique<detail::EngineCore>()) {
    std::string error;
    if (!core_->init(config, error)) {
        core_.reset();
        throw Error(error);
    }
}

Engine::~Engine() = default;

// --- Main loop -------------------------------------------------------------------------------
void Engine::run() { core_->run(); }

void Engine::quit() noexcept { core_->requestQuit(); }

void Engine::onUpdate(UpdateCallback callback) {
    if (!callback) {
        core_->updateHook = nullptr;
        return;
    }
    core_->updateHook = [this, cb = std::move(callback)](float dt, double time, std::uint64_t frame) {
        cb(*this, FrameInfo{dt, time, frame});
    };
}

void Engine::onKey(KeyCallback callback) {
    if (!callback) {
        core_->keyHook = nullptr;
        return;
    }
    core_->keyHook = [this, cb = std::move(callback)](int key, int action) {
        cb(*this, static_cast<Key>(key), static_cast<KeyAction>(action));
    };
}

bool Engine::isKeyDown(Key key) const noexcept { return core_->isKeyDown(static_cast<int>(key)); }

// --- World -----------------------------------------------------------------------------------
void Engine::loadMap(Map map) {
    if (!core_->selectMap(static_cast<int>(map)))
        throw Error("Nut: could not load the map " + std::string(toString(map)));
}

Map Engine::currentMap() const noexcept { return static_cast<Map>(core_->currentMap()); }

TerrainParams& Engine::terrain() noexcept { return core_->terrainParams(); }

FoliageParams& Engine::foliage() noexcept { return core_->foliageParams(); }

void Engine::regenerateWorld() {
    if (!core_->inSponza())
        core_->regenerateTerrain();
}

void Engine::setVillageLights(bool enabled, float intensity) noexcept {
    core_->villageLamps() = enabled;
    core_->lampIntensity() = intensity;
}

// --- Sky and sun -----------------------------------------------------------------------------
void Engine::setSky(const std::filesystem::path& panorama) {
    if (panorama.empty()) {
        setProceduralSky();
        return;
    }
    if (!core_->panorama(panorama.string()))
        throw Error("Nut: could not load the sky " + panorama.string());
}

void Engine::setProceduralSky() { core_->panorama(""); }

SkySettings Engine::skySettings() const {
    const detail::Skybox& sky = core_->sky();
    SkySettings s;
    s.exposure = sky.exposure;
    s.rotation = sky.rotationDeg;
    s.blur = sky.blur;
    s.clouds = core_->cloudEnabled();
    s.cloudSpeed = core_->cloudSpeed();
    s.cloudScale = core_->cloudScale();
    s.cloudOpacity = core_->cloudOpacity();
    return s;
}

void Engine::setSkySettings(const SkySettings& settings) {
    detail::Skybox& sky = core_->sky();
    sky.exposure = settings.exposure;
    sky.rotationDeg = settings.rotation;
    sky.blur = settings.blur;
    core_->cloudEnabled() = settings.clouds;
    core_->cloudSpeed() = settings.cloudSpeed;
    core_->cloudScale() = settings.cloudScale;
    core_->cloudOpacity() = settings.cloudOpacity;
}

void Engine::setSun(const SunSettings& sun) noexcept {
    core_->sunFromSky() = false;
    core_->sunElevation() = sun.elevation;
    core_->sunAzimuth() = sun.azimuth;
    core_->sunIntensity() = sun.intensity;
    core_->sunTint() = sun.tint;
}

void Engine::useSunFromSky() noexcept { core_->sunFromSky() = true; }

// --- Player ----------------------------------------------------------------------------------
PlayerState Engine::player() const noexcept {
    PlayerState p;
    p.position = core_->getPlayerPos();
    p.yaw = core_->getYaw();
    p.pitch = core_->getPitch();
    p.swimming = core_->isSwimming();
    p.underwater = core_->isUnderwater();
    p.oxygen = core_->getOxygen();
    return p;
}

void Engine::teleport(const glm::vec3& position, float yaw, float pitch) noexcept {
    core_->teleport(position, yaw, pitch);
}

void Engine::respawn() noexcept { core_->teleportToSpawn(); }

// --- Graphics --------------------------------------------------------------------------------
GraphicsSettings& Engine::graphics() noexcept { return core_->graphics(); }

void Engine::setQualityTier(QualityTier tier) {
    core_->applyTier(static_cast<int>(tier), "set by the application");
    core_->graphics().autoTier = false;
}

void Engine::setVSync(bool enabled) noexcept { core_->vsync(enabled); }

bool Engine::vsync() const noexcept { return core_->getVsyncEnabled(); }

FrameStats Engine::stats() const noexcept {
    FrameStats s;
    s.fps = core_->fps();
    s.gpuMs = core_->gpuTimers().totalMs();
    s.renderScale = core_->renderScale();
    s.tier = static_cast<QualityTier>(core_->graphics().tier);
    return s;
}

// --- User interface --------------------------------------------------------------------------
void Engine::setSettingsPanelVisible(bool visible) noexcept { core_->setGuiVisible(visible); }

bool Engine::settingsPanelVisible() const noexcept { return core_->isGuiVisible(); }

} // namespace nut
