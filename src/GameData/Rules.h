#pragma once

#include "Engine/Core/Types.h"
#include "Westwood/Ini/Ini.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ra2yr::gamedata {

struct WeaponDefinition {
    int damage = 15;
    int rateOfFire = 25;
    float range = 4.0F;
    std::string projectile = "InvisibleLow";
    std::string warhead = "SA";
};

struct UnitDefinition {
    std::string id = "E2";
    std::string image = "CONS";
    std::string name = "Conscript";
    int strength = 125;
    int speed = 4;
    int armorValue = 0;
    std::string armorType = "Light";
    Faction faction = Faction::Neutral;
    std::vector<std::string> unitTags;
    int sight = 5;
    bool selectable = true;
    bool autoAcquire = true;
    bool returnFire = true;
    WeaponDefinition primary;
};

class RulesDatabase {
public:
    bool load(const std::filesystem::path& rulesPath, std::string& error);
    [[nodiscard]] const UnitDefinition& e2() const { return e2_; }
    [[nodiscard]] bool loaded() const { return loaded_; }

private:
    UnitDefinition e2_;
    bool loaded_ = false;
};

} // namespace ra2yr::gamedata
