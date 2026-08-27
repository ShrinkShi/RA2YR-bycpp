#include "Client/Hud/UnitStatusViewModel.h"

#include <algorithm>

namespace ra2yr::client::hud {

UnitStatusViewModel UnitStatusViewModelBuilder::build(const simulation::Entity& entity,
    const gamedata::UnitDefinition& definition, const gamedata::RulesDatabase& rules,
    const gamedata::VeterancyDatabase& veterancy, const PlayerUpgradeState& upgrades,
    float healthyThreshold, float criticalThreshold) {
    UnitStatusViewModel model;
    model.displayName = definition.name;
    model.secondaryName = definition.secondaryName;
    model.health = entity.health;
    model.maxHealth = entity.maxHealth;
    const float ratio = entity.maxHealth > 0 ? static_cast<float>(entity.health) /
        static_cast<float>(entity.maxHealth) : 0.0F;
    model.healthBand = ratio <= criticalThreshold ? HealthBand::Critical :
        ratio <= healthyThreshold ? HealthBand::Warning : HealthBand::Healthy;
    for (const int shield : entity.shields) {
        model.shields.push_back({shield});
    }
    model.energy = entity.energy;
    model.maxEnergy = entity.maxEnergy;
    model.kills = entity.killCount;
    model.experience = entity.experience;
    const gamedata::VeterancyLevelDefinition* current = veterancy.level(entity.veterancyProfile,
        entity.experience);
    const gamedata::VeterancyLevelDefinition* next = veterancy.nextLevel(entity.veterancyProfile,
        entity.experience);
    model.veterancyName = current == nullptr ? entity.veterancyLevel : current->uiName;
    model.nextExperience = next == nullptr ? 0 : next->requiredExperience;
    model.armor.id = definition.armor.id.empty() ? definition.armorType : definition.armor.id;
    model.armor.uiName = definition.armor.uiName.empty() ? definition.armorType : definition.armor.uiName;
    model.armor.icon = definition.armor.icon;
    model.armor.value = definition.armor.value;
    model.armor.upgradeLevel = upgrades.level(definition.armor.upgradeGroup);
    for (const gamedata::WeaponDefinition& weapon : definition.weapons) {
        model.weapons.push_back({weapon.id, weapon.uiName, weapon.icon, weapon.damage, weapon.range,
            weapon.rateOfFire, weapon.targetTypes, upgrades.level(weapon.upgradeGroup)});
    }
    if (model.weapons.empty() && !definition.primary.id.empty()) {
        const auto& weapon = definition.primary;
        model.weapons.push_back({weapon.id, weapon.uiName, weapon.icon, weapon.damage, weapon.range,
            weapon.rateOfFire, weapon.targetTypes, upgrades.level(weapon.upgradeGroup)});
    }
    for (const std::string& tag : definition.unitTags) {
        model.tags.push_back(rules.tagUiName(tag));
    }
    return model;
}

std::wstring UnitStatusViewModelBuilder::tooltip(const ArmorCardViewModel& card) {
    return std::wstring(card.uiName.begin(), card.uiName.end()) + L"  Armor: " +
        std::to_wstring(card.value) + L"  Upgrade: " + std::to_wstring(card.upgradeLevel);
}

std::wstring UnitStatusViewModelBuilder::tooltip(const WeaponCardViewModel& card) {
    return std::wstring(card.uiName.begin(), card.uiName.end()) + L"  Damage: " +
        std::to_wstring(card.damage) + L"  Range: " + std::to_wstring(card.range) +
        L"  ROF: " + std::to_wstring(card.rateOfFire) + L"  Targets: " +
        std::wstring(card.targetTypes.begin(), card.targetTypes.end());
}

} // namespace ra2yr::client::hud
