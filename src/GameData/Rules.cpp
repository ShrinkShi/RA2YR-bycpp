#include "GameData/Rules.h"

namespace ra2yr::gamedata {

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
    e2_.unitTag = rules.get("E2", "UnitTag", "E2");
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
