// Isola demo: walk around the procedural islands and the Sponza palace.
//
//   isola-demo [--windowed] [--map <name>] [--resources <dir>]
//
// In the game: WASD move, mouse look, Space jump, Shift sprint, Tab settings panel, Esc quit.
// Demo keys:   1-8 switch map, N new island, T sun time-lapse, F3 print frame stats.

#include <isola/Isola.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct Options {
    bool windowed = false;
    isola::Map map = isola::Map::BigIsland;
    std::filesystem::path resources = ".";   // folder holding shaders/ and assets/
};

void printUsage() {
    std::cout << "Usage: isola-demo [--windowed] [--map <name>] [--resources <dir>]\n\nMaps:\n";
    for (isola::Map map : isola::kAllMaps)
        std::cout << "  " << isola::toString(map) << '\n';
}

Options parseArguments(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        auto value = [&]() -> std::string_view {
            if (i + 1 >= argc)
                throw std::invalid_argument(std::string(arg) + " needs a value");
            return argv[++i];
        };
        if (arg == "--windowed") {
            options.windowed = true;
        } else if (arg == "--map") {
            const std::string_view name = value();
            const auto map = isola::mapFromName(name);
            if (!map)
                throw std::invalid_argument("unknown map: " + std::string(name));
            options.map = *map;
        } else if (arg == "--resources") {
            options.resources = value();
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            std::exit(EXIT_SUCCESS);
        } else {
            throw std::invalid_argument("unknown option: " + std::string(arg));
        }
    }
    return options;
}

// Moves the sun through a day while the time-lapse is on
class SunCycle {
public:
    void toggle(isola::Engine& engine) {
        enabled_ = !enabled_;
        if (!enabled_)
            engine.useSunFromSky();
    }

    void update(isola::Engine& engine, float dt) {
        if (!enabled_)
            return;
        constexpr float kDegreesPerSecond = 6.0f;   // a whole day in a minute
        hourAngle_ = std::fmod(hourAngle_ + kDegreesPerSecond * dt, 360.0f);
        isola::SunSettings sun;
        sun.elevation = 65.0f * std::sin(hourAngle_ * 3.14159265f / 180.0f);
        sun.azimuth = 90.0f + hourAngle_;
        sun.intensity = sun.elevation > 0.0f ? 1.0f : 0.15f;   // a dim, moonlit night
        engine.setSun(sun);
    }

private:
    bool enabled_ = false;
    float hourAngle_ = 30.0f;
};

void printStats(const isola::Engine& engine) {
    const isola::FrameStats stats = engine.stats();
    const isola::PlayerState player = engine.player();
    std::cout << isola::toString(engine.currentMap()) << ": " << stats.fps << " fps, GPU " << stats.gpuMs
              << " ms, render scale " << static_cast<int>(stats.renderScale * 100.0f) << "%, player at ("
              << player.position.x << ", " << player.position.y << ", " << player.position.z << ")\n";
}

} // namespace

int main(int argc, char** argv) try {
    const Options options = parseArguments(argc, argv);

    isola::EngineConfig config;
    config.window.title = "Isola Demo";
    config.window.fullscreen = !options.windowed;
    config.paths.shaders = options.resources / "shaders";
    config.paths.assets = options.resources / "assets";
    config.startMap = options.map;
    config.sky = config.paths.assets / "panoramas" / "kloofendal_48d_partly_cloudy_puresky_4k.hdr";

    isola::Engine engine{config};
    std::cout << "Isola " << ISOLA_VERSION_STRING << " - " << isola::toString(engine.currentMap())
              << ". Keys: 1-8 maps, N new island, T sun time-lapse, F3 stats, Tab settings, Esc quit\n";

    SunCycle sunCycle;

    engine.onKey([&](isola::Engine& e, isola::Key key, isola::KeyAction action) {
        if (action != isola::KeyAction::Press)
            return;
        const int digit = static_cast<int>(key) - static_cast<int>(isola::Key::Num1);
        if (digit >= 0 && digit < static_cast<int>(isola::kAllMaps.size())) {
            e.loadMap(isola::kAllMaps[static_cast<std::size_t>(digit)]);
            return;
        }
        switch (key) {
        case isola::Key::N:   // the same island type, a new random layout
            if (isola::isIsland(e.currentMap())) {
                e.terrain().seed += 1;
                e.regenerateWorld();
            }
            break;
        case isola::Key::T:
            sunCycle.toggle(e);
            break;
        case isola::Key::F3:
            printStats(e);
            break;
        default:
            break;
        }
    });

    engine.onUpdate([&](isola::Engine& e, const isola::FrameInfo& frame) { sunCycle.update(e, frame.deltaTime); });

    engine.run();
    return EXIT_SUCCESS;
} catch (const isola::Error& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
} catch (const std::exception& error) {
    std::cerr << "isola-demo: " << error.what() << '\n';
    printUsage();
    return EXIT_FAILURE;
}
