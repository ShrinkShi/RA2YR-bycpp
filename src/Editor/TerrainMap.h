#pragma once

#include "Engine/Core/Types.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ra2yr::editor {

struct TerrainCell {
    std::string terrainTypeId;
    std::int16_t height = 0;
    bool exists = false;
};

class TerrainMap {
public:
    TerrainMap(int width, int height);

    [[nodiscard]] bool contains(GridCoord cell) const;
    [[nodiscard]] const TerrainCell& cell(GridCoord coordinate) const;
    [[nodiscard]] TerrainCell& cell(GridCoord coordinate);
    bool setTerrain(GridCoord coordinate, std::string_view terrainTypeId);
    bool setVoid(GridCoord coordinate);
    void fill(std::string_view terrainTypeId);
    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }
    [[nodiscard]] std::size_t existingCount() const;
    [[nodiscard]] bool consumeDirty();

private:
    [[nodiscard]] std::size_t index(GridCoord coordinate) const;

    int width_ = 0;
    int height_ = 0;
    std::vector<TerrainCell> cells_;
    bool dirty_ = true;
};

} // namespace ra2yr::editor
