#include "Engine/Engine.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <cctype>
#include <string>

// Map by (part of) its name, case-insensitive: "sponza", "archipelago", "big island"...
static int findMap(const std::string& wanted) {
    auto lower = [](std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; };
    for (int i = 0; i < Engine::mapCount(); ++i)
        if (lower(Engine::mapName(i)).find(lower(wanted)) != std::string::npos) return i;
    return -1;
}

int main(int argc, char** argv) {
    // --windowed, --smoke-test, --map <name>
    bool smoke = false, windowed = false;
    std::string map;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke-test") == 0) smoke = true;
        else if (std::strcmp(argv[i], "--windowed") == 0) windowed = true;
        else if (std::strcmp(argv[i], "--map") == 0 && i + 1 < argc) map = argv[++i];
    }
    windowed |= smoke;
    Engine engine;

    // Initialize the engine (fullscreen by default). If you want windowed, pass false.
    if (!engine.init(!windowed)) {
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

    if (!map.empty()) {
        int index = findMap(map);
        if (index < 0 || !engine.selectMap(index)) {
            std::cerr << "Unknown or unloadable map: " << map << "\n";
            return 5;
        }
    }

    // A reproducible render/collision check, without entering the interactive loop.
    if (smoke) {
        std::string shotPath = "build/village-smoke.ppm";
        if (engine.inSponza()) {
            // The spawn stands on a floor, and the palace's outer wall blocks the player
            const Sponza& sp = engine.sponza();
            glm::vec3 spawn = sp.spawn();
            if (sp.groundAt(spawn.x, spawn.z, spawn.y - 1.7f) < -1e8f) return 3;
            glm::vec3 outside = spawn, start = spawn;
            for (int i = 0; i < 400; ++i) { outside.z += 0.05f; sp.collide(outside, 1.7f); }
            if (outside.z > sp.boundsMax().z - 0.2f) return 3;
            std::cout << "Sponza smoke: floor at spawn, walked " << (outside.z - start.z) << " m before a wall\n";
            shotPath = "build/sponza-smoke.ppm";
        } else {
            if (!engine.village().active()) return 2;
            glm::vec3 door = engine.village().testDoorway();
            glm::vec3 wall = engine.village().testWall();
            glm::vec3 originalDoor = door, originalWall = wall;
            engine.village().collide(door);
            engine.village().collide(wall);
            if (glm::length(door-originalDoor)>0.01f || glm::length(wall-originalWall)<0.1f) return 3;
            std::cout<<"Village smoke: doorway open, wall collision active\n";
        }
        while (glGetError()!=GL_NO_ERROR) {}
        for (int i=0;i<3;++i) engine.renderFrame(0,1280,720);
        glFinish();
        GLenum error=glGetError();
        std::vector<unsigned char> pixels(1280*720*3);
        glReadPixels(0,0,1280,720,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
        std::ofstream shot(shotPath,std::ios::binary);
        shot<<"P6\n1280 720\n255\n";
        for(int y=719;y>=0;--y) shot.write(reinterpret_cast<char*>(pixels.data()+y*1280*3),1280*3);
        std::cout<<"Smoke render: "<<shotPath<<", GL error="<<error<<"\n";
        return error==GL_NO_ERROR?0:4;
    }

    // Enter the engine main loop
    engine.mainloop();

    return 0;
}
