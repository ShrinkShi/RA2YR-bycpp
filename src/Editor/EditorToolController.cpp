#include "Editor/EditorToolController.h"

#include "Westwood/Ini/Ini.h"

#include <array>
#include <algorithm>
#include <charconv>
#include <queue>
#include <utility>

namespace ra2yr::editor {
namespace {

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool parseBrushPreset(std::string token, BrushPreset& result) {
    token = trim(std::move(token));
    const std::size_t separator = token.find('x');
    if (separator == std::string::npos || separator == 0 || separator + 1 >= token.size()) {
        return false;
    }
    int width = 0;
    int height = 0;
    const auto widthEnd = token.data() + separator;
    const auto heightStart = token.data() + separator + 1;
    const auto heightEnd = token.data() + token.size();
    const auto widthResult = std::from_chars(token.data(), widthEnd, width);
    const auto heightResult = std::from_chars(heightStart, heightEnd, height);
    if (widthResult.ec != std::errc{} || widthResult.ptr != widthEnd ||
        heightResult.ec != std::errc{} || heightResult.ptr != heightEnd ||
        width <= 0 || height <= 0 || width > 32 || height > 32) {
        return false;
    }
    result = {token, width, height};
    return true;
}

} // namespace

EditorToolController::EditorToolController(TerrainMap& terrain,
    const gamedata::TerrainDatabase& terrainDatabase, const gamedata::RulesDatabase& rules,
    simulation::Simulation& simulation)
    : terrain_(terrain), terrainDatabase_(terrainDatabase), rules_(rules), simulation_(simulation),
      brushPresets_(state_.defaultBrushPresets()) {}

bool EditorToolController::loadBrushPresets(const std::filesystem::path& path, std::string& error) {
    westwood::IniDocument document;
    if (!document.load(path, error)) {
        return false;
    }
    const std::vector<std::string> tokens = [&document] {
        std::vector<std::string> values;
        std::string source = document.get("Brush", "Presets");
        std::size_t start = 0;
        while (start <= source.size()) {
            const std::size_t comma = source.find(',', start);
            const std::size_t end = comma == std::string::npos ? source.size() : comma;
            values.push_back(source.substr(start, end - start));
            if (comma == std::string::npos) {
                break;
            }
            start = comma + 1;
        }
        return values;
    }();
    if (tokens.empty() || (tokens.size() == 1U && trim(tokens.front()).empty())) {
        error = "Editor.ini [Brush] Presets is required";
        return false;
    }
    std::vector<BrushPreset> parsed;
    parsed.reserve(tokens.size());
    for (const std::string& token : tokens) {
        BrushPreset preset;
        if (!parseBrushPreset(token, preset)) {
            error = "Invalid brush preset in Editor.ini: " + token;
            return false;
        }
        parsed.push_back(std::move(preset));
    }
    brushPresets_ = std::move(parsed);
    state_.brushPreset = std::min(state_.brushPreset, brushPresets_.size() - 1U);
    return true;
}

void EditorToolController::beginStroke() {
    strokeCells_.clear();
}

void EditorToolController::endStroke() {
    strokeCells_.clear();
}

bool EditorToolController::isStrokeCell(GridCoord cell) const {
    return strokeCells_.contains({cell.x, cell.y});
}

EditorFeedback EditorToolController::apply(GridCoord cell,
    std::optional<std::uint32_t> entityAtCell) {
    beginStroke();
    const EditorFeedback result = applyCell(cell, entityAtCell);
    endStroke();
    return result;
}

EditorFeedback EditorToolController::continueStroke(GridCoord cell,
    std::optional<std::uint32_t> entityAtCell) {
    if (state_.tool != EditorToolId::Pencil && state_.tool != EditorToolId::Brush) {
        return apply(cell, entityAtCell);
    }
    if (isStrokeCell(cell)) {
        return {false, false, "Stroke cell already edited"};
    }
    const EditorFeedback result = applyCell(cell, entityAtCell);
    return result;
}

void EditorToolController::remember(EditorFeedback feedback) {
    lastFeedback_ = std::move(feedback);
}

EditorFeedback EditorToolController::applyCell(GridCoord cell,
    std::optional<std::uint32_t> entityAtCell) {
    if (!terrain_.contains(cell)) {
        EditorFeedback result{false, true, "Outside terrain map"};
        remember(result);
        return result;
    }
    if (state_.tool == EditorToolId::Pointer) {
        EditorFeedback result{false, false, {}};
        remember(result);
        return result;
    }
    if (state_.tool == EditorToolId::FillBucket) {
        const EditorFeedback result = state_.category == EditorAssetCategory::Terrain ?
            fillTerrain(cell) : EditorFeedback{false, true, "Fill Bucket only supports Terrain"};
        remember(result);
        return result;
    }
    if (state_.tool == EditorToolId::Eyedropper) {
        const TerrainCell& source = terrain_.cell(cell);
        if (state_.category == EditorAssetCategory::Terrain) {
            if (!source.exists) {
                EditorFeedback result{false, true, "Void has no terrain asset"};
                remember(result);
                return result;
            }
            state_.terrainAsset = source.terrainTypeId;
        } else if (state_.category == EditorAssetCategory::Unit && entityAtCell.has_value()) {
            const simulation::Entity* entity = simulation_.find(*entityAtCell);
            if (entity != nullptr) {
                state_.unitAsset = entity->definitionId;
            }
        }
        EditorFeedback result{false, false, "Asset selected"};
        remember(result);
        return result;
    }

    EditorFeedback total;
    const std::vector<GridCoord> cells = state_.tool == EditorToolId::Brush ? brushCells(cell) :
        std::vector<GridCoord>{cell};
    for (const GridCoord target : cells) {
        if (!strokeCells_.insert({target.x, target.y}).second) {
            continue;
        }
        const EditorFeedback result = state_.category == EditorAssetCategory::Terrain ?
            applyTerrain(target) : applyUnit(target, entityAtCell);
        total.changed = total.changed || result.changed;
        total.blocked = total.blocked || result.blocked;
        if (result.blocked) {
            total.message = result.message;
        }
    }
    if (total.message.empty()) {
        total.message = total.changed ? "Editor change applied" : "No change";
    }
    remember(total);
    return total;
}

EditorFeedback EditorToolController::applyTerrain(GridCoord cell) {
    if (state_.tool == EditorToolId::Eraser) {
        if (simulation_.hasUnitAtCell(cell)) {
            return {false, true, "Cannot erase terrain occupied by a unit"};
        }
        return {terrain_.setVoid(cell), false, "Terrain erased"};
    }
    if (!terrainDatabase_.find(state_.terrainAsset)) {
        return {false, true, "Unknown terrain asset: " + state_.terrainAsset};
    }
    if (!validTerrain(cell)) {
        return {false, true, "Terrain edit is outside the map"};
    }
    return {terrain_.setTerrain(cell, state_.terrainAsset), false, "Terrain painted"};
}

EditorFeedback EditorToolController::applyUnit(GridCoord cell,
    std::optional<std::uint32_t> entityAtCell) {
    if (state_.tool == EditorToolId::Eraser) {
        const std::uint32_t id = entityAtCell.value_or(simulation_.entityAtCell(cell));
        if (id == 0) {
            return {false, false, "No unit at cell"};
        }
        return {simulation_.eraseEntity(id), false, "Unit erased"};
    }
    if (!validUnit(cell)) {
        return {false, true, "Unit cannot be placed on void or impassable terrain"};
    }
    const gamedata::UnitDefinition* definition = rules_.findUnit(state_.unitAsset);
    if (definition == nullptr) {
        return {false, true, "Unknown unit asset: " + state_.unitAsset};
    }
    const std::uint32_t id = simulation_.spawn(*definition, state_.owner, cell);
    if (id == 0) {
        return {false, true, "No available infantry subcell"};
    }
    return {true, false, "Unit placed"};
}

EditorFeedback EditorToolController::fillTerrain(GridCoord source) {
    if (!terrain_.cell(source).exists) {
        return {false, true, "Fill source is Void"};
    }
    const std::string sourceId = terrain_.cell(source).terrainTypeId;
    if (!terrainDatabase_.find(state_.terrainAsset)) {
        return {false, true, "Unknown terrain asset: " + state_.terrainAsset};
    }
    if (sourceId == state_.terrainAsset) {
        return {false, false, "Fill source already matches target"};
    }
    std::queue<GridCoord> pending;
    std::set<std::pair<int, int>> visited;
    pending.push(source);
    constexpr std::size_t kSafetyLimit = 4096;
    std::size_t changed = 0;
    constexpr std::array<GridCoord, 4> directions{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
    while (!pending.empty() && visited.size() < kSafetyLimit) {
        const GridCoord current = pending.front();
        pending.pop();
        if (!terrain_.contains(current) || !visited.insert({current.x, current.y}).second ||
            !terrain_.cell(current).exists || terrain_.cell(current).terrainTypeId != sourceId) {
            continue;
        }
        if (terrain_.setTerrain(current, state_.terrainAsset)) {
            ++changed;
        }
        for (const GridCoord direction : directions) {
            pending.push({current.x + direction.x, current.y + direction.y});
        }
    }
    return {changed != 0, visited.size() >= kSafetyLimit, changed == 0 ?
        "No fill change" : "Terrain region filled"};
}

bool EditorToolController::validTerrain(GridCoord cell) const {
    return terrain_.contains(cell);
}

bool EditorToolController::validUnit(GridCoord cell) const {
    const TerrainCell& value = terrain_.cell(cell);
    const gamedata::TerrainDefinition* definition = terrainDatabase_.find(value.terrainTypeId);
    return value.exists && definition != nullptr && definition->passable;
}

std::vector<GridCoord> EditorToolController::brushCells(GridCoord center) const {
    if (brushPresets_.empty()) {
        return {center};
    }
    const BrushPreset& preset = brushPresets_[std::min(state_.brushPreset, brushPresets_.size() - 1U)];
    std::vector<GridCoord> result;
    result.reserve(static_cast<std::size_t>(preset.width * preset.height));
    const int left = (preset.width - 1) / 2;
    const int top = (preset.height - 1) / 2;
    for (int y = 0; y < preset.height; ++y) {
        for (int x = 0; x < preset.width; ++x) {
            result.push_back({center.x + x - left, center.y + y - top});
        }
    }
    return result;
}

std::vector<GridCoord> EditorToolController::previewCells(GridCoord center) const {
    if (state_.tool == EditorToolId::Brush) {
        return brushCells(center);
    }
    return {center};
}

} // namespace ra2yr::editor
