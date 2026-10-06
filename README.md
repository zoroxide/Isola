**Procedural Terrain Generator Based Game using Moden OpenGL**
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

# Demo Code
use you own textures and Panoramas (png and HDR)

```cpp
#include "Engine/Engine.h"
#include <iostream>

int main() {
    Engine engine;

    // Initialize the engine (fullscreen by default). If you want windowed, pass false.
    if (!engine.init(true)) {
        std::cerr << "Failed to initialize engine\n";
        return -1;
    }

    // Ground materials (grass/rock/sand/snow) load automatically from assets/textures/terrain.
    // To force your own grass texture instead:
    // engine.load_terrain_using_texture("assets/textures/grass.png");

    // Load panorama (optional). HDR panoramas also drive the sun direction, colour and fog.
    if (!engine.panorama("assets/panoramas/kloofendal_48d_partly_cloudy_puresky_4k.hdr") &&
        !engine.panorama("assets/skybox/sky_17_2k.png")) {
        std::cerr << "Failed to load panorama texture\n";
    }

    // Toggle vsync if desired
    engine.vsync(true);

    // Enter the engine main loop
    engine.mainloop();

    return 0;
}


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

Use `build/windows/program.exe --windowed` on Windows or
`./build/linux/program --windowed` on Linux for a windowed session (run from the
repository root, with the platform's runtime libraries on PATH).
`--smoke-test` checks village doorway/wall collision, renders three frames, and
writes `build/village-smoke.ppm` for inspection. With `--map sponza` it checks the
palace's floor and wall collision instead and writes `build/sponza-smoke.ppm`.

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

- **Windows**: unzip `Nut-windows-x86_64.zip` and run `Nut.exe` (DLLs included).
- **Linux**: extract `Nut-linux-x86_64.tar.gz` and run `nut.sh` (GLFW, GLEW and Assimp are
  bundled; OpenGL comes from your driver). Needs glibc 2.35+ (Ubuntu 22.04 or newer).

`scripts/package.sh linux|windows` builds the same packages locally (into `dist/`).

# Build and installation

Run all build commands from the repository root. The executable loads assets using
relative paths, so it should also be run from there.

## Windows (MSYS2 UCRT64)

Install [MSYS2](https://www.msys2.org/) and open the **UCRT64** shell. Do not mix
libraries from the MINGW64 and UCRT64 environments.

Update MSYS2, then install the compiler, Make, and graphics dependencies:

```sh
pacman -Syu
pacman -S --needed base-devel \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-glfw \
  mingw-w64-ucrt-x86_64-glew \
  mingw-w64-ucrt-x86_64-glm \
  mingw-w64-ucrt-x86_64-assimp
```

Build and run:

```sh
make win
make run
```

`make windows` is an alias for `make win`. The Windows executable is written to
`build/windows/program.exe`.

You may also invoke Make from PowerShell, provided `C:\msys64\ucrt64\bin` is on
`PATH`. `make run` prioritizes this directory so that it loads the matching UCRT64
DLLs. If MSYS2 is installed elsewhere, pass its runtime directory explicitly:

```powershell
make UCRT64_BIN=D:/path/to/msys64/ucrt64/bin run
```

## Linux (Debian/Ubuntu)

Install the build tools and dependencies:

```sh
sudo apt update
sudo apt install -y build-essential make pkg-config \
  libglfw3-dev libglew-dev libglm-dev \
  libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
  libgl1-mesa-dev libassimp-dev
```

Build and run:

```sh
make linux
make run
```

The Linux executable is written to `build/linux/program`.

## Clean build files

On either platform, remove all generated build output with:

```sh
make clean
```

