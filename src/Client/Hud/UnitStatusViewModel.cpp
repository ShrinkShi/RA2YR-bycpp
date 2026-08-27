#include "Client/Hud/UnitStatusViewModel.h"

#include "Engine/Core/Utf.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace {

std::wstring number(float value) {
    std::wostringstream stream;
    stream << std::fixed << std::setprecision(2) << value;
    std::wstring result = stream.str();
    while (!result.empty() && result.back() == L'0') {
        result.pop_back();
    }
    if (!result.empty() && result.back() == L'.') {
        result.pop_back();
    }
    return result;
}

std::wstring targetLabel(std::string_view targetTypes) {
    if (targetTypes == "Ground") {
        return L"地面单位";
    }
    if (targetTypes == "Air") {
        return L"空中单位";
    }
    if (targetTypes == "Ground,Air" || targetTypes == "Air,Ground") {
        return L"地面/空中单位";
    }
    return ra2yr::utf8ToWide(targetTypes);
}

} // namespace

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
    if (!model.armor.uiName.empty()) {
        model.tags.push_back(model.armor.uiName);
    }
    return model;
}

std::wstring UnitStatusViewModelBuilder::tooltip(const ArmorCardViewModel& card) {
    return ra2yr::utf8ToWide(card.uiName) + L"\n护甲：" + std::to_wstring(card.value) +
        L"\n升级：" + std::to_wstring(card.upgradeLevel) + L"/5";
}

std::wstring UnitStatusViewModelBuilder::tooltip(const WeaponCardViewModel& card) {
    const float interval = card.rateOfFire > 0 ? 25.0F / static_cast<float>(card.rateOfFire) : 0.0F;
    return ra2yr::utf8ToWide(card.uiName) + L"\n伤害：" + std::to_wstring(card.damage) +
        L"\n攻击范围：" + number(card.range) + L"\n攻击间隔：" + number(interval) + L"秒\n目标：" +
        targetLabel(card.targetTypes) + L"\n升级：" + std::to_wstring(card.upgradeLevel) + L"/5";
}

} // namespace ra2yr::client::hud
