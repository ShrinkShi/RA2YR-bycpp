#include "Simulation/Simulation.h"

#include <algorithm>
#include <cmath>

namespace ra2yr::simulation {
namespace {

WorldCoord toWorld(GridCoord coord) {
    return {static_cast<float>(coord.x), static_cast<float>(coord.y)};
}

GridCoord toGrid(WorldCoord coord) {
    return {static_cast<int>(std::lround(coord.x)), static_cast<int>(std::lround(coord.y))};
}

bool hostile(Owner first, Owner second) {
    return first != Owner::Neutral && second != Owner::Neutral && first != second;
}

} // namespace

Simulation::Simulation(const gamedata::UnitDefinition& definition) : definition_(definition) {}

std::uint32_t Simulation::spawn(Owner owner, GridCoord position) {
    Entity entity;
    entity.id = nextId_++;
    entity.owner = owner;
    entity.faction = owner == Owner::Red ? Faction::SovietRules : owner == Owner::Blue ? Faction::BlueRules : Faction::Neutral;
    entity.position = toWorld(position);
    entity.health = definition_.strength;
    entity.maxHealth = definition_.strength;
    entity.speed = std::max(1, definition_.speed);
    entity.weaponRange = definition_.primary.range;
    entity.weaponDamage = std::max(1, definition_.primary.damage);
    entities_.push_back(entity);
    return entity.id;
}

void Simulation::update(float seconds) {
    animationTime_ += seconds;
    while (animationTime_ >= 0.08F) {
        animationTime_ -= 0.08F;
        animationFrame_ = (animationFrame_ + 1) % 8;
    }
    for (Entity& entity : entities_) {
        if (isAlive(entity)) {
            updateEntity(entity, seconds);
        }
    }
    entities_.erase(std::remove_if(entities_.begin(), entities_.end(), [](const Entity& entity) {
        return entity.health <= 0 && entity.order.kind == CommandKind::None;
    }), entities_.end());
}

void Simulation::updateEntity(Entity& entity, float seconds) {
    if (entity.weaponCooldown > 0.0F) {
        entity.weaponCooldown = std::max(0.0F, entity.weaponCooldown - seconds);
    }

    const WorldCoord destination = toWorld(entity.order.destination);
    const bool hasDestination = entity.order.kind == CommandKind::Move ||
        entity.order.kind == CommandKind::Patrol || entity.order.kind == CommandKind::AttackMove;
    if (hasDestination && distance(entity.position, destination) > 0.12F) {
        const float dx = destination.x - entity.position.x;
        const float dy = destination.y - entity.position.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        const float step = static_cast<float>(entity.speed) * seconds * 0.55F;
        entity.position.x += dx / length * std::min(step, length);
        entity.position.y += dy / length * std::min(step, length);
    } else if (entity.order.kind == CommandKind::Patrol && entity.patrolPoint.has_value()) {
        std::swap(entity.order.destination, *entity.patrolPoint);
    }

    Entity* target = entity.order.kind == CommandKind::Attack ? find(entity.order.target) : nullptr;
    if (entity.order.kind == CommandKind::AttackMove && target == nullptr) {
        target = nearestEnemy(entity, entity.weaponRange + 3.0F);
        if (target != nullptr) {
            entity.order.kind = CommandKind::Attack;
            entity.order.target = target->id;
        }
    }
    if (target != nullptr && isAlive(*target)) {
        const float targetDistance = distance(entity.position, target->position);
        if (targetDistance > entity.weaponRange) {
            const float dx = target->position.x - entity.position.x;
            const float dy = target->position.y - entity.position.y;
            const float length = std::sqrt(dx * dx + dy * dy);
            const float step = static_cast<float>(entity.speed) * seconds * 0.55F;
            entity.position.x += dx / length * std::min(step, length);
            entity.position.y += dy / length * std::min(step, length);
        } else if (entity.weaponCooldown <= 0.0F) {
            target->health -= entity.weaponDamage;
            entity.weaponCooldown = 25.0F / 30.0F;
            if (target->health <= 0) {
                target->health = 0;
                target->order = {};
            }
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

void Simulation::applyToSelected(const Command& command) {
    for (Entity& entity : entities_) {
        if (entity.selected && isAlive(entity)) {
            entity.order = command;
            if (command.kind != CommandKind::Patrol) {
                entity.patrolPoint.reset();
            }
        }
    }
}

void Simulation::issueMove(GridCoord destination) {
    applyToSelected({CommandKind::Move, destination, 0});
}

void Simulation::issueStop() {
    applyToSelected({CommandKind::Stop, {}, 0});
    for (Entity& entity : entities_) {
        if (entity.selected) {
            entity.order = {};
        }
    }
}

void Simulation::issueHold() {
    applyToSelected({CommandKind::Hold, {}, 0});
}

void Simulation::issuePatrol(GridCoord destination) {
    for (Entity& entity : entities_) {
        if (entity.selected && isAlive(entity)) {
            entity.patrolPoint = toGrid(entity.position);
            entity.order = {CommandKind::Patrol, destination, 0};
        }
    }
}

void Simulation::issueAttackMove(GridCoord destination) {
    applyToSelected({CommandKind::AttackMove, destination, 0});
}

void Simulation::issueAttack(std::uint32_t target) {
    applyToSelected({CommandKind::Attack, {}, target});
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

} // namespace ra2yr::simulation
