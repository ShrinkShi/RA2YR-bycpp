#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ra2yr::gamedata {

struct VeterancyLevelDefinition {
    std::string id;
    std::string uiName;
    int requiredExperience = 0;
};

struct VeterancyProfile {
    std::string id;
    std::vector<VeterancyLevelDefinition> levels;
};

class VeterancyDatabase {
public:
    bool load(const std::filesystem::path& rulesPath, std::string& error);
    [[nodiscard]] const VeterancyProfile* find(std::string_view id) const;
    [[nodiscard]] const VeterancyLevelDefinition* level(std::string_view profileId,
        int experience) const;
    [[nodiscard]] const VeterancyLevelDefinition* nextLevel(std::string_view profileId,
        int experience) const;

private:
    std::vector<VeterancyProfile> profiles_;
};

} // namespace ra2yr::gamedata
