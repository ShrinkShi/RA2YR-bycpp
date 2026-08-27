#include "Simulation/Simulation.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ra2yr::simulation {
namespace {

constexpr float kPi = 3.14159265358979323846F;

WorldCoord toWorld(GridCoord coord) {
    return {static_cast<float>(coord.x), static_cast<float>(coord.y)};
}

GridCoord toGrid(WorldCoord coord) {
    return {static_cast<int>(std::lround(coord.x)), static_cast<int>(std::lround(coord.y))};
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

Simulation::Simulation(const gamedata::ArtDefinition& animationDefinition)
    : animationDefinition_(animationDefinition) {}

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
    entity.position = toWorld(position);
    entity.health = definition.strength;
    entity.maxHealth = definition.strength;
    entity.speed = std::max(1, definition.speed);
    entity.sight = std::max(1, definition.sight);
    entity.weaponRange = std::max(0.1F, definition.primary.range);
    entity.weaponDamage = std::max(1, definition.primary.damage);
    entity.autoAcquire = definition.autoAcquire;
    entity.returnFire = definition.returnFire;
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
                    target->order = {};
                    target->patrolPoint.reset();
                    target->selected = false;
                }
            }
        }
    }

    const WorldCoord destination = toWorld(entity.order.destination);
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
    } else if (!attacking && !moving && orderKind == CommandKind::Patrol && entity.patrolPoint.has_value() &&
        distance(entity.position, destination) <= 0.12F) {
        std::swap(entity.order.destination, *entity.patrolPoint);
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
            entity.order = command;
            if (clearRecentAttacker) {
                entity.recentAttacker = 0;
            }
            if (command.kind != CommandKind::Patrol) {
                entity.patrolPoint.reset();
            }
        }
    }
}

void Simulation::issueMove(GridCoord destination) {
    applyToSelected({CommandKind::Move, destination, 0}, true);
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
            entity.patrolPoint = toGrid(entity.position);
            entity.order = {CommandKind::Patrol, destination, 0};
            entity.recentAttacker = 0;
        }
    }
}

void Simulation::issueAttackMove(GridCoord destination) {
    applyToSelected({CommandKind::AttackMove, destination, 0}, true);
}

void Simulation::issueAttack(std::uint32_t target) {
    applyToSelected({CommandKind::Attack, {}, target}, true);
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
