#include "GameData/Rules.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace ra2yr::gamedata {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::vector<std::string> splitList(const std::string& value) {
    std::vector<std::string> result;
    std::stringstream stream(value);
    std::string token;
    while (std::getline(stream, token, ',')) {
        const auto first = token.find_first_not_of(" \t\r\n");
        const auto last = token.find_last_not_of(" \t\r\n");
        if (first != std::string::npos) {
            result.push_back(token.substr(first, last - first + 1));
        }
    }
    return result;
}

Faction parseFaction(const std::string& value) {
    const std::string normalized = lower(value);
    if (normalized == "soviet") {
        return Faction::Soviet;
    }
    if (normalized == "allied") {
        return Faction::Allied;
    }
    if (normalized == "yuri") {
        return Faction::Yuri;
    }
    return Faction::Neutral;
}

bool parseFloat(const std::string& value, float& result) {
    char* end = nullptr;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0') {
        return false;
    }
    result = parsed;
    return true;
}

} // namespace

bool RulesDatabase::load(const std::filesystem::path& rulesPath, std::string& error) {
    westwood::IniDocument rules;
    if (!rules.load(rulesPath, error)) {
        return false;
    }
    if (rules.get("InfantryTypes", "1").empty() || rules.get("InfantryTypes", "1") != "E2" ||
        !rules.hasSection("E2") || !rules.hasSection("M1Carbine") || !rules.hasSection("SA") ||
        !rules.hasSection("InvisibleLow")) {
        error = "Enhanced Rules.ini must define InfantryTypes=1=E2 and E2/M1Carbine/SA/InvisibleLow sections";
        return false;
    }
    e2_.id = "E2";
    e2_.image = rules.get("E2", "Image", "CONS");
    e2_.name = rules.get("E2", "Name", "Conscript");
    e2_.strength = rules.getInt("E2", "Strength", 125);
    e2_.speed = rules.getInt("E2", "Speed", 4);
    e2_.armorValue = rules.getInt("E2", "ArmorValue", 0);
    e2_.armorType = rules.get("E2", "ArmorType", "Light");
    e2_.faction = parseFaction(rules.get("E2", "Faction", "Soviet"));
    e2_.unitTags = splitList(rules.get("E2", "UnitTag"));
    if (e2_.unitTags.empty()) {
        error = "Enhanced Rules.ini must define at least one E2 UnitTag";
        return false;
    }
    const std::string selectionRadius = rules.get("E2", "SelectionRadius");
    if (!parseFloat(selectionRadius, e2_.selectionRadius) || e2_.selectionRadius <= 0.0F) {
        error = "Enhanced Rules.ini must define a positive E2 SelectionRadius";
        return false;
    }
    e2_.occupancyProfile = rules.get("E2", "OccupancyProfile");
    if (e2_.occupancyProfile.empty()) {
        error = "Enhanced Rules.ini must define E2 OccupancyProfile explicitly; do not infer it from UnitTag";
        return false;
    }
    e2_.voiceSelect = rules.get("E2", "VoiceSelect");
    e2_.voiceMove = rules.get("E2", "VoiceMove");
    e2_.voiceAttack = rules.get("E2", "VoiceAttack");
    if (e2_.voiceSelect.empty() || e2_.voiceMove.empty() || e2_.voiceAttack.empty()) {
        error = "Enhanced Rules.ini must define E2 VoiceSelect, VoiceMove and VoiceAttack";
        return false;
    }
    e2_.sight = rules.getInt("E2", "Sight", 5);
    e2_.selectable = rules.getBool("E2", "Selectable", true);
    e2_.autoAcquire = rules.getBool("E2", "AutoAcquire", true);
    e2_.returnFire = rules.getBool("E2", "ReturnFire", true);
    e2_.primary.damage = rules.getInt("M1Carbine", "Damage", 15);
    e2_.primary.rateOfFire = rules.getInt("M1Carbine", "ROF", 25);
    e2_.primary.range = static_cast<float>(rules.getInt("M1Carbine", "Range", 4));
    e2_.primary.projectile = rules.get("M1Carbine", "Projectile", "InvisibleLow");
    e2_.primary.warhead = rules.get("M1Carbine", "Warhead", "SA");
    loaded_ = true;
    return true;
}

} // namespace ra2yr::gamedata
