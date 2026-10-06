#pragma once
#include <isola/Graphics.hpp>
#include <isola/Map.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace isola {

struct WindowConfig {
    std::string title = "Isola";
    int width = 1280;           ///< ignored when fullscreen
    int height = 720;
    bool fullscreen = false;    ///< fullscreen on the primary monitor at its desktop resolution
    bool vsync = true;
};

/// Where the engine finds its runtime files. Relative paths are resolved against the current
/// working directory.
struct ResourcePaths {
    std::filesystem::path shaders = "shaders";   ///< the engine's GLSL sources
    std::filesystem::path assets = "assets";     ///< textures, panoramas, maps
    std::filesystem::path data = ".";            ///< writable: graphics.cfg, gpu_report.txt
};

struct EngineConfig {
    WindowConfig window;
    ResourcePaths paths;

    Map startMap = Map::BigIsland;
    std::optional<int> seed;                 ///< island seed; default: the preset's own
    std::filesystem::path sky;               ///< panorama (.hdr/.png/.jpg) or cube-face folder; empty: procedural sky

    /// Fixed quality tier. Unset: benchmark the GPU on first start (result cached in
    /// `<data>/graphics.cfg`) and raise / lower the tier at runtime to hold the target frame rate.
    std::optional<QualityTier> quality;

    bool settingsPanel = true;   ///< Tab opens the built-in settings panel
    bool hud = true;             ///< minimap, swimming / oxygen display, FPS
    bool quitOnEscape = true;
};

} // namespace isola
