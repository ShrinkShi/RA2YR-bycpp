#pragma once

#include "GameData/Rules.h"
#include "GameData/Veterancy.h"
#include "Simulation/Simulation.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace ra2yr::client::hud {

enum class HealthBand {
    Healthy,
    Warning,
    Critical,
};

struct ShieldViewModel {
    int value = 0;
};

struct ArmorCardViewModel {
    std::string id;
    std::string uiNameKey;
    std::string icon;
    int value = 0;
    int upgradeLevel = 0;
};

struct WeaponCardViewModel {
    std::string id;
    std::string uiNameKey;
    std::string icon;
    int damage = 0;
    float range = 0.0F;
    int rateOfFire = 0;
    std::string targetTypes;
    int upgradeLevel = 0;
};

struct UnitStatusViewModel {
    std::string displayNameKey;
    std::string secondaryNameKey;
    int health = 0;
    int maxHealth = 0;
    HealthBand healthBand = HealthBand::Healthy;
    std::vector<ShieldViewModel> shields;
    int energy = 0;
    int maxEnergy = 0;
    std::uint32_t kills = 0;
    std::string veterancyNameKey;
    int experience = 0;
    int nextExperience = 0;
    ArmorCardViewModel armor;
    std::vector<WeaponCardViewModel> weapons;
    std::vector<std::string> tags;
};

struct PlayerUpgradeState {
    std::unordered_map<std::string, int> levels;

    [[nodiscard]] int level(std::string_view group) const {
        const auto it = levels.find(std::string(group));
        return it == levels.end() ? 0 : it->second;
    }
};

class UnitStatusViewModelBuilder {
public:
    static UnitStatusViewModel build(const simulation::Entity& entity,
        const gamedata::UnitDefinition& definition, const gamedata::RulesDatabase& rules,
        const gamedata::VeterancyDatabase& veterancy, const PlayerUpgradeState& upgrades,
        float healthyThreshold = 0.60F, float criticalThreshold = 0.30F);

    [[nodiscard]] static std::wstring tooltip(const ArmorCardViewModel& card);
    [[nodiscard]] static std::wstring tooltip(const WeaponCardViewModel& card);
};

} // namespace ra2yr::client::hud
