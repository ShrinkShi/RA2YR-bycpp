#pragma once

#include "Editor/EditorToolState.h"
#include "Editor/TerrainMap.h"
#include "GameData/Rules.h"
#include "GameData/Terrain.h"
#include "Simulation/Simulation.h"

#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ra2yr::editor {

struct EditorFeedback {
    bool changed = false;
    bool blocked = false;
    std::string message;
};

class EditorToolController {
public:
    EditorToolController(TerrainMap& terrain, const gamedata::TerrainDatabase& terrainDatabase,
        const gamedata::RulesDatabase& rules, simulation::Simulation& simulation);

    [[nodiscard]] EditorToolState& state() { return state_; }
    [[nodiscard]] const EditorToolState& state() const { return state_; }
    [[nodiscard]] const std::vector<BrushPreset>& brushPresets() const { return brushPresets_; }
    bool loadBrushPresets(const std::filesystem::path& path, std::string& error);
    [[nodiscard]] EditorFeedback apply(GridCoord cell, std::optional<std::uint32_t> entityAtCell = {});
    [[nodiscard]] EditorFeedback continueStroke(GridCoord cell,
        std::optional<std::uint32_t> entityAtCell = {});
    void beginStroke();
    void endStroke();
    [[nodiscard]] std::vector<GridCoord> previewCells(GridCoord center) const;
    [[nodiscard]] const EditorFeedback& lastFeedback() const { return lastFeedback_; }
    [[nodiscard]] bool isStrokeCell(GridCoord cell) const;

private:
    [[nodiscard]] EditorFeedback applyCell(GridCoord cell, std::optional<std::uint32_t> entityAtCell);
    [[nodiscard]] EditorFeedback applyTerrain(GridCoord cell);
    [[nodiscard]] EditorFeedback applyUnit(GridCoord cell, std::optional<std::uint32_t> entityAtCell);
    [[nodiscard]] EditorFeedback fillTerrain(GridCoord source);
    [[nodiscard]] bool validTerrain(GridCoord cell) const;
    [[nodiscard]] bool validUnit(GridCoord cell) const;
    [[nodiscard]] std::vector<GridCoord> brushCells(GridCoord center) const;
    void remember(EditorFeedback feedback);

    TerrainMap& terrain_;
    const gamedata::TerrainDatabase& terrainDatabase_;
    const gamedata::RulesDatabase& rules_;
    simulation::Simulation& simulation_;
    EditorToolState state_;
    std::vector<BrushPreset> brushPresets_;
    std::set<std::pair<int, int>> strokeCells_;
    EditorFeedback lastFeedback_;
};

} // namespace ra2yr::editor
