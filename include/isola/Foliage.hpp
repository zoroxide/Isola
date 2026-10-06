#pragma once

namespace isola {

/// Grass and tree settings. Changes to grass and wind apply live; tree placement changes apply
/// when the world is regenerated.
struct FoliageParams {
    // Grass (generated on the GPU around the player every frame)
    bool  grassEnabled = true;
    float grassDensity = 1.0f;     // multiplier on the number of blades
    float grassRadius = 45.0f;     // metres; blades thin out towards this distance
    float grassHeight = 0.5f;      // metres (average blade)
    bool  flowers = true;

    // Trees (placed once per terrain generation)
    bool  treesEnabled = true;
    float treeDensity = 0.55f;     // 0..1, how much of the grassland becomes forest
    float treeDistance = 1200.0f;  // draw distance in metres
    float treeDetailDistance = 90.0f; // beyond this, trees are drawn as billboards (impostors)

    // Wind (direction follows the ocean wind)
    float windStrength = 0.6f;
};

} // namespace isola
