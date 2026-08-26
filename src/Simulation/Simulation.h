#pragma once

#include "Engine/Core/Types.h"
#include "GameData/Art.h"
#include "GameData/Rules.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ra2yr::simulation {

enum class AnimationState : std::uint8_t {
    Idle,
    Walk,
    Attack,
    Death,
};

struct Entity {
    std::uint32_t id = 0;
    std::string definitionId;
    std::vector<std::string> unitTags;
    Owner owner = Owner::Neutral;
    Faction faction = Faction::Neutral;
    WorldCoord position{};
    int health = 1;
    int maxHealth = 1;
    int speed = 1;
    int sight = 5;
    float weaponRange = 1.0F;
    int weaponDamage = 1;
    float weaponCooldown = 0.0F;
    bool autoAcquire = true;
    bool returnFire = true;
    std::uint32_t recentAttacker = 0;
    Command order{};
    std::optional<GridCoord> patrolPoint;
    bool selected = false;
    AnimationState animationState = AnimationState::Idle;
    int facing = 0;
    int animationFrame = 0;
    float animationTime = 0.0F;
    std::uint32_t attackEvent = 0;
};

class Simulation {
public:
    explicit Simulation(const gamedata::ArtDefinition& animationDefinition);

    std::uint32_t spawn(const gamedata::UnitDefinition& definition, Owner owner, GridCoord position);
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

private:
    void setAnimation(Entity& entity, AnimationState state);
    void updateAnimation(Entity& entity, float seconds);
    void applyToSelected(const Command& command);
    void updateEntity(Entity& entity, float seconds);
    [[nodiscard]] Entity* nearestEnemy(const Entity& source, float maxDistance);
    [[nodiscard]] const gamedata::AnimationSequence* animationSequence(AnimationState state) const;
    [[nodiscard]] static const char* animationSequenceName(AnimationState state);
    [[nodiscard]] static bool isAlive(const Entity& entity) { return entity.health > 0; }

    gamedata::ArtDefinition animationDefinition_;
    std::vector<Entity> entities_;
    std::uint32_t nextId_ = 1;
};

} // namespace ra2yr::simulation
