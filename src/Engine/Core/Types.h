#pragma once

#include <algorithm>
#include <cstdint>
#include <cmath>

namespace ra2yr {

struct GridCoord {
    int x = 0;
    int y = 0;

    friend bool operator==(const GridCoord&, const GridCoord&) = default;
};

struct WorldCoord {
    float x = 0.0F;
    float y = 0.0F;
};

struct ScreenCoord {
    float x = 0.0F;
    float y = 0.0F;
};

// Direction order is screen-space North, then counter-clockwise around the
// compass.  Art assets can map this stable engine order to their own facing
// indices through Art.ini.
enum class Direction8 : std::uint8_t {
    North,
    NorthWest,
    West,
    SouthWest,
    South,
    SouthEast,
    East,
    NorthEast,
};

enum class TerrainType : std::uint8_t {
    Grass,
};

struct Tile {
    TerrainType terrain = TerrainType::Grass;
    std::int16_t height = 0;
    bool passable = true;
};

struct IsoProjection {
    float tileWidth = 44.0F;
    float tileHeight = 22.0F;
    ScreenCoord origin{790.0F, 440.0F};

    [[nodiscard]] ScreenCoord toScreenVector(WorldCoord coord) const {
        return {(coord.x - coord.y) * tileWidth * 0.5F,
            (coord.x + coord.y) * tileHeight * 0.5F};
    }

    [[nodiscard]] WorldCoord toWorldVector(ScreenCoord screen) const {
        const float horizontal = screen.x / (tileWidth * 0.5F);
        const float vertical = screen.y / (tileHeight * 0.5F);
        return {(horizontal + vertical) * 0.5F,
            (vertical - horizontal) * 0.5F};
    }

    [[nodiscard]] ScreenCoord toScreen(WorldCoord coord) const {
        const ScreenCoord vector = toScreenVector(coord);
        return {origin.x + vector.x, origin.y + vector.y};
    }

    [[nodiscard]] GridCoord toGrid(ScreenCoord screen) const {
        const WorldCoord world = toWorldVector({screen.x - origin.x, screen.y - origin.y});
        return {static_cast<int>(std::lround(world.x)), static_cast<int>(std::lround(world.y))};
    }
};

struct IsometricCamera {
    IsoProjection projection{44.0F, 22.0F, {0.0F, 0.0F}};
    WorldCoord worldCenter{};
    ScreenCoord viewportCenter{795.0F, 440.0F};
    float zoom = 1.0F;
    float minZoom = 0.5F;
    float maxZoom = 2.0F;

    [[nodiscard]] ScreenCoord toScreen(WorldCoord world) const {
        const ScreenCoord vector = projection.toScreenVector({
            world.x - worldCenter.x, world.y - worldCenter.y});
        return {viewportCenter.x + vector.x * zoom, viewportCenter.y + vector.y * zoom};
    }

    [[nodiscard]] WorldCoord toWorld(ScreenCoord screen) const {
        const ScreenCoord vector{
            (screen.x - viewportCenter.x) / zoom,
            (screen.y - viewportCenter.y) / zoom};
        const WorldCoord relative = projection.toWorldVector(vector);
        return {worldCenter.x + relative.x, worldCenter.y + relative.y};
    }

    [[nodiscard]] GridCoord toGrid(ScreenCoord screen) const {
        const WorldCoord world = toWorld(screen);
        return {static_cast<int>(std::lround(world.x)), static_cast<int>(std::lround(world.y))};
    }

    // A positive screen delta moves the camera in that direction, so the
    // world content moves opposite to the cursor edge.
    void panScreen(ScreenCoord delta) {
        worldCenter = toWorld({viewportCenter.x + delta.x, viewportCenter.y + delta.y});
    }

    void zoomAt(ScreenCoord cursor, float factor) {
        const WorldCoord before = toWorld(cursor);
        zoom = std::clamp(zoom * factor, minZoom, maxZoom);
        const WorldCoord after = toWorld(cursor);
        worldCenter.x += before.x - after.x;
        worldCenter.y += before.y - after.y;
    }
};

struct Color {
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
    float a = 1.0F;
};

struct Rect {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;

    [[nodiscard]] bool contains(float px, float py) const {
        return px >= x && px <= x + width && py >= y && py <= y + height;
    }
};

enum class Owner : std::uint8_t {
    Neutral,
    Red,
    Blue,
};

enum class Faction : std::uint8_t {
    Neutral,
    Soviet,
    Allied,
    Yuri,
};

enum class CommandKind : std::uint8_t {
    None,
    Move,
    Stop,
    Hold,
    Patrol,
    AttackMove,
    Attack,
};

struct Command {
    CommandKind kind = CommandKind::None;
    GridCoord destination{};
    std::uint32_t target = 0;
};

[[nodiscard]] inline float distance(WorldCoord a, WorldCoord b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace ra2yr
