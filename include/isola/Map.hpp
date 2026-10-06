#pragma once
#include <isola/Export.hpp>

#include <array>
#include <optional>
#include <string_view>

namespace isola {

/// The worlds the engine can show. Island maps are procedurally generated from presets
/// (see TerrainParams); Sponza Palace is the Crytek Sponza atrium, loaded on first use from
/// `<assets>/maps/sponza/Sponza.gltf`.
enum class Map : int {
    BigIsland,
    Archipelago,
    Mountains,
    RollingHills,
    Plains,
    MesaCanyons,
    AlpinePeaks,
    SponzaPalace,
};

/// Every map, in menu order.
inline constexpr std::array<Map, 8> kAllMaps{
    Map::BigIsland, Map::Archipelago, Map::Mountains,   Map::RollingHills,
    Map::Plains,    Map::MesaCanyons, Map::AlpinePeaks, Map::SponzaPalace,
};

/// True for the procedurally generated island maps.
constexpr bool isIsland(Map map) noexcept { return map != Map::SponzaPalace; }

/// Display name, e.g. "Rolling Hills".
ISOLA_API std::string_view toString(Map map) noexcept;

/// Finds a map by (part of) its display name, ignoring case: "sponza", "alpine", "big island".
ISOLA_API std::optional<Map> mapFromName(std::string_view name) noexcept;

} // namespace isola
