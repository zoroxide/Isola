# **Procedural Terrain Generator Based Game using Moden OpenGL (Nut)**
A beautifull 3D Fixed Terrain Generation (Perlin Noise) based game (graphics engine with movement controls)
Created using Modern OpenGL (GLFW, GLEW, GLM), modern C++ and finally stb_image for image handling and others..

### Thanks to:
**this software was never be produced without these resources:**
 - [learnopengl.com](https://learnopengl.com/)
 - [OGLDEV](https://www.youtube.com/@OGLDEV)
 - CS633 / CS352 Computer Graphics & linear algebra college courses

# Screenshots
### New version

<table>
  <tr>
    <td><img alt="Screenshot From 2026-09-30 11-52-11" src="https://github.com/user-attachments/assets/6d710708-2c6d-4d0d-bae0-645753addcd3" /></td>
    <td><img alt="Screenshot From 2026-09-30 11-54-03" src="https://github.com/user-attachments/assets/df1cdf76-f1b2-4c4d-9ac5-2cea998f0848" /></td>
  </tr>
  <tr>
    <td><img alt="image" src="https://github.com/user-attachments/assets/3b43c0d8-952c-44d3-b254-6d54c94634bd" /></td>
    <td><img alt="Screenshot From 2026-09-30 10-48-17" src="https://github.com/user-attachments/assets/8c3fd254-9e4b-44ee-a6c3-abd552941da7" /></td>
  </tr>
  <tr>
    <td><img alt="image" src="https://github.com/user-attachments/assets/73ca598c-f664-4596-ad7e-60bfce921067" /></td>
    <td><img width="1366" height="768" alt="Screenshot From 2026-10-02 14-20-53" src="https://github.com/user-attachments/assets/5c1396a1-16d3-4dec-a48a-ee6e52727619" /></td>
  </tr>
 <tr>
    <td><img width="1366" height="768" alt="Screenshot From 2026-10-02 14-21-12" src="https://github.com/user-attachments/assets/f1b332d1-1e40-447d-8faa-24db92773c65" />
</td>
    <td><img width="1366" height="768" alt="Screenshot From 2026-10-02 14-25-57" src="https://github.com/user-attachments/assets/c55ea032-1661-461d-a47c-0f89952191a9" />
</td>
  </tr>
</table>

### Old version

<table>
  <tr>
    <td><img alt="image" src="https://github.com/user-attachments/assets/f3ffdeab-faa8-443a-b2bd-3d36c32b81de" /></td>
    <td><img alt="image" src="https://github.com/user-attachments/assets/242c0160-3348-43c3-896a-9da0c6687416" /></td>
  </tr>
  <tr>
    <td><img alt="Screenshot From 2026-09-13 04-54-36" src="https://github.com/user-attachments/assets/08c704ee-2f23-49a2-ac3d-a3ad25abd3ef" /></td>
    <td><img alt="image" src="https://github.com/user-attachments/assets/d0fcbf73-9daa-42e4-8045-116761a2fe4d" /></td>
  </tr>
  <tr>
    <td><img alt="image" src="https://github.com/user-attachments/assets/50be3578-bbae-4739-96e2-42976e7c3efa" /></td>
    <td></td>
  </tr>
</table>

# Using Nut as a library

Nut is a C++17 library: include `<nut/Nut.hpp>`, link `nut::nut`, and the engine gives you a
window, the world (a procedural island or the Sponza palace), a first-person player and the
built-in settings panel. The [demo](examples/demo/main.cpp) is a complete example.

```cpp
#include <nut/Nut.hpp>
#include <iostream>

int main() try {
    nut::EngineConfig config;
    config.window.title = "My Game";
    config.startMap = nut::Map::Archipelago;
    config.sky = "assets/panoramas/kloofendal_48d_partly_cloudy_puresky_4k.hdr";

    nut::Engine engine{config};   // throws nut::Error if it can't start

    engine.onKey([](nut::Engine& e, nut::Key key, nut::KeyAction action) {
        if (key == nut::Key::P && action == nut::KeyAction::Press)
            e.loadMap(nut::Map::SponzaPalace);
    });
    engine.onUpdate([](nut::Engine& e, const nut::FrameInfo&) {
        if (e.player().underwater && e.player().oxygen < 0.2f)
            e.respawn();
    });

    engine.run();
} catch (const nut::Error& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
```

The public API (all in namespace `nut`, headers in [`include/nut`](include/nut)):

| Header | Contents |
| --- | --- |
| `Engine.hpp` | `Engine`: main loop and callbacks, maps, sky and sun, player, graphics, settings panel |
| `Config.hpp` | `EngineConfig`, `WindowConfig`, `ResourcePaths` (where `shaders/` and `assets/` are) |
| `Map.hpp` | `Map` (the island presets and `SponzaPalace`), `toString`, `mapFromName` |
| `Terrain.hpp`, `Foliage.hpp` | `TerrainParams`, `FoliageParams`: island generation, ocean, grass and trees |
| `Graphics.hpp` | `GraphicsSettings`, `QualityTier` |
| `Sky.hpp` | `SkySettings`, `SunSettings` |
| `Input.hpp` | `Key`, `KeyAction` |
| `Error.hpp`, `Version.hpp` | `Error` (the exception type), `NUT_VERSION_*` |

At run time the engine needs its `shaders/` directory and the `assets/` directory (see
`EngineConfig::paths`; by default both are looked up in the working directory).

In your CMake project, either add Nut as a subdirectory or install it and use `find_package`:

```cmake
add_subdirectory(Nut)          # or: find_package(Nut 0.1 REQUIRED)
target_link_libraries(my_game PRIVATE nut::nut)
```

### Project layout

```
include/nut/       public headers
src/               implementation (namespace nut::detail): renderer, terrain, foliage, village, Sponza, GUI
shaders/           GLSL shaders, loaded at run time
assets/            textures, panoramas, the Sponza map
examples/demo/     the demo game
third_party/       Dear ImGui (git submodule), stb_image
```

# Controls
- **WASD** for moving (**Shift** to sprint / swim faster)
- **SPACE_BAR** for jumping (swim up while in the water)
- **C** or **Ctrl** to dive while swimming (or look down and press **W**)
- **Mouse** cursor for Looking
- **Tab** to show / hide the settings panel (frees the mouse while it is open)
- **M** to switch the minimap (bottom-left) between small and large
- **Enter** to free / capture the mouse

# Maps

Pick a map in the settings panel (**Tab -> Map**):

- **Island maps**: Big Island, Archipelago, Mountains, Rolling Hills, Plains, Mesa / Canyons and
  Alpine Peaks - procedurally generated terrain presets (see *World* below).
- **Sponza Palace**: the Crytek Sponza atrium (glTF, `assets/maps/sponza`), loaded the first time
  you open it. It uses the PBR materials (base colour, normal and metal/roughness maps,
  alpha-tested plants and chains), a sun shadow map fitted to the building, and baked indirect light:
  a ray-traced grid stores how much sky every point sees and how much sunlight bounces off
  the lit walls (three bounces), and it is re-baked in the background when the sun moves.
  Exposure adapts to the light where you stand, so the shaded galleries stay readable while
  the sunlit courtyard rolls off softly. Walls, columns and props block you; *Map -> Bounce
  Light* and *Eye adaptation* tune the look. With the default sky the sun only reaches the
  upper walls; turn off *Sky / Panorama -> Sun & light from sky* and raise the sun to light the floor.

Start straight into a map with `--map <name>` (any part of the name, e.g. `--map sponza`).

# World

The island has a procedurally planned village (CC0 materials in `assets/textures/village`).
It picks a gentle, dry site with a view of the sea, lays out a fountain plaza with three or
four cobbled streets winding out along the hillside, and lines them with 10-16 houses that
face the street, each on its own terrace blended into the slope. You start at the end of
the main street, looking up towards the plaza.

- **Houses**: pastel plaster with stone corners and foundations, terracotta roofs, glass
  windows with painted shutters and flower boxes, open doors, door lanterns, chimneys,
  door canopies and balconies. One- and two-storey houses; two-storey ones have stairs.
- **Interiors**: fireplaces with animated fire, tables and chairs, rugs, beds with night
  stands and candles, wardrobes, cabinets with jars, ceiling beams and hanging lamps.
- **Lights**: lamps, fires and lanterns are real point lights; houses are lit by their own
  lamps, and from dusk the street lamps light the streets, grass and trees. Buildings cast
  sun shadows (sunlight falls through the windows onto the floors).
- **Plaza**: a two-tier fountain, benches, market stalls, planters, barrels, crates and trees.
- **Gameplay**: walls, furniture and props block you; floors, steps, stairs and balconies are
  walkable, and you fall off edges.
- **Settings** (Tab -> Village): go to the village, lamps on/off, light intensity, new village.

Run `build/examples/nut-demo --windowed` from the repository root for a windowed session
(`--map <name>` picks the start map, `--help` lists them).

- A procedurally generated island (hills, mountains, beaches, no lakes) surrounded by an ocean
- Ocean with Gerstner waves: walk into the sea to swim, dive to explore the sea floor,
  and keep an eye on your oxygen
- Swaying forests (fir, broadleaf and acacia trees) and wind-blown grass that parts as you walk through it
- Sun shadows from the terrain and trees, valley fog, sun glow in the haze, bloom and sun rays
- Terrain presets, island shape, waves, materials, vegetation, sky, fog and graphics quality are all tweakable in the GUI

# Performance
The game adapts itself to the GPU it runs on:

- **GPU detection**: on start it reads the GPU name, driver and video memory and picks a
  starting quality tier for that GPU family (old / low-end Radeon HD and GeForce GT, Intel HD,
  modern cards).
- **Benchmark**: on the first launch (and after a driver or GPU change) it renders a demanding
  view at two resolutions per tier for a few seconds, predicts the resolution each tier can
  hold at 60 FPS and keeps the best one. The result is saved in `graphics.cfg`; delete the file
  or press *Re-run GPU benchmark* (Tab -> Graphics & Performance) to measure again.
- **Quality tiers** (Potato, Low, Medium, High) change more than resolution: lower tiers compile
  simpler shaders (no normal maps, single-tap shadows, fewer lights, no caustics), turn off
  bloom / sun rays, use less texture filtering, smaller shadow maps and less grass and tree detail.
- **At runtime** the render resolution adapts every frame, and if even the lowest resolution
  can't hold the target for a few seconds the game drops a tier (and goes back up when there
  is headroom).
- **`gpu_report.txt`** is written next to the game: GPU, driver, video memory, benchmark
  results, GPU time per pass and any shader compiler messages. Send it along when reporting
  performance problems on a specific machine.

# Assets
HDRI skies (`assets/panoramas`), terrain materials (`assets/textures/terrain`) and the bark / leaf textures
the tree cards are baked from (`assets/textures/foliage`) are CC0 from [Poly Haven](https://polyhaven.com)

The Sponza palace (`assets/maps/sponza`) is the glTF version from the
[Khronos glTF Sample Assets](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Sponza)
(Crytek Sponza with Alexandre Pestana's PBR textures), under the CRYENGINE Limited License
Agreement - see `assets/maps/sponza/LICENSE.md`.
  
# Download

Every push to `main` is built for Windows and Linux by GitHub Actions
(`.github/workflows/release.yml`) and published on the
[Releases](https://github.com/zoroxide/Nut/releases) page:

- **Windows**: unzip `Nut-windows-x86_64.zip` and run `nut-demo.exe` (DLLs included).
- **Linux**: extract `Nut-linux-x86_64.tar.gz` and run `nut-demo.sh` (GLFW, GLEW and Assimp are
  bundled; OpenGL comes from your driver). Needs glibc 2.35+ (Ubuntu 22.04 or newer).

`scripts/package.sh linux|windows` builds the same packages locally (into `dist/`).

# Build and installation

Nut builds with CMake (3.16+) and a C++17 compiler. Clone with the Dear ImGui submodule:

```sh
git clone --recursive https://github.com/zoroxide/Nut.git
# or, in an existing clone:
git submodule update --init
```

## Linux (Debian/Ubuntu)

```sh
sudo apt update
sudo apt install -y build-essential cmake ninja-build pkg-config \
  libglfw3-dev libglew-dev libglm-dev libassimp-dev \
  libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/examples/nut-demo --windowed
```

## Windows (MSYS2 UCRT64)

Install [MSYS2](https://www.msys2.org/) and open the **UCRT64** shell (don't mix MINGW64 and
UCRT64 libraries):

```sh
pacman -Syu
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-glfw mingw-w64-ucrt-x86_64-glew mingw-w64-ucrt-x86_64-glm mingw-w64-ucrt-x86_64-assimp
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/examples/nut-demo.exe --windowed
```

Run it from the repository root (or pass `--resources <dir>`), with `C:\msys64\ucrt64\bin` on
`PATH` for the DLLs when starting it outside the UCRT64 shell.

## Options and installing

| CMake option | Default | |
| --- | --- | --- |
| `NUT_BUILD_EXAMPLES` | ON (top level) | build `nut-demo` |
| `NUT_INSTALL` | ON (top level) | install rules and the `find_package(Nut)` package |
| `BUILD_SHARED_LIBS` | OFF | build `nut` as a shared library |
| `NUT_WARNINGS_AS_ERRORS` | OFF | `-Werror` / `/WX` |

`cmake --install build --prefix <dir>` installs the headers, the library, the shaders
(`share/nut/shaders`) and the CMake package (`lib/cmake/Nut`).
