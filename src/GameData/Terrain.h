#pragma once

#include "Engine/Core/Types.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ra2yr::gamedata {

struct TerrainDefinition {
    std::string id;
    std::string uiNameKey;
    std::string icon;
    bool passable = true;
    bool buildable = true;
};

class TerrainDatabase {
public:
    bool load(const std::filesystem::path& path, std::string& error);
    [[nodiscard]] const TerrainDefinition* find(std::string_view id) const;
    [[nodiscard]] const std::vector<TerrainDefinition>& definitions() const { return definitions_; }
    [[nodiscard]] bool loaded() const { return loaded_; }

private:
    std::vector<TerrainDefinition> definitions_;
    bool loaded_ = false;
};

} // namespace ra2yr::gamedata
