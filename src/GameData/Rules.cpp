#include "GameData/Rules.h"

namespace ra2yr::gamedata {

bool RulesDatabase::load(const std::filesystem::path& rulesPath, std::string& error) {
    westwood::IniDocument rules;
    if (!rules.load(rulesPath, error)) {
        return false;
    }
    if (!rules.hasSection("E2") || !rules.hasSection("M1Carbine") || !rules.hasSection("SA") ||
        !rules.hasSection("InvisibleLow")) {
        error = "Effective rulesmd.ini is missing E2/M1Carbine/SA/InvisibleLow sections";
        return false;
    }
    e2_.id = "E2";
    e2_.image = rules.get("E2", "Image", "CONS");
    e2_.name = rules.get("E2", "Name", "Conscript");
    e2_.strength = rules.getInt("E2", "Strength", 125);
    e2_.speed = rules.getInt("E2", "Speed", 4);
    e2_.armor = rules.get("E2", "Armor", "flak");
    e2_.primary.damage = rules.getInt("M1Carbine", "Damage", 15);
    e2_.primary.rateOfFire = rules.getInt("M1Carbine", "ROF", 25);
    e2_.primary.range = static_cast<float>(rules.getInt("M1Carbine", "Range", 4));
    e2_.primary.projectile = rules.get("M1Carbine", "Projectile", "InvisibleLow");
    e2_.primary.warhead = rules.get("M1Carbine", "Warhead", "SA");
    loaded_ = true;
    return true;
}

} // namespace ra2yr::gamedata
