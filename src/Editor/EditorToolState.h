#pragma once

#include "Engine/Core/Types.h"

#include <string>
#include <string_view>
#include <vector>

namespace ra2yr::editor {

enum class EditorToolId : std::uint8_t {
    Pointer,
    Pencil,
    Eraser,
    Brush,
    FillBucket,
    Eyedropper,
};

enum class EditorAssetCategory : std::uint8_t {
    Terrain,
    Unit,
    Building,
    Resource,
};

struct BrushPreset {
    std::string id;
    int width = 1;
    int height = 1;
};

class EditorToolState {
public:
    EditorToolId tool = EditorToolId::Pointer;
    EditorAssetCategory category = EditorAssetCategory::Terrain;
    std::string terrainAsset = "GRASS";
    std::string unitAsset = "E2";
    Owner owner = Owner::Red;
    std::size_t brushPreset = 0;
    bool placing = false;

    [[nodiscard]] bool editsWorld() const { return tool != EditorToolId::Pointer; }
    [[nodiscard]] bool categoryEnabled() const {
        return category == EditorAssetCategory::Terrain || category == EditorAssetCategory::Unit;
    }
    [[nodiscard]] const std::string& currentAsset() const {
        return category == EditorAssetCategory::Terrain ? terrainAsset : unitAsset;
    }
    [[nodiscard]] std::vector<BrushPreset> defaultBrushPresets() const;
};

[[nodiscard]] const char* editorToolName(EditorToolId tool);
[[nodiscard]] const char* editorCategoryName(EditorAssetCategory category);
[[nodiscard]] const char* ownerName(Owner owner);

} // namespace ra2yr::editor
