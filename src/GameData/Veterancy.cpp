#include "GameData/Veterancy.h"

#include "Westwood/Ini/Ini.h"

#include <algorithm>
#include <charconv>

namespace ra2yr::gamedata {
namespace {

std::vector<std::string> split(const std::string& value) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t comma = value.find(',', start);
        const std::size_t end = comma == std::string::npos ? value.size() : comma;
        const std::size_t first = value.find_first_not_of(" \t\r\n", start);
        if (first != std::string::npos && first < end) {
            const std::size_t last = value.find_last_not_of(" \t\r\n", end - 1);
            result.push_back(value.substr(first, last - first + 1));
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return result;
}

int indexOf(const std::string& key) {
    int result = 0;
    const auto [end, ec] = std::from_chars(key.data(), key.data() + key.size(), result);
    return ec == std::errc{} && end == key.data() + key.size() && result > 0 ? result : 0;
}

} // namespace

bool VeterancyDatabase::load(const std::filesystem::path& rulesPath, std::string& error) {
    westwood::IniDocument rules;
    if (!rules.load(rulesPath, error)) {
        return false;
    }
    profiles_.clear();
    for (const auto& [key, value] : rules.entries("VeterancyProfiles")) {
        const int index = indexOf(key);
        if (index == 0 || value.empty()) {
            continue;
        }
        const std::string profileId = value;
        const std::string profileSection = "Veterancy." + profileId;
        if (!rules.hasSection(profileSection)) {
            error = "Rules.ini is missing " + profileSection;
            return false;
        }
        VeterancyProfile profile;
        profile.id = profileId;
        for (const std::string& levelId : split(rules.get(profileSection, "Levels"))) {
            const std::string levelSection = profileSection + "." + levelId;
            VeterancyLevelDefinition level;
            level.id = levelId;
            level.uiNameKey = rules.get(levelSection, "UIName", levelId);
            level.requiredExperience = rules.getInt(levelSection, "RequiredExperience", 0);
            profile.levels.push_back(std::move(level));
        }
        if (profile.levels.empty()) {
            error = profileSection + " must define Levels";
            return false;
        }
        std::sort(profile.levels.begin(), profile.levels.end(),
            [](const VeterancyLevelDefinition& first, const VeterancyLevelDefinition& second) {
                return first.requiredExperience < second.requiredExperience;
            });
        profiles_.push_back(std::move(profile));
    }
    return true;
}

const VeterancyProfile* VeterancyDatabase::find(std::string_view id) const {
    const auto it = std::find_if(profiles_.begin(), profiles_.end(), [id](const VeterancyProfile& profile) {
        return profile.id == id;
    });
    return it == profiles_.end() ? nullptr : &*it;
}

const VeterancyLevelDefinition* VeterancyDatabase::level(std::string_view profileId, int experience) const {
    const VeterancyProfile* profile = find(profileId);
    if (profile == nullptr || profile->levels.empty()) {
        return nullptr;
    }
    const VeterancyLevelDefinition* result = &profile->levels.front();
    for (const VeterancyLevelDefinition& candidate : profile->levels) {
        if (candidate.requiredExperience > experience) {
            break;
        }
        result = &candidate;
    }
    return result;
}

const VeterancyLevelDefinition* VeterancyDatabase::nextLevel(std::string_view profileId, int experience) const {
    const VeterancyProfile* profile = find(profileId);
    if (profile == nullptr) {
        return nullptr;
    }
    for (const VeterancyLevelDefinition& candidate : profile->levels) {
        if (candidate.requiredExperience > experience) {
            return &candidate;
        }
    }
    return nullptr;
}

} // namespace ra2yr::gamedata
