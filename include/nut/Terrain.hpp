#pragma once
#include <glm/vec3.hpp>

namespace nut {

/// All settings of the procedural island: shape, erosion, ocean, ground materials and fog.
/// Edit them through Engine::terrain(), then call Engine::regenerateWorld() (the ocean, materials
/// and fog also update live).
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
};

} // namespace nut
