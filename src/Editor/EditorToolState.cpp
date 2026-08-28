#include "Editor/EditorToolState.h"

namespace ra2yr::editor {

std::vector<BrushPreset> EditorToolState::defaultBrushPresets() const {
    return {{"1x1", 1, 1}, {"2x2", 2, 2}, {"3x3", 3, 3}, {"4x4", 4, 4},
        {"5x5", 5, 5}, {"1x3", 1, 3}, {"3x1", 3, 1}};
}

const char* editorToolName(EditorToolId tool) {
    switch (tool) {
    case EditorToolId::Pointer: return "Pointer";
    case EditorToolId::Pencil: return "Pencil";
    case EditorToolId::Eraser: return "Eraser";
    case EditorToolId::Brush: return "Brush";
    case EditorToolId::FillBucket: return "Fill Bucket";
    case EditorToolId::Eyedropper: return "Eyedropper";
    }
    return "Pointer";
}

const char* editorCategoryName(EditorAssetCategory category) {
    switch (category) {
    case EditorAssetCategory::Terrain: return "Terrain";
    case EditorAssetCategory::Unit: return "Unit";
    case EditorAssetCategory::Building: return "Building";
    case EditorAssetCategory::Resource: return "Resource";
    }
    return "Terrain";
}

const char* ownerName(Owner owner) {
    switch (owner) {
    case Owner::Red: return "Red";
    case Owner::Blue: return "Blue";
    case Owner::Neutral: return "Neutral";
    }
    return "Neutral";
}

} // namespace ra2yr::editor
