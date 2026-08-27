#include "Simulation/Simulation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace ra2yr::simulation {
namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kInfantrySubcellOffset = 0.30F;

WorldCoord toWorld(GridCoord coord) {
    return {static_cast<float>(coord.x), static_cast<float>(coord.y)};
}

GridCoord toGrid(WorldCoord coord) {
    return {static_cast<int>(std::lround(coord.x)), static_cast<int>(std::lround(coord.y))};
}

std::pair<int, int> cellKey(GridCoord cell) {
    return {cell.x, cell.y};
}

WorldCoord subcellOffset(InfantrySubcell subcell) {
    switch (subcell) {
    case InfantrySubcell::TopCenter:
        return {0.0F, -kInfantrySubcellOffset};
    case InfantrySubcell::BottomLeft:
        return {-kInfantrySubcellOffset, kInfantrySubcellOffset};
    case InfantrySubcell::BottomRight:
        return {kInfantrySubcellOffset, kInfantrySubcellOffset};
    case InfantrySubcell::None:
        return {};
    }
    return {};
}

bool movementOrder(CommandKind kind) {
    return kind == CommandKind::Move || kind == CommandKind::Patrol || kind == CommandKind::AttackMove;
}

bool hostile(Owner first, Owner second) {
    return first != Owner::Neutral && second != Owner::Neutral && first != second;
}

float cross(WorldCoord a, WorldCoord b, WorldCoord point) {
    return (b.x - a.x) * (point.y - a.y) - (b.y - a.y) * (point.x - a.x);
}

bool pointInConvexQuad(WorldCoord point, const std::array<WorldCoord, 4>& corners) {
    int sign = 0;
    for (std::size_t index = 0; index < corners.size(); ++index) {
        const WorldCoord first = corners[index];
        const WorldCoord second = corners[(index + 1) % corners.size()];
        const float edgeCross = cross(first, second, point);
        if (std::abs(edgeCross) < 0.001F) {
            continue;
        }
        const int currentSign = edgeCross > 0.0F ? 1 : -1;
        if (sign == 0) {
            sign = currentSign;
        } else if (sign != currentSign) {
            return false;
        }
    }
    return true;
}

} // namespace

const char* infantrySubcellName(InfantrySubcell subcell) {
    switch (subcell) {
    case InfantrySubcell::TopCenter:
        return "TopCenter";
    case InfantrySubcell::BottomLeft:
        return "BottomLeft";
    case InfantrySubcell::BottomRight:
        return "BottomRight";
    case InfantrySubcell::None:
        return "None";
    }
    return "None";
}

Simulation::Simulation(const gamedata::ArtDefinition& animationDefinition,
    const gamedata::VeterancyDatabase* veterancyDatabase)
    : animationDefinition_(animationDefinition), veterancyDatabase_(veterancyDatabase) {}

Direction8 Simulation::directionFromDelta(float dx, float dy) {
    if (std::abs(dx) < 0.001F && std::abs(dy) < 0.001F) {
        return Direction8::North;
    }

    // Convert grid movement to the same 2:1 isometric screen basis used by
    // IsoProjection before selecting the nearest screen-space octant.
    const float screenDx = dx - dy;
    const float screenDy = (dx + dy) * 0.5F;
    float angle = std::atan2(-screenDx, -screenDy);
    if (angle < 0.0F) {
        angle += 2.0F * kPi;
    }
    const int index = static_cast<int>(std::lround(angle / (kPi * 0.25F))) % 8;
    return static_cast<Direction8>(index);
}

std::uint32_t Simulation::spawn(const gamedata::UnitDefinition& definition, Owner owner, GridCoord position) {
    Entity entity;
    entity.id = nextId_++;
    entity.definitionId = definition.id;
    entity.unitTags = definition.unitTags;
    entity.owner = owner;
    entity.faction = definition.faction;
    entity.selectionRadius = definition.selectionRadius;
    entity.occupancyProfile = definition.occupancyProfile;
    entity.occupancyCell = position;
    if (isInfantry(entity)) {
        const auto location = findAvailableSubcell(position, entity.id);
        if (!location.has_value()) {
            return 0;
        }
        entity.occupancyCell = location->cell;
        entity.occupancySubcell = location->subcell;
        entity.position = subcellPosition(entity.occupancyCell, entity.occupancySubcell);
    } else {
        entity.position = toWorld(position);
    }
    entity.health = definition.strength;
    entity.maxHealth = definition.strength;
    entity.speed = std::max(1, definition.speed);
    entity.sight = std::max(1, definition.sight);
    entity.weaponRange = std::max(0.1F, definition.primary.range);
    entity.weaponDamage = std::max(1, definition.primary.damage);
    entity.autoAcquire = definition.autoAcquire;
    entity.returnFire = definition.returnFire;
    entity.experienceValue = definition.experienceValue;
    entity.veterancyProfile = definition.veterancyProfile;
    entity.veterancyLevel = definition.initialVeterancy;
    entity.energy = definition.initialEnergy;
    entity.maxEnergy = definition.maxEnergy;
    if (isInfantry(entity) && entity.occupancySubcell != InfantrySubcell::None) {
        infantryOccupancy_[cellKey(entity.occupancyCell)][static_cast<std::size_t>(entity.occupancySubcell)] = entity.id;
    }
    entities_.push_back(std::move(entity));
    return entities_.back().id;
}

void Simulation::update(float seconds) {
    const float elapsed = std::max(0.0F, seconds);
    for (Entity& entity : entities_) {
        if (isAlive(entity)) {
            updateEntity(entity, elapsed);
        } else {
            setAnimation(entity, AnimationState::Death);
            updateAnimation(entity, elapsed);
        }
    }

    const auto* deathSequence = animationSequence(AnimationState::Death);
    const int lastDeathFrame = deathSequence == nullptr ? 0 : deathSequence->frameCount - 1;
    for (Entity& entity : entities_) {
        if (entity.health <= 0 && entity.animationState == AnimationState::Death &&
            entity.animationFrame >= lastDeathFrame) {
            releaseReservation(entity);
            releaseOccupancy(entity);
        }
    }
    entities_.erase(std::remove_if(entities_.begin(), entities_.end(), [lastDeathFrame](const Entity& entity) {
        return entity.health <= 0 && entity.animationState == AnimationState::Death &&
            entity.animationFrame >= lastDeathFrame;
    }), entities_.end());
}

void Simulation::updateEntity(Entity& entity, float seconds) {
    if (entity.weaponCooldown > 0.0F) {
        entity.weaponCooldown = std::max(0.0F, entity.weaponCooldown - seconds);
    }

    const CommandKind orderKind = entity.order.kind;
    Entity* target = nullptr;
    bool mayChaseTarget = false;
    if (orderKind == CommandKind::Attack) {
        target = find(entity.order.target);
        mayChaseTarget = true;
    } else if (orderKind == CommandKind::AttackMove) {
        target = nearestEnemy(entity, entity.weaponRange + 3.0F);
        mayChaseTarget = true;
    } else {
        if (entity.returnFire && entity.recentAttacker != 0) {
            Entity* attacker = find(entity.recentAttacker);
            if (attacker != nullptr && isAlive(*attacker) && hostile(entity.owner, attacker->owner) &&
                distance(entity.position, attacker->position) <= entity.weaponRange) {
                target = attacker;
            }
        }
        const bool canAutoAcquire = orderKind == CommandKind::None || orderKind == CommandKind::Stop ||
            orderKind == CommandKind::Hold;
        if (target == nullptr && entity.autoAcquire && canAutoAcquire) {
            // Guarding and weapon range are intentionally bounded by the weapon itself in this slice.
            target = nearestEnemy(entity, std::min(entity.weaponRange, static_cast<float>(entity.sight)));
        }
    }

    bool moving = false;
    bool attacking = false;
    if (target != nullptr && isAlive(*target)) {
        const float targetDistance = distance(entity.position, target->position);
        if (targetDistance > entity.weaponRange) {
            if (mayChaseTarget) {
                const float dx = target->position.x - entity.position.x;
                const float dy = target->position.y - entity.position.y;
                const float length = std::sqrt(dx * dx + dy * dy);
                if (length > 0.001F) {
                    const float step = static_cast<float>(entity.speed) * seconds * 0.55F;
                    setFacing(entity, dx, dy);
                    entity.position.x += dx / length * std::min(step, length);
                    entity.position.y += dy / length * std::min(step, length);
                    moving = true;
                }
            }
        } else {
            attacking = true;
            setFacing(entity, target->position.x - entity.position.x,
                target->position.y - entity.position.y);
            if (entity.weaponCooldown <= 0.0F) {
                target->health = std::max(0, target->health - entity.weaponDamage);
                target->recentAttacker = entity.id;
                entity.weaponCooldown = 25.0F / 30.0F;
                ++entity.attackEvent;
                if (!isAlive(*target)) {
                    awardExperience(entity, *target);
                    target->order = {};
                    target->patrolPoint.reset();
                    target->selected = false;
                }
            }
        }
    }

    const WorldCoord destination = movementDestination(entity);
    const bool hasDestination = orderKind == CommandKind::Move || orderKind == CommandKind::Patrol ||
        orderKind == CommandKind::AttackMove;
    if (!attacking && !moving && (target == nullptr || !isAlive(*target)) && hasDestination &&
        distance(entity.position, destination) > 0.12F) {
        const float dx = destination.x - entity.position.x;
        const float dy = destination.y - entity.position.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length > 0.001F) {
            const float step = static_cast<float>(entity.speed) * seconds * 0.55F;
            setFacing(entity, dx, dy);
            entity.position.x += dx / length * std::min(step, length);
            entity.position.y += dy / length * std::min(step, length);
            moving = true;
        }
    } else if (!attacking && !moving && hasDestination && distance(entity.position, destination) <= 0.12F) {
        commitReservation(entity);
        if (orderKind == CommandKind::Patrol && entity.patrolPoint.has_value()) {
            std::swap(entity.order.destination, *entity.patrolPoint);
            static_cast<void>(reserveDestination(entity, entity.order.destination));
        }
    }

    if (moving && isInfantry(entity)) {
        applyInfantrySeparation(entity, seconds);
    }

    if (attacking) {
        setAnimation(entity, AnimationState::Attack);
    } else if (moving) {
        setAnimation(entity, AnimationState::Walk);
    } else {
        setAnimation(entity, AnimationState::Idle);
    }
    updateAnimation(entity, seconds);
}

void Simulation::setAnimation(Entity& entity, AnimationState state) {
    if (entity.animationState == state) {
        return;
    }
    entity.animationState = state;
    entity.animationFrame = 0;
    entity.animationTime = 0.0F;
}

void Simulation::setFacing(Entity& entity, float dx, float dy) {
    entity.direction = directionFromDelta(dx, dy);
    entity.facing = animationDefinition_.facingMap[static_cast<std::size_t>(entity.direction)];
}

void Simulation::updateAnimation(Entity& entity, float seconds) {
    const gamedata::AnimationSequence* sequence = animationSequence(entity.animationState);
    if (sequence == nullptr || sequence->frameCount <= 0) {
        return;
    }
    if (!sequence->loop && entity.animationFrame >= sequence->frameCount - 1) {
        return;
    }

    entity.animationTime += seconds;
    const float frameSeconds = static_cast<float>(sequence->frameDelayMs) / 1000.0F;
    while (entity.animationTime >= frameSeconds) {
        entity.animationTime -= frameSeconds;
        ++entity.animationFrame;
        if (sequence->loop) {
            entity.animationFrame %= sequence->frameCount;
        } else {
            entity.animationFrame = std::min(entity.animationFrame, sequence->frameCount - 1);
        }
        if (!sequence->loop && entity.animationFrame >= sequence->frameCount - 1) {
            break;
        }
    }
}

void Simulation::clearSelection() {
    for (Entity& entity : entities_) {
        entity.selected = false;
    }
}

void Simulation::selectSingle(GridCoord position, float radius) {
    clearSelection();
    Entity* best = nullptr;
    float bestDistance = radius;
    for (Entity& entity : entities_) {
        if (!isAlive(entity)) {
            continue;
        }
        const float candidateDistance = distance(entity.position, toWorld(position));
        if (candidateDistance <= bestDistance) {
            best = &entity;
            bestDistance = candidateDistance;
        }
    }
    if (best != nullptr) {
        best->selected = true;
    }
}

void Simulation::selectEntity(std::uint32_t id) {
    clearSelection();
    Entity* entity = find(id);
    if (entity != nullptr && isAlive(*entity)) {
        entity->selected = true;
    }
}

void Simulation::selectBox(WorldCoord topLeft, WorldCoord bottomRight) {
    clearSelection();
    const float left = std::min(topLeft.x, bottomRight.x);
    const float right = std::max(topLeft.x, bottomRight.x);
    const float top = std::min(topLeft.y, bottomRight.y);
    const float bottom = std::max(topLeft.y, bottomRight.y);
    for (Entity& entity : entities_) {
        entity.selected = isAlive(entity) && entity.position.x >= left &&
            entity.position.x <= right && entity.position.y >= top && entity.position.y <= bottom;
    }
}

void Simulation::selectBox(const std::array<WorldCoord, 4>& corners) {
    clearSelection();
    for (Entity& entity : entities_) {
        entity.selected = isAlive(entity) && pointInConvexQuad(entity.position, corners);
    }
}

void Simulation::applyToSelected(const Command& command, bool clearRecentAttacker) {
    for (Entity& entity : entities_) {
        if (entity.selected && isAlive(entity)) {
            releaseReservation(entity);
            entity.order = command;
            if (clearRecentAttacker) {
                entity.recentAttacker = 0;
            }
            if (command.kind != CommandKind::Patrol) {
                entity.patrolPoint.reset();
            }
            if (movementOrder(command.kind)) {
                static_cast<void>(reserveDestination(entity, command.destination));
            }
        }
    }
}

void Simulation::issueMove(GridCoord destination) {
    std::vector<std::uint32_t> infantryIds;
    for (const Entity& entity : entities_) {
        if (entity.selected && isAlive(entity) && isInfantry(entity)) {
            infantryIds.push_back(entity.id);
        }
    }
    std::sort(infantryIds.begin(), infantryIds.end());

    // Remove the selected formation from occupancy before choosing target slots.
    // This lets a formation move back into its current cells without blocking itself.
    for (const std::uint32_t id : infantryIds) {
        Entity* entity = find(id);
        if (entity != nullptr) {
            releaseReservation(*entity);
            releaseOccupancy(*entity);
        }
    }

    const auto slotAvailable = [this](GridCoord cell, int slot) {
        const auto occupied = infantryOccupancy_.find(cellKey(cell));
        if (occupied != infantryOccupancy_.end() && occupied->second[static_cast<std::size_t>(slot)] != 0) {
            return false;
        }
        const auto reserved = infantryReservations_.find(cellKey(cell));
        return reserved == infantryReservations_.end() ||
            reserved->second[static_cast<std::size_t>(slot)] == 0;
    };
    std::vector<SubcellLocation> availableSlots;
    for (int radius = 0; radius <= 32 && availableSlots.size() < infantryIds.size(); ++radius) {
        for (int y = destination.y - radius; y <= destination.y + radius; ++y) {
            for (int x = destination.x - radius; x <= destination.x + radius; ++x) {
                if (std::max(std::abs(x - destination.x), std::abs(y - destination.y)) != radius) {
                    continue;
                }
                // Add the complete cell before advancing to the next cell. This
                // gives 6 infantry two cells and 9 infantry three cells.
                for (int slot = 0; slot < 3; ++slot) {
                    if (slotAvailable({x, y}, slot)) {
                        availableSlots.push_back({{x, y}, static_cast<InfantrySubcell>(slot)});
                    }
                }
                if (availableSlots.size() >= infantryIds.size()) {
                    break;
                }
            }
            if (availableSlots.size() >= infantryIds.size()) {
                break;
            }
        }
    }

    for (std::size_t index = 0; index < infantryIds.size(); ++index) {
        Entity* entity = find(infantryIds[index]);
        if (entity == nullptr) {
            continue;
        }
        entity->order = {CommandKind::Move, destination, 0};
        entity->recentAttacker = 0;
        entity->patrolPoint.reset();
        if (index >= availableSlots.size()) {
            continue;
        }
        const SubcellLocation& location = availableSlots[index];
        entity->reservedCell = location.cell;
        entity->reservedSubcell = location.subcell;
        infantryReservations_[cellKey(location.cell)][static_cast<std::size_t>(location.subcell)] = entity->id;
    }

    // Non-infantry units keep the same command semantics, but do not participate
    // in the three-slot infantry formation allocator.
    for (Entity& entity : entities_) {
        if (entity.selected && isAlive(entity) && !isInfantry(entity)) {
            releaseReservation(entity);
            entity.order = {CommandKind::Move, destination, 0};
            entity.recentAttacker = 0;
            entity.patrolPoint.reset();
        }
    }
}

void Simulation::issueStop() {
    applyToSelected({CommandKind::Stop, {}, 0});
}

void Simulation::issueHold() {
    applyToSelected({CommandKind::Hold, {}, 0});
}

void Simulation::issuePatrol(GridCoord destination) {
    for (Entity& entity : entities_) {
        if (entity.selected && isAlive(entity)) {
            releaseReservation(entity);
            entity.patrolPoint = entity.occupancyCell;
            entity.order = {CommandKind::Patrol, destination, 0};
            entity.recentAttacker = 0;
            static_cast<void>(reserveDestination(entity, destination));
        }
    }
}

void Simulation::issueAttackMove(GridCoord destination) {
    applyToSelected({CommandKind::AttackMove, destination, 0}, true);
}

void Simulation::issueAttack(std::uint32_t target) {
    applyToSelected({CommandKind::Attack, {}, target}, true);
}

bool Simulation::eraseEntity(std::uint32_t id) {
    const auto it = std::find_if(entities_.begin(), entities_.end(), [id](const Entity& entity) {
        return entity.id == id;
    });
    if (it == entities_.end()) {
        return false;
    }
    releaseReservation(*it);
    releaseOccupancy(*it);
    entities_.erase(it);
    return true;
}

const Entity* Simulation::find(std::uint32_t id) const {
    const auto it = std::find_if(entities_.begin(), entities_.end(), [id](const Entity& entity) {
        return entity.id == id;
    });
    return it == entities_.end() ? nullptr : &*it;
}

Entity* Simulation::find(std::uint32_t id) {
    const auto it = std::find_if(entities_.begin(), entities_.end(), [id](const Entity& entity) {
        return entity.id == id;
    });
    return it == entities_.end() ? nullptr : &*it;
}

std::uint32_t Simulation::entityAtCell(GridCoord cell) const {
    for (const Entity& entity : entities_) {
        if (isAlive(entity) && entity.occupancyCell == cell) {
            return entity.id;
        }
    }
    return 0;
}

void Simulation::awardExperience(Entity& attacker, const Entity& target) {
    ++attacker.killCount;
    attacker.experience += std::max(0, target.experienceValue);
    if (veterancyDatabase_ != nullptr) {
        const gamedata::VeterancyLevelDefinition* current =
            veterancyDatabase_->level(attacker.veterancyProfile, attacker.experience);
        if (current != nullptr) {
            attacker.veterancyLevel = current->id;
        }
    }
}

Entity* Simulation::nearestEnemy(const Entity& source, float maxDistance) {
    Entity* result = nullptr;
    float resultDistance = maxDistance;
    for (Entity& candidate : entities_) {
        if (!isAlive(candidate) || !hostile(source.owner, candidate.owner)) {
            continue;
        }
        const float candidateDistance = distance(source.position, candidate.position);
        if (candidateDistance < resultDistance) {
            resultDistance = candidateDistance;
            result = &candidate;
        }
    }
    return result;
}

void Simulation::applyInfantrySeparation(Entity& entity, float seconds) {
    constexpr float kPersonalSpace = 0.42F;
    const WorldCoord reservedTarget = movementDestination(entity);
    const bool closeToReservation = entity.reservedSubcell != InfantrySubcell::None &&
        distance(entity.position, reservedTarget) < 0.75F;
    for (const Entity& other : entities_) {
        if (other.id == entity.id || !isAlive(other) || !isInfantry(other)) {
            continue;
        }
        const float dx = entity.position.x - other.position.x;
        const float dy = entity.position.y - other.position.y;
        const float currentDistance = std::sqrt(dx * dx + dy * dy);
        if (currentDistance >= kPersonalSpace) {
            continue;
        }
        const float directionX = currentDistance > 0.001F ? dx / currentDistance :
            (entity.id < other.id ? -1.0F : 1.0F);
        const float directionY = currentDistance > 0.001F ? dy / currentDistance : 0.0F;
        const float strength = (kPersonalSpace - currentDistance) / kPersonalSpace *
            seconds * (closeToReservation ? 0.35F : 1.25F);
        entity.position.x += directionX * strength;
        entity.position.y += directionY * strength;
    }
}

bool Simulation::isInfantry(const Entity& entity) {
    return entity.occupancyProfile == "Infantry";
}

WorldCoord Simulation::subcellPosition(GridCoord cell, InfantrySubcell subcell) {
    const WorldCoord center = toWorld(cell);
    const WorldCoord offset = subcellOffset(subcell);
    return {center.x + offset.x, center.y + offset.y};
}

std::optional<Simulation::SubcellLocation> Simulation::findAvailableSubcell(GridCoord requested,
    std::uint32_t entityId) const {
    float bestDistance = std::numeric_limits<float>::max();
    std::optional<SubcellLocation> best;
    const WorldCoord requestedCenter = toWorld(requested);
    for (int radius = 0; radius <= 8; ++radius) {
        for (int y = requested.y - radius; y <= requested.y + radius; ++y) {
            for (int x = requested.x - radius; x <= requested.x + radius; ++x) {
                if (std::max(std::abs(x - requested.x), std::abs(y - requested.y)) != radius) {
                    continue;
                }
                const auto it = infantryOccupancy_.find(cellKey({x, y}));
                const auto reservation = infantryReservations_.find(cellKey({x, y}));
                for (int slot = 0; slot < 3; ++slot) {
                    const std::uint32_t occupant = it == infantryOccupancy_.end() ? 0 :
                        it->second[static_cast<std::size_t>(slot)];
                    const std::uint32_t reserved = reservation == infantryReservations_.end() ? 0 :
                        reservation->second[static_cast<std::size_t>(slot)];
                    if ((occupant != 0 && occupant != entityId) || (reserved != 0 && reserved != entityId)) {
                        continue;
                    }
                    const InfantrySubcell candidate = static_cast<InfantrySubcell>(slot);
                    const float candidateDistance = distance(subcellPosition({x, y}, candidate), requestedCenter);
                    if (candidateDistance < bestDistance) {
                        bestDistance = candidateDistance;
                        best = SubcellLocation{{x, y}, candidate};
                    }
                }
            }
        }
        if (best.has_value() && radius > 0) {
            break;
        }
    }
    return best;
}

void Simulation::releaseOccupancy(Entity& entity) {
    if (!isInfantry(entity) || entity.occupancySubcell == InfantrySubcell::None) {
        return;
    }
    const auto it = infantryOccupancy_.find(cellKey(entity.occupancyCell));
    if (it == infantryOccupancy_.end()) {
        return;
    }
    auto& slots = it->second;
    const std::size_t slot = static_cast<std::size_t>(entity.occupancySubcell);
    if (slot < slots.size() && slots[slot] == entity.id) {
        slots[slot] = 0;
    }
    if (std::all_of(slots.begin(), slots.end(), [](std::uint32_t occupant) { return occupant == 0; })) {
        infantryOccupancy_.erase(it);
    }
}

void Simulation::releaseReservation(Entity& entity) {
    if (!isInfantry(entity)) {
        return;
    }
    if (entity.reservedSubcell != InfantrySubcell::None) {
        const auto it = infantryReservations_.find(cellKey(entity.reservedCell));
        if (it != infantryReservations_.end()) {
            auto& slots = it->second;
            const std::size_t slot = static_cast<std::size_t>(entity.reservedSubcell);
            if (slot < slots.size() && slots[slot] == entity.id) {
                slots[slot] = 0;
            }
            if (std::all_of(slots.begin(), slots.end(), [](std::uint32_t occupant) { return occupant == 0; })) {
                infantryReservations_.erase(it);
            }
        }
    }
    entity.reservedSubcell = InfantrySubcell::None;
    entity.reservedCell = {};
}

bool Simulation::reserveDestination(Entity& entity, GridCoord destination) {
    if (!isInfantry(entity) || destination == entity.occupancyCell) {
        return true;
    }
    const auto location = findAvailableSubcell(destination, entity.id);
    if (!location.has_value()) {
        return false;
    }
    entity.reservedCell = location->cell;
    entity.reservedSubcell = location->subcell;
    infantryReservations_[cellKey(location->cell)][static_cast<std::size_t>(location->subcell)] = entity.id;
    return true;
}

void Simulation::commitReservation(Entity& entity) {
    if (!isInfantry(entity) || entity.reservedSubcell == InfantrySubcell::None) {
        return;
    }
    const GridCoord destinationCell = entity.reservedCell;
    const InfantrySubcell destinationSubcell = entity.reservedSubcell;
    releaseReservation(entity);
    releaseOccupancy(entity);
    entity.occupancyCell = destinationCell;
    entity.occupancySubcell = destinationSubcell;
    entity.position = subcellPosition(entity.occupancyCell, entity.occupancySubcell);
    infantryOccupancy_[cellKey(entity.occupancyCell)][static_cast<std::size_t>(entity.occupancySubcell)] = entity.id;
}

WorldCoord Simulation::movementDestination(const Entity& entity) const {
    if (isInfantry(entity) && entity.reservedSubcell != InfantrySubcell::None) {
        return subcellPosition(entity.reservedCell, entity.reservedSubcell);
    }
    if (isInfantry(entity) && entity.order.destination == entity.occupancyCell) {
        return entity.position;
    }
    return toWorld(entity.order.destination);
}

const gamedata::AnimationSequence* Simulation::animationSequence(AnimationState state) const {
    const auto it = animationDefinition_.sequences.find(animationSequenceName(state));
    return it == animationDefinition_.sequences.end() ? nullptr : &it->second;
}

const char* Simulation::animationSequenceName(AnimationState state) {
    switch (state) {
    case AnimationState::Idle:
        return "Ready";
    case AnimationState::Walk:
        return "Walk";
    case AnimationState::Attack:
        return "Fire";
    case AnimationState::Death:
        return "Death";
    }
    return "Ready";
}

} // namespace ra2yr::simulation
