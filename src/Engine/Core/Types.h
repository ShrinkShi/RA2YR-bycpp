#pragma once

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

    [[nodiscard]] ScreenCoord toScreen(WorldCoord coord) const {
        return {origin.x + (coord.x - coord.y) * tileWidth * 0.5F,
            origin.y + (coord.x + coord.y) * tileHeight * 0.5F};
    }

    [[nodiscard]] GridCoord toGrid(ScreenCoord screen) const {
        const float horizontal = (screen.x - origin.x) / (tileWidth * 0.5F);
        const float vertical = (screen.y - origin.y) / (tileHeight * 0.5F);
        return {static_cast<int>(std::lround((horizontal + vertical) * 0.5F)),
            static_cast<int>(std::lround((vertical - horizontal) * 0.5F))};
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
