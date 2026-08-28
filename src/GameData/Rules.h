#pragma once

#include "Engine/Core/Types.h"
#include "Westwood/Ini/Ini.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ra2yr::gamedata {

struct WeaponDefinition {
    std::string id;
    std::string uiNameKey;
    std::string icon;
    int damage = 15;
    int rateOfFire = 25;
    float range = 4.0F;
    std::string projectile = "InvisibleLow";
    std::string warhead = "SA";
    std::string targetTypes = "Ground";
    std::string upgradeGroup;
    int maxUpgradeLevel = 0;
};

struct ArmorDefinition {
    std::string id;
    std::string uiNameKey;
    std::string icon;
    int value = 0;
    std::string upgradeGroup;
    int maxUpgradeLevel = 0;
};

struct UnitDefinition {
    std::string id = "E2";
    std::string image = "CONS";
    std::string uiNameKey = "Conscript";
    std::string secondaryUiNameKey;
    int strength = 125;
    int speed = 4;
    int armorValue = 0;
    std::string armorType = "Light";
    Faction faction = Faction::Neutral;
    std::vector<std::string> unitTags;
    float selectionRadius = 0.30F;
    std::string occupancyProfile;
    std::string voiceSelect;
    std::string voiceMove;
    std::string voiceAttack;
    int sight = 5;
    bool selectable = true;
    bool autoAcquire = true;
    bool returnFire = true;
    int experienceValue = 0;
    std::string veterancyProfile = "Standard";
    std::string initialVeterancy = "Rookie";
    std::vector<std::string> shieldLayers;
    int initialEnergy = 0;
    int maxEnergy = 0;
    ArmorDefinition armor;
    std::vector<WeaponDefinition> weapons;
    WeaponDefinition primary;
};

class RulesDatabase {
public:
    bool load(const std::filesystem::path& rulesPath, std::string& error);
    [[nodiscard]] const UnitDefinition& e2() const { return e2_; }
    [[nodiscard]] const UnitDefinition* findUnit(std::string_view id) const;
    [[nodiscard]] std::string tagUiName(std::string_view tag) const;
    [[nodiscard]] const std::vector<UnitDefinition>& infantry() const { return infantry_; }
    [[nodiscard]] bool loaded() const { return loaded_; }

private:
    UnitDefinition e2_;
    std::vector<UnitDefinition> infantry_;
    std::unordered_map<std::string, std::string> tagNames_;
    bool loaded_ = false;
};

} // namespace ra2yr::gamedata
