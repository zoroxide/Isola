#pragma once

// Rendering quality and post-processing settings (edited live in the GUI).
// A quality tier (0 potato .. 3 high) bundles everything below, including how complex the
// shaders are; the engine picks the tier for the GPU it runs on (see GpuProfile).
struct GraphicsSettings {
    // Quality tier and automatic adaptation
    int   tier = 2;                // 0 potato, 1 low, 2 medium, 3 high
    bool  autoTier = true;         // lower / raise the tier when the GPU can't / easily can keep up

    // Resolution: the 3D scene is rendered at renderScale x window size and upscaled.
    // With autoResolution the scale adapts every frame to hold targetFps.
    bool  autoResolution = true;
    float targetFps = 60.0f;
    float renderScale = 1.0f;      // manual scale (used when autoResolution is off)
    float minScale = 0.5f;
    float maxScale = 1.0f;

    // Terrain detail: distance (m) where terrain chunks start dropping resolution
    float terrainLodDistance = 110.0f;

    // Lighting
    bool  shadows = true;
    float shadowStrength = 0.85f;
    int   villageShadowRes = 2048;   // building shadow map size
    int   maxOutdoorLights = 6;      // street lamps lighting terrain / grass / trees
    int   maxHouseLights = 4;        // interior lamps + fire per house
    float anisotropy = 4.0f;         // texture filtering (expensive on older GPUs)

    // Atmosphere
    float fogHeightDensity = 0.0035f;  // extra haze in valleys / over the sea
    float fogHeightFalloff = 0.05f;
    float sunGlow = 0.22f;             // haze lit up around the sun

    // Vegetation budget for this tier (applied to the Grass & Trees settings)
    bool  grass = true;
    float grassDensity = 1.0f;
    float grassRadius = 45.0f;
    float treeDetailDistance = 90.0f;
    float treeDistance = 1200.0f;

    // Post-processing
    bool  bloom = true;
    float bloomIntensity = 0.25f;
    float bloomThreshold = 0.9f;
    bool  godRays = true;
    float godRayIntensity = 0.35f;
    bool  fxaa = true;
    float sharpen = 0.35f;
    bool  vignette = true;
    float vignetteStrength = 0.3f;
    float exposure = 1.0f;
    float contrast = 1.06f;
    float saturation = 1.08f;
    float warmth = 0.03f;
    bool  underwaterWobble = true;

    // Shader complexity for this tier (the QUALITY define)
    int shaderQuality() const { return tier; }

    static const char* tierName(int t) {
        static const char* names[4] = { "Potato", "Low", "Medium", "High" };
        return names[t < 0 ? 0 : (t > 3 ? 3 : t)];
    }

    static GraphicsSettings forTier(int t) {
        GraphicsSettings g;
        g.tier = t < 0 ? 0 : (t > 3 ? 3 : t);
        switch (g.tier) {
        case 0:   // Potato: very old / low-end GPUs (e.g. Radeon HD 5000-8000 OEM, 64-bit DDR3)
            g.minScale = 0.4f; g.maxScale = 0.7f; g.terrainLodDistance = 45.0f;
            g.villageShadowRes = 1024; g.maxOutdoorLights = 0; g.maxHouseLights = 1; g.anisotropy = 1.0f;
            g.grass = true; g.grassDensity = 0.35f; g.grassRadius = 18.0f; g.treeDetailDistance = 30.0f; g.treeDistance = 600.0f;
            g.bloom = false; g.godRays = false; g.fxaa = false; g.sharpen = 0.55f; g.vignette = false;
            g.fogHeightDensity = 0.0f;
            break;
        case 1:   // Low
            g.minScale = 0.45f; g.maxScale = 0.85f; g.terrainLodDistance = 65.0f;
            g.villageShadowRes = 1024; g.maxOutdoorLights = 2; g.maxHouseLights = 2; g.anisotropy = 2.0f;
            g.grassDensity = 0.6f; g.grassRadius = 28.0f; g.treeDetailDistance = 50.0f; g.treeDistance = 850.0f;
            g.bloom = false; g.godRays = false; g.fxaa = true; g.sharpen = 0.5f; g.vignette = false;
            break;
        case 2:   // Medium (tuned for Intel HD 620 class)
            g.minScale = 0.5f; g.maxScale = 1.0f;
            break;
        default:  // High
            g.minScale = 0.7f; g.maxScale = 1.0f; g.terrainLodDistance = 170.0f;
            g.maxOutdoorLights = 8; g.anisotropy = 8.0f;
            g.grassDensity = 1.3f; g.grassRadius = 60.0f; g.treeDetailDistance = 140.0f; g.treeDistance = 1600.0f;
            break;
        }
        return g;
    }
    // Backwards compatible name
    static GraphicsSettings preset(int level) { return forTier(level + 1); }
};
