#pragma once
#include <isola/Config.hpp>
#include <isola/Export.hpp>
#include <isola/Foliage.hpp>
#include <isola/Graphics.hpp>
#include <isola/Input.hpp>
#include <isola/Map.hpp>
#include <isola/Sky.hpp>
#include <isola/Terrain.hpp>

#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>

namespace isola {

namespace detail {
class EngineCore;
}

/// Passed to the update callback once per frame.
struct FrameInfo {
    float deltaTime = 0.0f;     ///< seconds since the previous frame
    double time = 0.0;          ///< seconds since run() started
    std::uint64_t frame = 0;    ///< frames since run() started
};

/// Rendering statistics of the last frames.
struct FrameStats {
    float fps = 0.0f;
    float gpuMs = 0.0f;         ///< GPU time of the whole frame
    float renderScale = 1.0f;   ///< dynamic resolution: fraction of the window resolution
    QualityTier tier = QualityTier::Medium;
};

/// What the first-person player is doing.
struct PlayerState {
    glm::vec3 position{0.0f};   ///< eye position, metres
    float yaw = 0.0f;           ///< degrees; 0 looks along +X, 90 along +Z
    float pitch = 0.0f;         ///< degrees; positive looks up
    bool swimming = false;
    bool underwater = false;
    float oxygen = 1.0f;        ///< 0..1, drains while diving
};

/// The engine: a window with an OpenGL 3.3 context, the world (a procedural island or the Sponza
/// palace), a first-person player (WASD, mouse look, Space jumps, Shift sprints) and the optional
/// built-in settings panel (Tab).
///
/// One Engine per process. It owns the window and the GL context, so it is neither copyable nor
/// movable; create it on the stack or in a std::unique_ptr.
///
/// \code
/// isola::Engine engine{config};
/// engine.onUpdate([](isola::Engine& e, const isola::FrameInfo& f) { ... });
/// engine.run();
/// \endcode
class ISOLA_API Engine {
public:
    using UpdateCallback = std::function<void(Engine&, const FrameInfo&)>;
    using KeyCallback = std::function<void(Engine&, Key, KeyAction)>;

    /// Creates the window, compiles the shaders and builds the start map.
    /// \throws isola::Error when the window, OpenGL context, shaders or start map can't be set up.
    explicit Engine(const EngineConfig& config = {});
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

    // --- Main loop -------------------------------------------------------------------------
    /// Runs until the window is closed or quit() is called.
    void run();
    /// Leaves run() after the current frame.
    void quit() noexcept;

    /// Called every frame after the player has moved and before the frame is drawn.
    void onUpdate(UpdateCallback callback);
    /// Called for every key press / repeat / release (not while typing into the settings panel).
    void onKey(KeyCallback callback);
    [[nodiscard]] bool isKeyDown(Key key) const noexcept;

    // --- World -----------------------------------------------------------------------------
    /// Switches the world. Islands are generated from their preset (keeping the current seed);
    /// Sponza is loaded from disk the first time.
    /// \throws isola::Error if the map's files can't be loaded.
    void loadMap(Map map);
    [[nodiscard]] Map currentMap() const noexcept;

    /// Island generation settings of the current island; call regenerateWorld() after editing.
    [[nodiscard]] TerrainParams& terrain() noexcept;
    [[nodiscard]] FoliageParams& foliage() noexcept;
    /// Rebuilds the island (terrain, village, trees) from terrain() and moves the player to the
    /// village. No effect on the Sponza map.
    void regenerateWorld();
    /// Village lamps, lanterns and fires.
    void setVillageLights(bool enabled, float intensity = 1.0f) noexcept;

    // --- Sky and sun -----------------------------------------------------------------------
    /// Loads an equirectangular panorama (.hdr, .png, .jpg, .bmp) or a folder of cube faces
    /// (right/left/top/bottom/front/back). An HDR panorama also lights the scene: the sun's
    /// direction and colour are found in the image.
    /// \throws isola::Error if it can't be loaded.
    void setSky(const std::filesystem::path& panorama);
    /// The built-in gradient sky with procedural clouds.
    void setProceduralSky();
    [[nodiscard]] SkySettings skySettings() const;
    void setSkySettings(const SkySettings& settings);
    /// Places the sun by hand (and stops taking it from the sky).
    void setSun(const SunSettings& sun) noexcept;
    /// Takes the sun direction and colour from the HDR sky again.
    void useSunFromSky() noexcept;

    // --- Player ----------------------------------------------------------------------------
    [[nodiscard]] PlayerState player() const noexcept;
    /// Moves the player's eye to `position`, looking along yaw / pitch (degrees).
    void teleport(const glm::vec3& position, float yaw, float pitch = 0.0f) noexcept;
    /// Back to the map's start point (the village street, or the palace entrance).
    void respawn() noexcept;

    // --- Graphics --------------------------------------------------------------------------
    /// Live rendering settings (post-processing, shadows, resolution...).
    [[nodiscard]] GraphicsSettings& graphics() noexcept;
    /// Applies a quality tier's defaults (recompiling shaders if their complexity changes) and
    /// turns automatic tier changes off.
    void setQualityTier(QualityTier tier);
    void setVSync(bool enabled) noexcept;
    [[nodiscard]] bool vsync() const noexcept;
    [[nodiscard]] FrameStats stats() const noexcept;

    // --- User interface --------------------------------------------------------------------
    /// Shows / hides the settings panel (frees / captures the mouse).
    void setSettingsPanelVisible(bool visible) noexcept;
    [[nodiscard]] bool settingsPanelVisible() const noexcept;

private:
    std::unique_ptr<detail::EngineCore> core_;
};

} // namespace isola
