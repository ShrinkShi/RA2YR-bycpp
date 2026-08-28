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

int parseListIndex(const std::string& key) {
    char* end = nullptr;
    const long value = std::strtol(key.c_str(), &end, 10);
    return end != key.c_str() && *end == '\0' && value > 0 && value <= 100000L ?
        static_cast<int>(value) : 0;
}

WeaponDefinition parseWeapon(const westwood::IniDocument& rules, const std::string& weaponId) {
    WeaponDefinition weapon;
    weapon.id = weaponId;
    weapon.uiNameKey = rules.get(weaponId, "UIName", weaponId);
    weapon.icon = rules.get(weaponId, "Icon");
    weapon.damage = rules.getInt(weaponId, "Damage", 15);
    weapon.rateOfFire = rules.getInt(weaponId, "ROF", 25);
    weapon.range = static_cast<float>(rules.getInt(weaponId, "Range", 4));
    weapon.projectile = rules.get(weaponId, "Projectile", "InvisibleLow");
    weapon.warhead = rules.get(weaponId, "Warhead", "SA");
    weapon.targetTypes = rules.get(weaponId, "TargetTypes", "Ground");
    weapon.upgradeGroup = rules.get(weaponId, "UpgradeGroup");
    weapon.maxUpgradeLevel = rules.getInt(weaponId, "MaxUpgradeLevel", 0);
    return weapon;
}

ArmorDefinition parseArmor(const westwood::IniDocument& rules, const std::string& armorId,
    int fallbackValue) {
    ArmorDefinition armor;
    armor.id = armorId;
    const std::string section = "ArmorType." + armorId;
    armor.uiNameKey = rules.get(section, "UIName", armorId);
    armor.icon = rules.get(section, "Icon");
    armor.value = rules.getInt(section, "Value", fallbackValue);
    armor.upgradeGroup = rules.get(section, "UpgradeGroup");
    armor.maxUpgradeLevel = rules.getInt(section, "MaxUpgradeLevel", 0);
    return armor;
}

UnitDefinition parseUnit(const westwood::IniDocument& rules, const std::string& id) {
    UnitDefinition unit;
    unit.id = id;
    unit.image = rules.get(id, "Image", "CONS");
    unit.uiNameKey = rules.get(id, "UIName", rules.get(id, "Name", id));
    unit.secondaryUiNameKey = rules.get(id, "SecondaryUIName");
    unit.strength = rules.getInt(id, "Strength", 125);
    unit.speed = std::max(1, rules.getInt(id, "Speed", 4));
    unit.armorValue = rules.getInt(id, "ArmorValue", 0);
    unit.armorType = rules.get(id, "ArmorType", "Light");
    unit.armor = parseArmor(rules, unit.armorType, unit.armorValue);
    unit.faction = parseFaction(rules.get(id, "Faction", "Neutral"));
    unit.unitTags = splitList(rules.get(id, "UnitTag"));
    unit.selectionRadius = 0.30F;
    static_cast<void>(parseFloat(rules.get(id, "SelectionRadius", "0.30"), unit.selectionRadius));
    unit.occupancyProfile = rules.get(id, "OccupancyProfile");
    unit.voiceSelect = rules.get(id, "VoiceSelect");
    unit.voiceMove = rules.get(id, "VoiceMove");
    unit.voiceAttack = rules.get(id, "VoiceAttack");
    unit.sight = rules.getInt(id, "Sight", 5);
    unit.selectable = rules.getBool(id, "Selectable", true);
    unit.autoAcquire = rules.getBool(id, "AutoAcquire", true);
    unit.returnFire = rules.getBool(id, "ReturnFire", true);
    unit.experienceValue = std::max(0, rules.getInt(id, "ExperienceValue", 0));
    unit.veterancyProfile = rules.get(id, "VeterancyProfile", "Standard");
    unit.initialVeterancy = rules.get(id, "InitialVeterancy", "Rookie");
    unit.shieldLayers = splitList(rules.get(id, "ShieldLayers"));
    unit.initialEnergy = std::max(0, rules.getInt(id, "InitialEnergy", 0));
    unit.maxEnergy = std::max(unit.initialEnergy, rules.getInt(id, "MaxEnergy", unit.initialEnergy));

    const std::string weaponList = rules.get(id, "Weapons", rules.get(id, "Primary", "M1Carbine"));
    for (const std::string& weaponId : splitList(weaponList)) {
        if (rules.hasSection(weaponId)) {
            unit.weapons.push_back(parseWeapon(rules, weaponId));
        }
    }
    if (unit.weapons.empty() && rules.hasSection("M1Carbine")) {
        unit.weapons.push_back(parseWeapon(rules, "M1Carbine"));
    }
    if (!unit.weapons.empty()) {
        unit.primary = unit.weapons.front();
    }
    return unit;
}

} // namespace

bool RulesDatabase::load(const std::filesystem::path& rulesPath, std::string& error) {
    westwood::IniDocument rules;
    if (!rules.load(rulesPath, error)) {
        return false;
    }
    if (rules.get("InfantryTypes", "1").empty() || !rules.hasSection("E2") ||
        !rules.hasSection("M1Carbine") || !rules.hasSection("SA") || !rules.hasSection("InvisibleLow")) {
        error = "Enhanced Rules.ini must define E2, M1Carbine, SA and InvisibleLow sections";
        return false;
    }
    e2_ = parseUnit(rules, "E2");
    if (e2_.unitTags.empty()) {
        error = "Enhanced Rules.ini must define at least one E2 UnitTag";
        return false;
    }
    if (e2_.selectionRadius <= 0.0F) {
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
    infantry_.clear();
    std::vector<std::pair<int, std::string>> indexed;
    for (const auto& [key, value] : rules.entries("InfantryTypes")) {
        const int index = parseListIndex(key);
        if (index > 0 && !value.empty()) {
            indexed.emplace_back(index, value);
        }
    }
    std::sort(indexed.begin(), indexed.end());
    for (const auto& [index, id] : indexed) {
        static_cast<void>(index);
        if (rules.hasSection(id)) {
            infantry_.push_back(id == "E2" ? e2_ : parseUnit(rules, id));
        }
    }
    if (infantry_.empty()) {
        infantry_.push_back(e2_);
    }
    tagNames_.clear();
    for (const std::string& tag : e2_.unitTags) {
        const std::string section = "UnitTag." + tag;
        tagNames_[tag] = rules.get(section, "UIName", tag);
    }
    loaded_ = true;
    return true;
}

const UnitDefinition* RulesDatabase::findUnit(std::string_view id) const {
    const auto it = std::find_if(infantry_.begin(), infantry_.end(), [id](const UnitDefinition& unit) {
        return unit.id == id;
    });
    return it == infantry_.end() ? nullptr : &*it;
}

std::string RulesDatabase::tagUiName(std::string_view tag) const {
    const auto it = tagNames_.find(std::string(tag));
    return it == tagNames_.end() ? std::string(tag) : it->second;
}

} // namespace ra2yr::gamedata
