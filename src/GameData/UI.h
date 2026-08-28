#pragma once

#include "Engine/Core/Types.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ra2yr::gamedata {

struct UiSkin {
    std::string name;
    std::unordered_map<std::string, std::string> imagePaths;
};

struct UiThemeDefinition {
    UiSkin skin;
};

class UiLayoutDatabase {
public:
    bool load(const std::filesystem::path& path, std::string& error);

    [[nodiscard]] const UiThemeDefinition& theme() const { return theme_; }
    [[nodiscard]] bool loaded() const { return loaded_; }
    [[nodiscard]] bool hasRect(std::string_view key) const;
    [[nodiscard]] Rect rect(std::string_view key) const;
    [[nodiscard]] Rect relativeRect(std::string_view key) const;
    [[nodiscard]] Rect childRect(std::string_view parent, std::string_view child) const;
    [[nodiscard]] bool hasImage(std::string_view key) const;
    [[nodiscard]] std::filesystem::path imagePath(std::string_view key,
        const std::filesystem::path& contentRoot) const;
    [[nodiscard]] float setting(std::string_view section, std::string_view key,
        float fallback = 0.0F) const;

private:
    UiThemeDefinition theme_;
    std::unordered_map<std::string, Rect> rects_;
    std::unordered_map<std::string, Rect> relativeRects_;
    std::unordered_map<std::string, float> settings_;
    bool loaded_ = false;
};

} // namespace ra2yr::gamedata
