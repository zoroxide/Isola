#include <nut/Map.hpp>

#include <cctype>
#include <string>

namespace nut {

namespace {
constexpr std::string_view kNames[] = {
    "Big Island", "Archipelago", "Mountains", "Rolling Hills", "Plains", "Mesa / Canyons", "Alpine Peaks", "Sponza Palace",
};
static_assert(std::size(kNames) == kAllMaps.size());

// Lower-case letters and digits only, so "rolling-hills", "RollingHills" and "rolling hills" match
std::string normalized(std::string_view s) {
    std::string out;
    for (char c : s)
        if (std::isalnum(static_cast<unsigned char>(c)))
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}
} // namespace

std::string_view toString(Map map) noexcept {
    auto i = static_cast<std::size_t>(map);
    return i < std::size(kNames) ? kNames[i] : std::string_view{"Unknown"};
}

std::optional<Map> mapFromName(std::string_view name) noexcept {
    try {
        std::string wanted = normalized(name);
        if (wanted.empty())
            return std::nullopt;
        for (Map map : kAllMaps)
            if (normalized(toString(map)).find(wanted) != std::string::npos)
                return map;
    } catch (...) {   // allocation failure
    }
    return std::nullopt;
}

} // namespace nut
