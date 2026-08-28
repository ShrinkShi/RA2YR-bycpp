#pragma once

#include "Engine/Core/Types.h"
#include "GameData/Art.h"
#include "GameData/Rules.h"
#include "GameData/Veterancy.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace ra2yr::simulation {

enum class AnimationState : std::uint8_t {
    Idle,
    Walk,
    Attack,
    Death,
};

enum class InfantrySubcell : std::int8_t {
    None = -1,
    TopCenter = 0,
    BottomLeft = 1,
    BottomRight = 2,
};

[[nodiscard]] const char* infantrySubcellName(InfantrySubcell subcell);

struct Entity {
    std::uint32_t id = 0;
    std::string definitionId;
    std::vector<std::string> unitTags;
    Owner owner = Owner::Neutral;
    Faction faction = Faction::Neutral;
    WorldCoord position{};
    float selectionRadius = 0.30F;
    std::string occupancyProfile;
    GridCoord occupancyCell{};
    InfantrySubcell occupancySubcell = InfantrySubcell::None;
    GridCoord reservedCell{};
    InfantrySubcell reservedSubcell = InfantrySubcell::None;
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
    Direction8 direction = Direction8::North;
    int facing = 0;
    int animationFrame = 0;
    float animationTime = 0.0F;
    std::uint32_t attackEvent = 0;
    std::uint32_t killCount = 0;
    int experience = 0;
    int experienceValue = 0;
    std::string veterancyProfile;
    std::string veterancyLevel;
    std::vector<int> shields;
    int energy = 0;
    int maxEnergy = 0;
};

class Simulation {
public:
    explicit Simulation(const gamedata::ArtDefinition& animationDefinition,
        const gamedata::VeterancyDatabase* veterancyDatabase = nullptr);

    std::uint32_t spawn(const gamedata::UnitDefinition& definition, Owner owner, GridCoord position);
    void update(float seconds);
    void clearSelection();
    void selectSingle(GridCoord position, float radius = 0.8F);
    void selectEntity(std::uint32_t id);
    void selectBox(WorldCoord topLeft, WorldCoord bottomRight);
    void selectBox(const std::array<WorldCoord, 4>& corners);

    [[nodiscard]] static Direction8 directionFromDelta(float dx, float dy);

    void issueMove(GridCoord destination);
    void issueStop();
    void issueHold();
    void issuePatrol(GridCoord destination);
    void issueAttackMove(GridCoord destination);
    void issueAttack(std::uint32_t target);
    void issueForceAttack(GridCoord destination, std::uint32_t target = 0);
    bool eraseEntity(std::uint32_t id);

    [[nodiscard]] const std::vector<Entity>& entities() const { return entities_; }
    [[nodiscard]] std::vector<Entity>& entities() { return entities_; }
    [[nodiscard]] const Entity* find(std::uint32_t id) const;
    [[nodiscard]] Entity* find(std::uint32_t id);
    [[nodiscard]] std::uint32_t entityAtCell(GridCoord cell) const;
    [[nodiscard]] bool hasUnitAtCell(GridCoord cell) const { return entityAtCell(cell) != 0; }

private:
    struct SubcellLocation {
        GridCoord cell{};
        InfantrySubcell subcell = InfantrySubcell::None;
    };

    void setAnimation(Entity& entity, AnimationState state);
    void setFacing(Entity& entity, float dx, float dy);
    void updateAnimation(Entity& entity, float seconds);
    void applyToSelected(const Command& command, bool clearRecentAttacker = false);
    void issueGroupMove(GridCoord destination);
    void updateEntity(Entity& entity, float seconds);
    void applyInfantrySeparation(Entity& entity, float seconds);
    void releaseOccupancy(Entity& entity);
    void releaseReservation(Entity& entity);
    [[nodiscard]] bool reserveDestination(Entity& entity, GridCoord destination);
    void commitReservation(Entity& entity);
    [[nodiscard]] WorldCoord movementDestination(const Entity& entity) const;
    [[nodiscard]] static bool isInfantry(const Entity& entity);
    [[nodiscard]] std::optional<SubcellLocation> findAvailableSubcell(GridCoord requested,
        std::uint32_t entityId);
    static WorldCoord subcellPosition(GridCoord cell, InfantrySubcell subcell);
    [[nodiscard]] Entity* nearestEnemy(const Entity& source, float maxDistance);
    void awardExperience(Entity& attacker, const Entity& target);
    [[nodiscard]] const gamedata::AnimationSequence* animationSequence(AnimationState state) const;
    [[nodiscard]] static const char* animationSequenceName(AnimationState state);
    [[nodiscard]] static bool isAlive(const Entity& entity) { return entity.health > 0; }

    gamedata::ArtDefinition animationDefinition_;
    std::vector<Entity> entities_;
    std::map<std::pair<int, int>, std::array<std::uint32_t, 3>> infantryOccupancy_;
    std::map<std::pair<int, int>, std::array<std::uint32_t, 3>> infantryReservations_;
    std::uint32_t nextId_ = 1;
    const gamedata::VeterancyDatabase* veterancyDatabase_ = nullptr;
    std::mt19937 random_{std::random_device{}()};
};

} // namespace ra2yr::simulation
