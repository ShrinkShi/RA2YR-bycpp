#include "GameData/Terrain.h"

#include "Westwood/Ini/Ini.h"

#include <algorithm>
#include <charconv>

namespace ra2yr::gamedata {
namespace {

int listIndex(const std::string& key) {
    int result = 0;
    const auto [end, ec] = std::from_chars(key.data(), key.data() + key.size(), result);
    return ec == std::errc{} && end == key.data() + key.size() && result > 0 ? result : 0;
}

} // namespace

bool TerrainDatabase::load(const std::filesystem::path& path, std::string& error) {
    westwood::IniDocument document;
    if (!document.load(path, error)) {
        return false;
    }
    if (!document.hasSection("TerrainTypes")) {
        error = "Terrain.ini must define [TerrainTypes]";
        return false;
    }
    std::vector<std::pair<int, std::string>> indexed;
    for (const auto& [key, value] : document.entries("TerrainTypes")) {
        const int index = listIndex(key);
        if (index > 0 && !value.empty()) {
            indexed.emplace_back(index, value);
        }
    }
    std::sort(indexed.begin(), indexed.end());
    std::vector<TerrainDefinition> parsed;
    for (const auto& [index, id] : indexed) {
        static_cast<void>(index);
        if (!document.hasSection(id)) {
            error = "Terrain.ini is missing terrain section " + id;
            return false;
        }
        TerrainDefinition terrain;
        terrain.id = id;
        terrain.uiNameKey = document.get(id, "UIName", id);
        terrain.icon = document.get(id, "Icon");
        terrain.passable = document.getBool(id, "Passable", true);
        terrain.buildable = document.getBool(id, "Buildable", true);
        parsed.push_back(std::move(terrain));
    }
    if (parsed.empty()) {
        error = "Terrain.ini must define at least one TerrainTypes entry";
        return false;
    }
    definitions_ = std::move(parsed);
    loaded_ = true;
    return true;
}

const TerrainDefinition* TerrainDatabase::find(std::string_view id) const {
    const auto it = std::find_if(definitions_.begin(), definitions_.end(), [id](const TerrainDefinition& value) {
        return value.id == id;
    });
    return it == definitions_.end() ? nullptr : &*it;
}

} // namespace ra2yr::gamedata
