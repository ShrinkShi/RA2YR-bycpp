#include "Editor/TerrainMap.h"

#include <algorithm>

namespace ra2yr::editor {

TerrainMap::TerrainMap(int width, int height)
    : width_(std::max(0, width)), height_(std::max(0, height)),
      cells_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_)) {}

bool TerrainMap::contains(GridCoord coordinate) const {
    return coordinate.x >= 0 && coordinate.x < width_ && coordinate.y >= 0 && coordinate.y < height_;
}

const TerrainCell& TerrainMap::cell(GridCoord coordinate) const {
    static const TerrainCell empty;
    return contains(coordinate) ? cells_[index(coordinate)] : empty;
}

TerrainCell& TerrainMap::cell(GridCoord coordinate) {
    return cells_[index(coordinate)];
}

std::size_t TerrainMap::index(GridCoord coordinate) const {
    return static_cast<std::size_t>(coordinate.y) * static_cast<std::size_t>(width_) +
        static_cast<std::size_t>(coordinate.x);
}

bool TerrainMap::setTerrain(GridCoord coordinate, std::string_view terrainTypeId) {
    if (!contains(coordinate) || terrainTypeId.empty()) {
        return false;
    }
    TerrainCell& target = cell(coordinate);
    if (target.exists && target.terrainTypeId == terrainTypeId) {
        return false;
    }
    target.terrainTypeId = terrainTypeId;
    target.exists = true;
    dirty_ = true;
    return true;
}

bool TerrainMap::setVoid(GridCoord coordinate) {
    if (!contains(coordinate) || !cell(coordinate).exists) {
        return false;
    }
    TerrainCell& target = cell(coordinate);
    target.terrainTypeId.clear();
    target.exists = false;
    dirty_ = true;
    return true;
}

void TerrainMap::fill(std::string_view terrainTypeId) {
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            static_cast<void>(setTerrain({x, y}, terrainTypeId));
        }
    }
}

std::size_t TerrainMap::existingCount() const {
    return static_cast<std::size_t>(std::count_if(cells_.begin(), cells_.end(),
        [](const TerrainCell& cellValue) { return cellValue.exists; }));
}

bool TerrainMap::consumeDirty() {
    const bool wasDirty = dirty_;
    dirty_ = false;
    return wasDirty;
}

} // namespace ra2yr::editor
