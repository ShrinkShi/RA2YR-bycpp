#pragma once

#include "Westwood/Ini/Ini.h"

#include <filesystem>
#include <string>

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
    std::string armor = "flak";
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
