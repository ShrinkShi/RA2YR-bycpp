#pragma once

#include "Engine/Core/Types.h"
#include "GameData/Rules.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace ra2yr::simulation {

struct Entity {
    std::uint32_t id = 0;
    Owner owner = Owner::Neutral;
    Faction faction = Faction::Neutral;
    WorldCoord position{};
    int health = 1;
    int maxHealth = 1;
    int speed = 1;
    float weaponRange = 1.0F;
    int weaponDamage = 1;
    float weaponCooldown = 0.0F;
    Command order{};
    std::optional<GridCoord> patrolPoint;
    bool selected = false;
};

class Simulation {
public:
    explicit Simulation(const gamedata::UnitDefinition& definition);

    std::uint32_t spawn(Owner owner, GridCoord position);
    void update(float seconds);
    void clearSelection();
    void selectSingle(GridCoord position, float radius = 0.8F);
    void selectBox(WorldCoord topLeft, WorldCoord bottomRight);

    void issueMove(GridCoord destination);
    void issueStop();
    void issueHold();
    void issuePatrol(GridCoord destination);
    void issueAttackMove(GridCoord destination);
    void issueAttack(std::uint32_t target);

    [[nodiscard]] const std::vector<Entity>& entities() const { return entities_; }
    [[nodiscard]] std::vector<Entity>& entities() { return entities_; }
    [[nodiscard]] const Entity* find(std::uint32_t id) const;
    [[nodiscard]] Entity* find(std::uint32_t id);
    [[nodiscard]] int animationFrame() const { return animationFrame_; }

private:
    void applyToSelected(const Command& command);
    void updateEntity(Entity& entity, float seconds);
    [[nodiscard]] Entity* nearestEnemy(const Entity& source, float maxDistance);
    [[nodiscard]] static bool isAlive(const Entity& entity) { return entity.health > 0; }

    gamedata::UnitDefinition definition_;
    std::vector<Entity> entities_;
    std::uint32_t nextId_ = 1;
    float animationTime_ = 0.0F;
    int animationFrame_ = 0;
};

} // namespace ra2yr::simulation
